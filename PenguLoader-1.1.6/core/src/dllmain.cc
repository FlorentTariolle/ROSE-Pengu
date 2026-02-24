#include "pengu.h"
#include "hook.h"
#include "logger.h"
#include "include/cef_version.h"

bool check_libcef_version(bool is_browser);
void HookBrowserProcess();
void HookRendererProcess();

#if OS_WIN

EXTERN_C IMAGE_DOS_HEADER __ImageBase;
void InjectThisDll(HANDLE hProcess);

static bool wcsfindi(const wchar_t *str, const wchar_t *sub)
{
    size_t str_len = wcslen(str), sub_len = wcslen(sub);
    if (sub_len > str_len)
        return false;
    for (size_t i = 0; i <= str_len - sub_len; ++i) {
        for (size_t j = 0; j < sub_len; ++j)
            if (towlower(str[i + j]) != towlower(sub[j]))
                goto next;
        return true;
        next:;
    }
    return false;
}

static hook::Hook<decltype(&CreateProcessW)> Old_CreateProcessW;
static BOOL WINAPI Hooked_CreateProcessW(LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
    bool is_renderer = wcsfindi(lpCommandLine, L"LeagueClientUxRender.exe")
        && wcsfindi(lpCommandLine, L"--type=renderer");

    if (is_renderer)
        dwCreationFlags |= CREATE_SUSPENDED;

    BOOL success = Old_CreateProcessW(lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
        bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);

    if (success && is_renderer)
    {
        InjectThisDll(lpProcessInformation->hProcess);
        ResumeThread(lpProcessInformation->hThread);
    }

    return success;
}

static void Initialize()
{
    logger::info("DllMain", "========================================");
    logger::info("DllMain", "Initialize() called - core.dll loaded");
    logger::infof("DllMain", "CEF_VERSION_MAJOR: %d", CEF_VERSION_MAJOR);

    // Fast process check — only proceed for target processes.
    // This avoids any file I/O or config reading in non-target processes.
    WCHAR exe_path[2048]{};
    GetModuleFileNameW(nullptr, exe_path, _countof(exe_path));

    logger::info_w("DllMain", (std::wstring(L"Process exe_path: ") + exe_path).c_str());

    bool is_browser = wcsfindi(exe_path, L"LeagueClientUx.exe");
    bool is_renderer = wcsfindi(exe_path, L"LeagueClientUxRender.exe");

    logger::infof("DllMain", "is_browser=%d, is_renderer=%d", is_browser, is_renderer);

    if (!is_browser && !is_renderer)
    {
        logger::info("DllMain", "Not a target process, exiting Initialize()");
        return;
    }

    // Check %LOCALAPPDATA%\Rose\config.ini for disabled flag and valid loaderpath.
    {
        WCHAR appdata[2048]{};
        WCHAR thisPath[2048]{};
        GetModuleFileNameW((HMODULE)&__ImageBase, thisPath, _countof(thisPath));

        logger::info_w("DllMain", (std::wstring(L"This DLL path: ") + thisPath).c_str());

        if (GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, 2048) > 0)
        {
            std::wstring cfg = std::wstring(appdata) + L"\\Rose\\config.ini";
            logger::info_w("DllMain", (std::wstring(L"Config path: ") + cfg).c_str());

            // Check disabled flag — if set, skip hooking entirely.
            // Do NOT clear the flag here; the loader manages it on re-activation.
            wchar_t disabled[64]{};
            GetPrivateProfileStringW(L"General", L"disabled", L"0", disabled, 64, cfg.c_str());
            logger::info_w("DllMain", (std::wstring(L"disabled flag: ") + disabled).c_str());

            if (disabled[0] == L'1')
            {
                logger::warn("DllMain", "disabled=1, skipping hooking");
                return;
            }

            // If loaderpath is missing, the loader isn't installed — don't hook.
            // Try to self-delete so League returns to normal.
            wchar_t loaderpath[2048]{};
            GetPrivateProfileStringW(L"General", L"loaderpath", L"", loaderpath, 2048, cfg.c_str());
            logger::info_w("DllMain", (std::wstring(L"loaderpath: ") + loaderpath).c_str());

            if (loaderpath[0] == L'\0')
            {
                logger::warn("DllMain", "loaderpath is empty, attempting self-delete and exiting");
                DeleteFileW(thisPath);
                return;
            }

            logger::info("DllMain", "Config checks passed, proceeding with hooks");
        }
        else
        {
            // Can't read config at all — don't hook, try to clean up
            logger::error("DllMain", "Failed to get LOCALAPPDATA, attempting self-delete and exiting");
            DeleteFileW(thisPath);
            return;
        }
    }

    // Determine which process to be hooked.
    if (is_browser)
    {
        logger::info("DllMain", "Browser process detected, checking CEF version...");
        if (check_libcef_version(true))
        {
            logger::info("DllMain", "CEF version check passed, hooking browser process...");
            HookBrowserProcess();
            logger::info("DllMain", "HookBrowserProcess() completed");

            // Hook CreateProcessW.
            logger::info("DllMain", "Hooking CreateProcessW for renderer injection...");
            Old_CreateProcessW.hook(&CreateProcessW, Hooked_CreateProcessW);
            logger::info("DllMain", "CreateProcessW hooked successfully");
        }
        else
        {
            logger::error("DllMain", "CEF version check FAILED, not hooking");
        }
    }
    else if (is_renderer)
    {
        logger::info("DllMain", "Renderer process detected");
        // Renderer only.
        if (wcsstr(GetCommandLineW(), L"--type=renderer") != nullptr)
        {
            logger::info("DllMain", "Confirmed renderer via command line, checking CEF version...");
            if (check_libcef_version(false))
            {
                logger::info("DllMain", "CEF version check passed, hooking renderer process...");
                HookRendererProcess();
                logger::info("DllMain", "HookRendererProcess() completed");
            }
            else
            {
                logger::error("DllMain", "CEF version check FAILED for renderer");
            }
        }
        else
        {
            logger::warn("DllMain", "Renderer process but --type=renderer not in command line");
        }
    }

    logger::info("DllMain", "Initialize() completed");
    logger::info("DllMain", "========================================");
}

