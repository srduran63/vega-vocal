// Self-contained installer: the plugin travels inside this .exe as resources, so it keeps working
// even when someone double-clicks it from inside the zip without extracting anything else.
// Options, used by the tests: /S installs silently, /U uninstalls silently, /D=<folder> replaces
// the VST3 folder.
#include <windows.h>
#include <shlobj.h>

#include <string>

#include "setup_ids.h"

namespace
{
const wchar_t* const kTitle = L"Vega Vocal " VEGA_VERSION_W;

enum class Result { ok, locked, failed };

std::wstring systemVst3Folder()
{
    PWSTR common = nullptr;
    std::wstring path;
    if (SUCCEEDED (SHGetKnownFolderPath (FOLDERID_ProgramFilesCommon, 0, nullptr, &common)))
        path = std::wstring (common) + L"\\VST3";
    CoTaskMemFree (common);
    return path;
}

Result writeResource (int id, const std::wstring& target)
{
    HRSRC info = FindResourceW (nullptr, MAKEINTRESOURCEW (id), RT_RCDATA);
    HGLOBAL loaded = info != nullptr ? LoadResource (nullptr, info) : nullptr;
    const void* data = loaded != nullptr ? LockResource (loaded) : nullptr;
    const DWORD size = info != nullptr ? SizeofResource (nullptr, info) : 0;
    if (data == nullptr || size == 0)
        return Result::failed;

    // Written beside the target first, so a failure never leaves a half-copied plugin behind.
    const std::wstring temporary = target + L".new";
    HANDLE file = CreateFileW (temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return Result::failed;
    DWORD written = 0;
    const BOOL ok = WriteFile (file, data, size, &written, nullptr) && written == size;
    CloseHandle (file);
    if (! ok)
    {
        DeleteFileW (temporary.c_str());
        return Result::failed;
    }
    if (! MoveFileExW (temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        const DWORD error = GetLastError();
        DeleteFileW (temporary.c_str());
        return error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED ? Result::locked : Result::failed;
    }
    return Result::ok;
}

bool makeFolder (const std::wstring& path)
{
    const int code = SHCreateDirectoryExW (nullptr, path.c_str(), nullptr);
    return code == ERROR_SUCCESS || code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS;
}

Result install (const std::wstring& bundle)
{
    const std::wstring binaryFolder = bundle + L"\\Contents\\x86_64-win";
    const std::wstring resourceFolder = bundle + L"\\Contents\\Resources";
    if (! makeFolder (binaryFolder) || ! makeFolder (resourceFolder))
        return Result::failed;
    const Result binary = writeResource (IDR_PLUGIN, binaryFolder + L"\\Vega Vocal.vst3");
    if (binary != Result::ok)
        return binary;
    return writeResource (IDR_MODULEINFO, resourceFolder + L"\\moduleinfo.json");
}

// Removes only what install() created, plus folders it leaves empty.
Result uninstall (const std::wstring& bundle)
{
    const std::wstring binary = bundle + L"\\Contents\\x86_64-win\\Vega Vocal.vst3";
    if (! DeleteFileW (binary.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
        return Result::locked;
    DeleteFileW ((bundle + L"\\Contents\\Resources\\moduleinfo.json").c_str());
    RemoveDirectoryW ((bundle + L"\\Contents\\x86_64-win").c_str());
    RemoveDirectoryW ((bundle + L"\\Contents\\Resources").c_str());
    RemoveDirectoryW ((bundle + L"\\Contents").c_str());
    RemoveDirectoryW (bundle.c_str());
    return Result::ok;
}

void showFailure (Result result)
{
    MessageBoxW (nullptr,
                 result == Result::locked
                     ? L"Vega Vocal está en uso.\n\nCierra FL Studio o tu programa de música y vuelve a intentarlo."
                     : L"No se pudo completar la operación.\n\nReinicia el PC y vuelve a intentarlo. Si sigue fallando, revisa si tu antivirus bloqueó el instalador.",
                 kTitle, MB_OK | MB_ICONERROR);
}
} // namespace

int WINAPI wWinMain (HINSTANCE, HINSTANCE, PWSTR, int)
{
    bool silentInstall = false, silentUninstall = false;
    std::wstring folder;

    int count = 0;
    if (LPWSTR* args = CommandLineToArgvW (GetCommandLineW(), &count))
    {
        for (int i = 1; i < count; ++i)
        {
            const std::wstring arg (args[i]);
            if (arg == L"/S") silentInstall = true;
            else if (arg == L"/U") silentUninstall = true;
            else if (arg.rfind (L"/D=", 0) == 0) folder = arg.substr (3);
        }
        LocalFree (args);
    }

    if (folder.empty())
        folder = systemVst3Folder();
    if (folder.empty())
    {
        if (! silentInstall && ! silentUninstall)
            showFailure (Result::failed);
        return 1;
    }
    const std::wstring bundle = folder + L"\\Vega Vocal.vst3";

    if (silentInstall)
        return install (bundle) == Result::ok ? 0 : 1;
    if (silentUninstall)
        return uninstall (bundle) == Result::ok ? 0 : 1;

    const bool installed = GetFileAttributesW ((bundle + L"\\Contents\\x86_64-win\\Vega Vocal.vst3").c_str()) != INVALID_FILE_ATTRIBUTES;
    bool remove = false;
    if (installed)
    {
        const int choice = MessageBoxW (nullptr,
                                        L"Vega Vocal ya está instalado.\n\nSí: instalar de nuevo o actualizar\nNo: desinstalarlo\nCancelar: salir",
                                        kTitle, MB_YESNOCANCEL | MB_ICONQUESTION);
        if (choice == IDCANCEL)
            return 0;
        remove = choice == IDNO;
    }
    else if (MessageBoxW (nullptr, L"¿Instalar el plugin de voz Vega Vocal (VST3) en este PC?", kTitle, MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
    {
        return 0;
    }

    const Result result = remove ? uninstall (bundle) : install (bundle);
    if (result != Result::ok)
    {
        showFailure (result);
        return 1;
    }

    MessageBoxW (nullptr,
                 remove ? L"Vega Vocal se desinstaló."
                        : L"¡Listo! Vega Vocal está instalado.\n\nEn FL Studio: Options > Manage plugins > Find more plugins.\nEn otros programas, busca nuevos plugins VST3.",
                 kTitle, MB_OK | MB_ICONINFORMATION);
    return 0;
}
