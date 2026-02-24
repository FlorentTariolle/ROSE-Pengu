#include "pengu.h"
#include "hook.h"
#include "logger.h"
#include "include/cef_version.h"

// CefContext::GetBackgroundColor()
static cef_color_t get_background_color(void *rcx, cef_browser_settings_t *, cef_state_t)
{
    return 0; // SK_ColorTRANSPARENT
}

static void fix_browser_background(const void *rladdr)
{
#if OS_WIN
    const char *pattern = "41 83 F8 01 74 0B 41 83 F8 02 75 0A 45 31 C0";
#elif OS_MAC
    const char *pattern = "55 48 89 E5 83 FA 01 74 ?? 83 FA 02 75 ??";
#endif
    using Fn = decltype(&get_background_color);
    static hook::Hook<Fn> GetBackgroundColor;
    // Find CefContext::GetBackgroundColor()
    auto func = reinterpret_cast<Fn>(dylib::find_memory(rladdr, pattern));

    if (func != nullptr)
        GetBackgroundColor.hook(func, get_background_color);
}

bool check_libcef_version(bool is_browser)
{
    logger::infof("LibCef", "check_libcef_version called, is_browser=%d", is_browser);
    logger::infof("LibCef", "Looking for module: %s", LIBCEF_MODULE_NAME);

    void *libcef = dylib::find_lib(LIBCEF_MODULE_NAME);

    if (libcef != nullptr)
    {
        logger::infof("LibCef", "Found libcef at %p", libcef);

        auto get_version = reinterpret_cast<decltype(&cef_version_info)>(dylib::find_proc(libcef, "cef_version_info"));

        if (get_version == nullptr)
        {
            logger::error("LibCef", "cef_version_info function not found!");
            if (is_browser)
                dialog::alert("Pengu does not support your Client version.", "Pengu Loader");
            return false;
        }

        int client_cef_version = get_version(0);
        logger::infof("LibCef", "Client CEF version: %d, Expected: %d", client_cef_version, CEF_VERSION_MAJOR);

        // Check CEF version
        if (client_cef_version != CEF_VERSION_MAJOR)
        {
            logger::errorf("LibCef", "CEF VERSION MISMATCH! Client=%d, Expected=%d", client_cef_version, CEF_VERSION_MAJOR);
            if (is_browser)
                dialog::alert("Pengu does not support your Client version.", "Pengu Loader");
            return false;
        }

        logger::info("LibCef", "CEF version check PASSED");

        if (is_browser)
        {
            logger::info("LibCef", "Applying browser background fix...");
            fix_browser_background((const void *)get_version);
        }

        return true;
    }
    else
    {
        logger::error("LibCef", "libcef module NOT FOUND!");
        if (is_browser)
            dialog::alert("Failed to load Chromium Embedded Framework.", "Pengu Loader");
        return false;
    }
}