// DLL entry point.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
        {
            // Log IMMEDIATELY to catch any early crashes
            logger::info("DllMain", "======== CORE.DLL LOADED ========");
            logger::info("DllMain", "DLL_PROCESS_ATTACH received");

            // Log what DLL name we're running as (d3d9.dll, version.dll, etc.)
            WCHAR dllPath[2048]{};
            GetModuleFileNameW(module, dllPath, _countof(dllPath));
            logger::info_w("DllMain", (std::wstring(L"Running as: ") + dllPath).c_str());

            // Extract just the filename
            WCHAR* fileName = wcsrchr(dllPath, L'\\');
            fileName = fileName ? fileName + 1 : dllPath;

            // Only the PRIMARY proxy should do the hooking (avoid conflicts if multiple loaded)
            // Priority: winhttp.dll > d3d9.dll > version.dll > dwrite.dll
            bool isPrimaryProxy = (_wcsicmp(fileName, L"winhttp.dll") == 0);

            if (!isPrimaryProxy)
            {
                // Check if winhttp.dll is already loaded - if so, we're secondary
                WCHAR leagueDir[2048]{};
                wcscpy_s(leagueDir, dllPath);
                WCHAR* lastSlash = wcsrchr(leagueDir, L'\\');
                if (lastSlash) *lastSlash = L'\0';

                std::wstring primaryPath = std::wstring(leagueDir) + L"\\winhttp.dll";
                HMODULE primaryModule = GetModuleHandleW(primaryPath.c_str());

                if (primaryModule != NULL && primaryModule != module)
                {
                    logger::info("DllMain", "Secondary proxy detected - winhttp.dll is primary, skipping hooks");
                    DisableThreadLibraryCalls(module);
                    break;
                }

                // winhttp.dll not loaded, so we can be primary
                logger::info("DllMain", "winhttp.dll not loaded, this proxy will be primary");
            }
            else
            {
                logger::info("DllMain", "Running as primary proxy (winhttp.dll)");
            }

            DisableThreadLibraryCalls(module);
            logger::info("DllMain", "Calling Initialize()...");
            Initialize();
            break;
        }

        case DLL_THREAD_ATTACH:
            break;
        case DLL_THREAD_DETACH:
            break;
        case DLL_PROCESS_DETACH:
            logger::info("DllMain", "DLL_PROCESS_DETACH");
            break;
    }

    return TRUE;
}

