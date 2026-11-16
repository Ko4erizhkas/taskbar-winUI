// Точка входа BarKitTap.dll.
//
// BarKit.exe вызывает InitializeXamlDiagnosticsEx для explorer.exe — это штатный механизм,
// которым пользуются инструменты отладки XAML (и TranslucentTB). Windows загружает эту DLL
// в explorer.exe, создаёт наш COM-объект и передаёт ему IXamlDiagnostics. Через него мы
// подписываемся на изменения визуального дерева и получаем доступ к элементам панели задач.

#include "pch.h"
#include "TaskbarManager.h"
#include "Log.h"
#include "../shared/Settings.h"

#include <thread>

namespace barkit
{
    namespace
    {
        HMODULE g_module = nullptr;

        // ---------------------------------------------------------------- наблюдатель за деревом

        struct TreeWatcher;
        std::atomic<TreeWatcher*> g_currentWatcher{ nullptr }; // для потока подписки: «я всё ещё актуален?»

        struct TreeWatcher : winrt::implements<TreeWatcher, IVisualTreeServiceCallback2, winrt::non_agile>
        {
            explicit TreeWatcher(winrt::com_ptr<IXamlDiagnostics> diagnostics) : m_diagnostics(std::move(diagnostics))
            {
            }

            void Start()
            {
                // AdviseVisualTreeChange нельзя звать из потока, в котором пришёл SetSite (возможна
                // взаимоблокировка с UI-потоком), поэтому — отдельный поток. Он держит ссылку на объект.
                AddRef();
                HANDLE thread = CreateThread(nullptr, 0, &TreeWatcher::AdviseThread, this, 0, nullptr);
                if (thread)
                {
                    CloseHandle(thread);
                }
                else
                {
                    Release();
                    BARKIT_LOG(L"TAP: CreateThread failed: %lu", GetLastError());
                }
            }

            void Stop() noexcept
            {
                try
                {
                    winrt::com_ptr<IVisualTreeService3> service;
                    if (SUCCEEDED(m_diagnostics->QueryInterface(IID_PPV_ARGS(service.put()))))
                    {
                        service->UnadviseVisualTreeChange(this);
                    }
                }
                catch (...)
                {
                    LogCurrentException(L"TreeWatcher::Stop");
                }
            }

            HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation, VisualElement element, VisualMutationType mutationType) noexcept override
            {
                try
                {
                    if (mutationType != Add)
                    {
                        TaskbarManager::Instance().OnTreeMutated();
                        return S_OK;
                    }

                    const std::wstring_view type = element.Type ? std::wstring_view(element.Type) : std::wstring_view();
                    const std::wstring_view name = element.Name ? std::wstring_view(element.Name) : std::wstring_view();
                    if (!TaskbarManager::IsInteresting(type, name))
                    {
                        return S_OK;
                    }

                    wf::IInspectable object;
                    const HRESULT hr = m_diagnostics->GetIInspectableFromHandle(element.Handle,
                        reinterpret_cast<::IInspectable**>(winrt::put_abi(object)));
                    if (FAILED(hr) || !object)
                    {
                        return S_OK;
                    }

                    if (auto frameworkElement = object.try_as<wux::FrameworkElement>())
                    {
                        TaskbarManager::Instance().OnElementAdded(std::wstring(type), std::wstring(name), frameworkElement);
                    }
                }
                catch (...)
                {
                    LogCurrentException(L"OnVisualTreeChange");
                }
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR) noexcept override
            {
                return S_OK;
            }

        private:
            static DWORD WINAPI AdviseThread(void* parameter)
            {
                winrt::com_ptr<TreeWatcher> self;
                self.attach(static_cast<TreeWatcher*>(parameter));

                // Подписка может не удаться с первого раза (XAML возвращает E_UNEXPECTED, если дерево
                // менялось во время перечисления), поэтому пробуем несколько раз.
                HRESULT hr = E_NOINTERFACE;
                for (int attempt = 0; attempt < 3; ++attempt)
                {
                    if (g_currentWatcher.load() != self.get())
                    {
                        return 0; // нас уже заменил более новый наблюдатель
                    }
                    try
                    {
                        winrt::com_ptr<IVisualTreeService3> service;
                        hr = self->m_diagnostics->QueryInterface(IID_PPV_ARGS(service.put()));
                        if (SUCCEEDED(hr))
                        {
                            TaskbarManager::Instance().BeginAdvise();
                            hr = service->AdviseVisualTreeChange(self.get());
                            TaskbarManager::Instance().EndAdvise();
                            if (FAILED(hr))
                            {
                                service->UnadviseVisualTreeChange(self.get());
                            }
                        }
                    }
                    catch (...)
                    {
                        TaskbarManager::Instance().EndAdvise();
                        LogCurrentException(L"TreeWatcher::AdviseThread");
                        hr = E_FAIL;
                    }

                    BARKIT_LOG(L"TAP: AdviseVisualTreeChange (attempt %d) -> 0x%08X", attempt + 1, static_cast<unsigned int>(hr));
                    if (SUCCEEDED(hr))
                    {
                        break;
                    }
                    Sleep(700);
                }

                // Даже при ошибке подписки XAML успевает показать нам элементы панели, и всё работает
                // (за новыми панелями следит таймер). Поэтому «подключено» — это «панель найдена».
                const bool ready = TaskbarManager::Instance().IsReady();
                if (FAILED(hr) && ready)
                {
                    BARKIT_LOG(L"TAP: subscription failed, but the taskbar was found - working without tree notifications");
                }
                if (SUCCEEDED(hr) || ready)
                {
                    // Сообщаем приложению, что DLL работает в этом explorer.exe (дескриптор намеренно не закрываем).
                    wchar_t modulePath[MAX_PATH]{};
                    GetModuleFileNameW(g_module, modulePath, MAX_PATH);
                    static HANDLE loadedMutex = CreateMutexW(nullptr, FALSE, TapLoadedMutexName(GetCurrentProcessId(), modulePath).c_str());
                    (void)loadedMutex;
                }
                return 0;
            }

