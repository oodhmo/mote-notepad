#include "registration.h"
#include "strings.h"

#include <windows.h>
#include <shlobj.h>
#include <string>

namespace {

constexpr wchar_t kClasses[] = L"Software\\Classes\\";
constexpr wchar_t kCapabilitiesKey[] = L"Software\\mote\\Capabilities";
constexpr wchar_t kRegisteredApplicationsKey[] = L"Software\\RegisteredApplications";
constexpr wchar_t kAppName[] = L"mote";

struct FileType {
    const wchar_t* extension;
    const wchar_t* progId;
    StrId typeName;
};

constexpr FileType kFileTypes[] = {
    {L".txt", L"mote.txt", StrId::OpenWithTextType},
    {L".rtf", L"mote.rtf", StrId::OpenWithRichType},
};

std::wstring ExePath()
{
    wchar_t path[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return (length == 0 || length >= std::size(path)) ? std::wstring() : std::wstring(path, length);
}

std::wstring OpenCommand(const std::wstring& exe) { return L"\"" + exe + L"\" \"%1\""; }
std::wstring IconPath(const std::wstring& exe) { return L"\"" + exe + L"\",0"; }

bool SetString(const std::wstring& key, const wchar_t* name, const std::wstring& value)
{
    return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(),
                           static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

// 실행 명령과 아이콘 (ProgID와 Applications\mote.exe에 같은 모양으로 쓴다)
bool SetShellOpen(const std::wstring& key, const std::wstring& exe)
{
    return SetString(key + L"\\DefaultIcon", nullptr, IconPath(exe)) &&
           SetString(key + L"\\shell\\open\\command", nullptr, OpenCommand(exe));
}

std::wstring ReadString(const std::wstring& key, const wchar_t* name)
{
    wchar_t buffer[MAX_PATH * 4 + 16] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return {};
    return buffer;
}

bool WriteAll(const std::wstring& exe)
{
    const std::wstring classes = kClasses;
    bool ok = true;
    for (const FileType& type : kFileTypes) {
        const std::wstring progId = classes + type.progId;
        ok = ok && SetString(progId, nullptr, Str(type.typeName)) && SetShellOpen(progId, exe);
        // 연결 프로그램 목록에 바로 보이게 한다 (값 이름이 ProgID, 데이터는 비워 둔다)
        ok = ok && RegSetKeyValueW(HKEY_CURRENT_USER, (classes + type.extension + L"\\OpenWithProgids").c_str(),
                                   type.progId, REG_NONE, nullptr, 0) == ERROR_SUCCESS;
        ok = ok && SetString(std::wstring(kCapabilitiesKey) + L"\\FileAssociations", type.extension, type.progId);
    }

    const std::wstring application = classes + L"Applications\\mote.exe";
    ok = ok && SetString(application, L"FriendlyAppName", kAppName) && SetShellOpen(application, exe);
    for (const FileType& type : kFileTypes) {
        ok = ok && RegSetKeyValueW(HKEY_CURRENT_USER, (application + L"\\SupportedTypes").c_str(), type.extension,
                                   REG_SZ, L"", sizeof(wchar_t)) == ERROR_SUCCESS;
    }

    // Windows 설정 > 기본 앱 목록에 나오게 한다
    ok = ok && SetString(kCapabilitiesKey, L"ApplicationName", kAppName) &&
         SetString(kCapabilitiesKey, L"ApplicationDescription", Str(StrId::AboutDescription)) &&
         SetString(kRegisteredApplicationsKey, kAppName, kCapabilitiesKey);
    return ok;
}

// 값도 하위 키도 없는 키만 지운다 (.txt처럼 다른 앱과 함께 쓰는 키를 비워 두지 않으려고)
void DeleteKeyIfEmpty(const std::wstring& key)
{
    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &handle) != ERROR_SUCCESS) return;
    DWORD subkeys = 0, values = 0;
    const bool empty = RegQueryInfoKeyW(handle, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values,
                                        nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS && subkeys == 0 && values == 0;
    RegCloseKey(handle);
    if (empty) RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
}

// 탐색기가 연결 정보를 다시 읽게 한다
void NotifyShell()
{
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

}  // namespace

bool IsOpenWithRegistered()
{
    return RegGetValueW(HKEY_CURRENT_USER, kRegisteredApplicationsKey, kAppName, RRF_RT_REG_SZ,
                        nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

bool RegisterOpenWith()
{
    const std::wstring exe = ExePath();
    if (exe.empty()) return false;
    if (!WriteAll(exe)) {
        UnregisterOpenWith();
        return false;
    }
    NotifyShell();
    return true;
}

void UnregisterOpenWith()
{
    const std::wstring classes = kClasses;
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kRegisteredApplicationsKey, kAppName);
    RegDeleteTreeW(HKEY_CURRENT_USER, kCapabilitiesKey);
    RegDeleteKeyW(HKEY_CURRENT_USER, kCapabilitiesKey);
    for (const FileType& type : kFileTypes) {
        const std::wstring progId = classes + type.progId;
        RegDeleteTreeW(HKEY_CURRENT_USER, progId.c_str());
        RegDeleteKeyW(HKEY_CURRENT_USER, progId.c_str());
        const std::wstring openWith = classes + type.extension + L"\\OpenWithProgids";
        RegDeleteKeyValueW(HKEY_CURRENT_USER, openWith.c_str(), type.progId);
        DeleteKeyIfEmpty(openWith);
        DeleteKeyIfEmpty(classes + type.extension);
    }
    const std::wstring application = classes + L"Applications\\mote.exe";
    RegDeleteTreeW(HKEY_CURRENT_USER, application.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, application.c_str());
    NotifyShell();
}

void RefreshOpenWithPath()
{
    if (!IsOpenWithRegistered()) return;
    const std::wstring exe = ExePath();
    if (exe.empty()) return;
    const std::wstring current = ReadString(std::wstring(kClasses) + kFileTypes[0].progId + L"\\shell\\open\\command", nullptr);
    if (CompareStringOrdinal(current.c_str(), -1, OpenCommand(exe).c_str(), -1, TRUE) == CSTR_EQUAL) return;
    if (WriteAll(exe)) NotifyShell();
}