static void InjectThisDll(HANDLE hProcess)
{
    HMODULE kernel32 = GetModuleHandleA("kernel32");
    auto pVirtualAllocEx = (decltype(&VirtualAllocEx))GetProcAddress(kernel32, "VirtualAllocEx");
    auto pWriteProcessMemory = (decltype(&WriteProcessMemory))GetProcAddress(kernel32, "WriteProcessMemory");
    auto pCreateRemoteThread = (decltype(&CreateRemoteThread))GetProcAddress(kernel32, "CreateRemoteThread");

    WCHAR thisDllPath[2048]{};
    GetModuleFileNameW((HMODULE)&__ImageBase, thisDllPath, _countof(thisDllPath));

    size_t pathSize = (wcslen(thisDllPath) + 1) * sizeof(WCHAR);
    LPVOID pathAddr = pVirtualAllocEx(hProcess, NULL, pathSize, MEM_COMMIT, PAGE_READWRITE);
    pWriteProcessMemory(hProcess, pathAddr, thisDllPath, pathSize, NULL);

    HANDLE loader = pCreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)&LoadLibraryW, pathAddr, 0, NULL);
    WaitForSingleObject(loader, INFINITE);
    CloseHandle(loader);
}

int APIENTRY _BootstrapEntry(HWND, HINSTANCE, LPWSTR commandLine, int)
{
    LONG (NTAPI *NtQueryInformationProcess)(HANDLE, DWORD, PVOID, ULONG, PULONG);
    LONG (NTAPI *NtRemoveProcessDebug)(HANDLE, HANDLE);
    LONG (NTAPI *NtClose)(HANDLE Handle);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    if (!CreateProcessW(NULL, commandLine, NULL, NULL, FALSE,
        CREATE_SUSPENDED | DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi))
    {
        char msg[128];
        sprintf_s(msg, "Failed to create LeagueClientUx process, last error: 0x%08X.", GetLastError());
        MessageBoxA(NULL, msg, "Pengu Loader bootstrapper", MB_ICONWARNING | MB_OK | MB_TOPMOST);
        return 1;
    }

    HMODULE ntdll = GetModuleHandleA("ntdll");
    (LPVOID &)NtQueryInformationProcess = GetProcAddress(ntdll, "NtQueryInformationProcess");
    (LPVOID &)NtRemoveProcessDebug = GetProcAddress(ntdll, "NtRemoveProcessDebug");
    (LPVOID &)NtClose = GetProcAddress(ntdll, "NtClose");

    HANDLE hDebug;
    if (NtQueryInformationProcess(pi.hProcess, 30, &hDebug, sizeof(HANDLE), 0) >= 0)
    {
        NtRemoveProcessDebug(pi.hProcess, hDebug);
        NtClose(hDebug);
    }

    InjectThisDll(pi.hProcess);
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}

#elif OS_MAC

__attribute__((constructor)) static void dllmain(int argc, const char **argv)
{
    std::string prog(argv[0]);
    prog = prog.substr(prog.rfind('/') + 1);

    if (prog == "LeagueClientUx")
    {
#if _DEBUG
        char msg[128];
        snprintf(msg, sizeof(msg)-1, "Debug me: %d", getpid());
        dialog::alert("Continue debugging...", msg);
#endif
        if (check_libcef_version(true))
        {
            HookBrowserProcess();
        }
    }
    else if (prog == "LeagueClientUx Helper (Renderer)")
    {
        if (check_libcef_version(false))
        {
            HookRendererProcess();
        }
    }
}

#endif

int _GetCefVersion()
{
    return CEF_VERSION_MAJOR;
}