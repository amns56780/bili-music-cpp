// rescopy: copy icon + version-info resources from one PE file to another.
//
// Used by scripts\package.ps1 to give the installer (official 7zSD.sfx + payload)
// the same icon and version info as bilimusic.exe, without rebuilding the
// third-party SFX module.
//
//   rescopy <source.exe> <target.exe>
//
// Existing icon/version resources in the target are removed first (all languages),
// then the source resources are copied with their original languages.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

struct ResName {
    bool isId = false;
    WORD id = 0;
    std::wstring text;
};

struct TargetKey {
    LPCWSTR type = nullptr;
    ResName name;
    WORD language = 0;
};

struct Ctx {
    HMODULE source = nullptr;
    HANDLE update = nullptr;
    bool ok = true;
    std::vector<TargetKey> remove;
};

ResName makeResName(LPCWSTR name) {
    ResName out;
    if (IS_INTRESOURCE(name)) {
        out.isId = true;
        out.id = (WORD)(ULONG_PTR)name;
    } else {
        out.text = name;
    }
    return out;
}

LPCWSTR resNamePtr(const ResName& name) {
    return name.isId ? MAKEINTRESOURCEW(name.id) : name.text.c_str();
}

BOOL CALLBACK collectTargetLang(HMODULE, LPCWSTR type, LPCWSTR name, WORD language, LONG_PTR raw) {
    Ctx* ctx = reinterpret_cast<Ctx*>(raw);
    ctx->remove.push_back(TargetKey{type, makeResName(name), language});
    return TRUE;
}

BOOL CALLBACK collectTargetNames(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR raw) {
    EnumResourceLanguagesW(module, type, name, collectTargetLang, raw);
    return TRUE;
}

BOOL CALLBACK copySourceLang(HMODULE module, LPCWSTR type, LPCWSTR name, WORD language, LONG_PTR raw) {
    Ctx* ctx = reinterpret_cast<Ctx*>(raw);
    HRSRC resource = FindResourceExW(module, type, name, language);
    if (resource == nullptr) {
        ctx->ok = false;
        return TRUE;
    }
    const DWORD size = SizeofResource(module, resource);
    HGLOBAL loaded = LoadResource(module, resource);
    void* data = loaded ? LockResource(loaded) : nullptr;
    if (data == nullptr) {
        ctx->ok = false;
        return TRUE;
    }
    if (UpdateResourceW(ctx->update, type, name, language, data, size) == FALSE) {
        ctx->ok = false;
    }
    return TRUE;
}

BOOL CALLBACK copySourceNames(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR raw) {
    EnumResourceLanguagesW(module, type, name, copySourceLang, raw);
    return TRUE;
}

void deleteTarget(Ctx* ctx) {
    for (const TargetKey& key : ctx->remove) {
        UpdateResourceW(ctx->update, key.type, resNamePtr(key.name), key.language, nullptr, 0);
    }
}

bool copyResources(Ctx* ctx, LPCWSTR type) {
    return EnumResourceNamesW(ctx->source, type, copySourceNames, (LONG_PTR)ctx) != FALSE;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::fwprintf(stderr, L"usage: rescopy <source.exe> <target.exe>\n");
        return 2;
    }
    const std::wstring sourcePath = argv[1];
    const std::wstring targetPath = argv[2];

    Ctx ctx;
    HMODULE targetModule = LoadLibraryExW(targetPath.c_str(), nullptr,
                                          LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (targetModule == nullptr) {
        std::fwprintf(stderr, L"cannot open target: %s (error %lu)\n", targetPath.c_str(), GetLastError());
        return 1;
    }
    EnumResourceNamesW(targetModule, RT_ICON, collectTargetNames, (LONG_PTR)&ctx);
    EnumResourceNamesW(targetModule, RT_GROUP_ICON, collectTargetNames, (LONG_PTR)&ctx);
    EnumResourceNamesW(targetModule, RT_VERSION, collectTargetNames, (LONG_PTR)&ctx);
    FreeLibrary(targetModule);

    ctx.source = LoadLibraryExW(sourcePath.c_str(), nullptr,
                                LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (ctx.source == nullptr) {
        std::fwprintf(stderr, L"cannot open source: %s (error %lu)\n", sourcePath.c_str(), GetLastError());
        return 1;
    }

    ctx.update = BeginUpdateResourceW(targetPath.c_str(), FALSE);
    if (ctx.update == nullptr) {
        std::fwprintf(stderr, L"BeginUpdateResource failed: %s (error %lu)\n", targetPath.c_str(), GetLastError());
        FreeLibrary(ctx.source);
        return 1;
    }

    deleteTarget(&ctx);
    copyResources(&ctx, RT_ICON);
    copyResources(&ctx, RT_GROUP_ICON);
    copyResources(&ctx, RT_VERSION);

    const BOOL committed = EndUpdateResourceW(ctx.update, FALSE);
    FreeLibrary(ctx.source);
    if (committed == FALSE || !ctx.ok) {
        std::fwprintf(stderr, L"resource update failed (error %lu)\n", GetLastError());
        return 1;
    }
    std::printf("resources copied: %ls -> %ls\n", sourcePath.c_str(), targetPath.c_str());
    return 0;
}
