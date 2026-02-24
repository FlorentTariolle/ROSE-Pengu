#ifdef _WIN32
#include <windows.h>

static FARPROC GetFunction(const char *name)
{
    static HMODULE module = nullptr;

    if (module == nullptr)
    {
        BOOL wow64 = FALSE;
        WCHAR path[MAX_PATH];

        if (IsWow64Process(GetCurrentProcess(), &wow64) && wow64)
            GetSystemWow64DirectoryW(path, MAX_PATH);
        else
            GetSystemDirectoryW(path, MAX_PATH);

        lstrcatW(path, L"\\d3d9.dll");
        module = LoadLibraryW(path);
    }

    return GetProcAddress(module, name);
}

template<typename T>
static T _Forward(const char* funcName, T)
{
    static T proc = nullptr;
    if (proc != nullptr) return proc;
    return proc = reinterpret_cast<T>(GetFunction(funcName));
}

#define Forward(F) _Forward(#F, F)

EXTERN_C LPVOID WINAPI Direct3DCreate9(UINT SDKVersion)
{
    return Forward(Direct3DCreate9)(SDKVersion);
}

EXTERN_C HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, LPVOID ppEx)
{
    return Forward(Direct3DCreate9Ex)(SDKVersion, ppEx);
}

EXTERN_C int WINAPI D3DPERF_BeginEvent(DWORD col, LPCWSTR wszName)
{
    return Forward(D3DPERF_BeginEvent)(col, wszName);
}

EXTERN_C int WINAPI D3DPERF_EndEvent()
{
    return Forward(D3DPERF_EndEvent)();
}

EXTERN_C DWORD WINAPI D3DPERF_GetStatus()
{
    return Forward(D3DPERF_GetStatus)();
}

EXTERN_C BOOL WINAPI D3DPERF_QueryRepeatFrame()
{
    return Forward(D3DPERF_QueryRepeatFrame)();
}

EXTERN_C void WINAPI D3DPERF_SetMarker(DWORD col, LPCWSTR wszName)
{
    Forward(D3DPERF_SetMarker)(col, wszName);
}

EXTERN_C int WINAPI D3DPERF_SetOptions(DWORD dwOptions)
{
    return Forward(D3DPERF_SetOptions)(dwOptions);
}

EXTERN_C void WINAPI D3DPERF_SetRegion(DWORD col, LPCWSTR wszName)
{
    Forward(D3DPERF_SetRegion)(col, wszName);
}

#endif
