// tesmio_plugin.h - the boilerplate every plugin would otherwise repeat.
//
// Include this instead of tesmio_api.h. It binds the host table to file-scope
// names with the same shapes the loader itself uses - Logf, FindIatSlot,
// InstallInlineHook, ReadablePtr, FaultFilter - so code moved out of
// tesmioloader.cpp compiles unchanged, and so a new plugin reads like the
// loader rather than like a foreign thing bolted to it.
//
//   #include "../../src/tesmio_plugin.h"
//
//   extern "C" __declspec(dllexport) unsigned TsmPluginApiVersion(void) { return TSM_API_VERSION; }
//   extern "C" __declspec(dllexport) int TsmPluginInit(const TsmHost* host, TsmPluginInfo* info)
//   {
//       TsmBind(host);
//       ...
//   }
//
// Header-only and every function static, so each plugin gets its own copy. That
// is the point: nothing is shared across the DLL boundary but PODs.

#ifndef TESMIO_PLUGIN_H
#define TESMIO_PLUGIN_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <intrin.h>

#include "tesmio_api.h"

// C4505, "unreferenced function with internal linkage has been removed", is the
// expected and correct outcome for everything below: this is a menu, and no
// plugin orders the whole thing. Left alone it produces five to nine warnings
// per plugin at /W4 - about seventy across the tree - which is enough noise to
// hide a real one, and a real one did hide in it. Suppressed here and restored
// at the bottom of the file, so a plugin's own dead code still gets reported.
#pragma warning(push)
#pragma warning(disable: 4505)

#define DLL_ENGINE "C3DDLL64.dll"
#define DLL_STDIO  "api-ms-win-crt-stdio-l1-1-0.dll"

static const TsmHost*   H;
static HMODULE          g_exe;
static BYTE*            g_exeBase;
static SIZE_T           g_exeSize;
static HMODULE          g_engine;
static const char*      g_baseDir;
static const char*      g_vfsRoot;   // v4+; null against an older host - see TsmBind
static CRITICAL_SECTION g_lock;

// Logf(...) rather than H->log(...), because that is what several hundred lines
// of moved code already say.
#define Logf (H->log)

static void TsmBind(const TsmHost* host)
{
    H         = host;
    g_exe     = (HMODULE)host->exeModule;
    g_exeBase = host->exeBase;
    g_exeSize = host->exeSize;
    g_engine  = (HMODULE)host->engineModule;
    g_baseDir = host->baseDir;

    // vfsRoot is appended after `consume`, so structSize is what tells an old
    // host's smaller TsmHost apart from one that actually has this field.
    if (host->structSize >= offsetof(TsmHost, vfsRoot) + sizeof(host->vfsRoot))
        g_vfsRoot = host->vfsRoot;

    InitializeCriticalSection(&g_lock);
}

static void** FindIatSlot(HMODULE mod, const char* dll, const char* fn)
{
    return H->findIatSlot((void*)mod, dll, fn);
}

static bool PatchIat(HMODULE mod, const char* dll, const char* fn,
                     void* detour, void** original, const char* label)
{
    return H->patchIat((void*)mod, dll, fn, detour, original, label) != 0;
}

static bool InstallInlineHook(void* target, void* detour, void** trampoline,
                              const BYTE* expect, size_t stolen, const char* label)
{
    return H->installInlineHook(target, detour, trampoline, expect, stolen, label) != 0;
}

static BYTE* AllocNear(BYTE* anchor, SIZE_T size)
{
    return H->allocNear(anchor, size);
}

static bool ReadablePtr(const void* p, size_t n)
{
    return H->readablePtr(p, n) != 0;
}

static LONG FaultFilter(const char* what, PEXCEPTION_POINTERS ep)
{
    return H->faultFilter(what, (void*)ep);
}

static void Trim(char* s)
{
    char* p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    for (char* e = s + strlen(s); e > s; e--)
    {
        char c = e[-1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        e[-1] = 0;
    }
}

// Reads a NUL-terminated plain-ASCII name out of a pointer that came from a
// game structure rather than from a call we made. Refuses anything with a byte
// outside 32..126, which is what tells a real name from a stale pointer.
static bool SafeReadStr(const void* p, char* out, size_t n)
{
    // Before `out[0] = 0`, not after: with n == 0 that store is already out of
    // bounds, and `n - 1` below then underflows to SIZE_MAX and makes the copy
    // limit the whole region.
    if (!out || n == 0) return false;
    out[0] = 0;
    if (!p) return false;

    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;

    const char* s = (const char*)p;
    size_t room = (size_t)(((BYTE*)mbi.BaseAddress + mbi.RegionSize) - (BYTE*)p);
    size_t lim  = (room < n - 1) ? room : n - 1;

    size_t i = 0;
    for (; i < lim; i++)
    {
        char c = s[i];
        if (c == 0) break;
        if ((unsigned char)c < 32 || (unsigned char)c > 126) return false;
        out[i] = c;
    }
    out[i] = 0;
    return i > 0;
}

// A log file of the plugin's own, next to the loader's. The game holds these
// open, so reading one while it runs needs FileShare.ReadWrite.
static HANDLE TsmOpenLog(const char* name)
{
    char p[MAX_PATH];
    _snprintf_s(p, sizeof(p), _TRUNCATE, "%s\\%s", g_baseDir, name);
    return CreateFileA(p, GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

static void TsmWrite(HANDLE h, const char* s, int len)
{
    // NULL as well as INVALID_HANDLE_VALUE: CreateFile hands back the latter,
    // but a handle field that was never assigned is the former, and WriteFile
    // on it fails silently rather than loudly.
    if (h == NULL || h == INVALID_HANDLE_VALUE) return;

    // A NEGATIVE LENGTH IS THE DANGEROUS ONE, and it is easy to produce: every
    // caller gets `len` from _snprintf_s, which returns -1 rather than a length
    // when _TRUNCATE truncates. (DWORD)(-1) is four gigabytes, and WriteFile
    // would happily walk off the end of a 256-byte stack buffer trying to
    // satisfy it. Refused here, at the one place they all go through, rather
    // than at each call site where the next one added would miss it.
    if (!s || len <= 0) return;

    DWORD wrote = 0;
    WriteFile(h, s, (DWORD)len, &wrote, NULL);
}

#pragma warning(pop)        // 4505 - see the push at the top of this file

#endif // TESMIO_PLUGIN_H