            winrt::com_ptr<IXamlDiagnostics> m_diagnostics;
        };

        std::mutex g_watcherMutex;
        winrt::com_ptr<TreeWatcher> g_watcher;
        void* g_watcherOwner = nullptr;

        // ---------------------------------------------------------------- фоновые службы DLL

        void SettingsWatchThread()
        {
            HKEY key = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0, KEY_NOTIFY | KEY_READ, nullptr, &key, nullptr) != ERROR_SUCCESS)
            {
                BARKIT_LOG(L"TAP: cannot open settings key");
                return;
            }
            HANDLE changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!changed)
            {
                RegCloseKey(key);
                return;
            }

            for (;;)
            {
                const LSTATUS status = RegNotifyChangeKeyValue(key, FALSE,
                    REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_THREAD_AGNOSTIC, changed, TRUE);
                if (status != ERROR_SUCCESS)
                {
                    BARKIT_LOG(L"TAP: RegNotifyChangeKeyValue failed: %ld", status);
                    Sleep(2000);
                    continue;
                }
                WaitForSingleObject(changed, INFINITE);
                Sleep(60); // приложение пишет несколько значений подряд — даём ему закончить
                TaskbarManager::Instance().NotifySettingsChanged();
            }
        }

        void StartOnce()
        {
            static std::once_flag once;
            std::call_once(once, []()
                {
                    // DLL остаётся в explorer.exe до его перезапуска: у нас есть потоки и подписки на события.
                    HMODULE pinned = nullptr;
                    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                        reinterpret_cast<LPCWSTR>(&StartOnce), &pinned);

                    // Приложение по этим значениям понимает, какая копия DLL загружена.
                    wchar_t path[MAX_PATH]{};
                    GetModuleFileNameW(g_module, path, MAX_PATH);
                    HKEY key = nullptr;
                    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRuntimePath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS)
                    {
                        const DWORD pid = GetCurrentProcessId();
                        RegSetValueExW(key, L"TapPid", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&pid), sizeof(pid));
                        RegSetValueExW(key, L"TapPath", 0, REG_SZ, reinterpret_cast<const BYTE*>(path),
                            static_cast<DWORD>((wcslen(path) + 1) * sizeof(wchar_t)));
                        RegCloseKey(key);
                    }

                    BARKIT_LOG(L"TAP: loaded into pid %lu from %s", GetCurrentProcessId(), path);
                    std::thread(SettingsWatchThread).detach();
                });
        }

        // ---------------------------------------------------------------- COM-объект для XAML Diagnostics

        struct TapSite : winrt::implements<TapSite, IObjectWithSite, winrt::non_agile>
        {
            HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) noexcept override
            {
                try
                {
                    std::scoped_lock guard(g_watcherMutex);

                    if (!site)
                    {
                        if (g_watcherOwner == this && g_watcher)
                        {
                            g_watcher->Stop();
                            g_watcher = nullptr;
                            g_watcherOwner = nullptr;
                        }
                        m_site = nullptr;
                        return S_OK;
                    }

                    StartOnce();

                    // Повторное подключение (приложение перезапустили): старый наблюдатель заменяем новым.
                    if (g_watcher)
                    {
                        g_watcher->Stop();
                        g_watcher = nullptr;
                    }

                    m_site.copy_from(site);
                    winrt::com_ptr<IXamlDiagnostics> diagnostics;
                    const HRESULT hr = site->QueryInterface(IID_PPV_ARGS(diagnostics.put()));
                    if (FAILED(hr))
                    {
                        BARKIT_LOG(L"TAP: site has no IXamlDiagnostics: 0x%08X", static_cast<unsigned int>(hr));
                        return hr;
                    }

                    g_watcher = winrt::make_self<TreeWatcher>(std::move(diagnostics));
                    g_watcherOwner = this;
                    g_currentWatcher = g_watcher.get();
                    g_watcher->Start();
                    return S_OK;
                }
                catch (...)
                {
                    LogCurrentException(L"TapSite::SetSite");
                    return E_FAIL;
                }
            }

            HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** result) noexcept override
            {
                if (!result)
                {
                    return E_POINTER;
                }
                *result = nullptr;
                return m_site ? m_site->QueryInterface(riid, result) : E_FAIL;
            }

        private:
            winrt::com_ptr<IUnknown> m_site;
        };

        struct TapFactory : winrt::implements<TapFactory, IClassFactory, winrt::non_agile>
        {
            HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** result) noexcept override
            {
                if (!result)
                {
                    return E_POINTER;
                }
                *result = nullptr;
                if (outer)
                {
                    return CLASS_E_NOAGGREGATION;
                }
                try
                {
                    auto instance = winrt::make_self<TapSite>();
                    return instance->QueryInterface(riid, result);
                }
                catch (...)
                {
                    return E_OUTOFMEMORY;
                }
            }

            HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override
            {
                return S_OK;
            }
        };
    }
}

// Экспорты перечислены в BarKitTap.def.

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID riid, void** result)
{
    if (!result)
    {
        return E_POINTER;
    }
    *result = nullptr;
    if (!IsEqualCLSID(clsid, barkit::kTapClsid))
    {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    try
    {
        auto factory = winrt::make_self<barkit::TapFactory>();
        return factory->QueryInterface(riid, result);
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
}

extern "C" HRESULT __stdcall DllCanUnloadNow()
{
    return S_FALSE;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        barkit::g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
