#include "settings.h"

#include <windows.h>
#include <algorithm>

namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\mote";
constexpr wchar_t kToolbarCollapsedValue[] = L"ToolbarCollapsed";
constexpr wchar_t kRecentFilesValue[] = L"RecentFiles";

bool SamePath(const std::wstring& a, const std::wstring& b)
{
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

// REG_MULTI_SZ 형식: 문자열마다 NUL로 끝나고 마지막에 NUL이 하나 더 붙는다
void SaveRecentFiles(const std::vector<std::wstring>& files)
{
    std::wstring data;
    for (const std::wstring& file : files) {
        data += file;
        data += L'\0';
    }
    data += L'\0';
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kRecentFilesValue, REG_MULTI_SZ, data.data(),
                    static_cast<DWORD>(data.size() * sizeof(wchar_t)));
}

}  // namespace

bool LoadToolbarCollapsed()
{
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kToolbarCollapsedValue, RRF_RT_REG_DWORD,
                        nullptr, &value, &size) == ERROR_SUCCESS && value != 0;
}

void SaveToolbarCollapsed(bool collapsed)
{
    const DWORD value = collapsed ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kToolbarCollapsedValue, REG_DWORD, &value, sizeof(value));
}

std::vector<std::wstring> LoadRecentFiles()
{
    std::vector<std::wstring> files;
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kRecentFilesValue, RRF_RT_REG_MULTI_SZ,
                     nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0) {
        return files;
    }
    std::wstring data(size / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kRecentFilesValue, RRF_RT_REG_MULTI_SZ,
                     nullptr, data.data(), &size) != ERROR_SUCCESS) {
        return files;
    }
    for (const wchar_t* p = data.c_str(); *p && files.size() < kMaxRecentFiles; p += wcslen(p) + 1) {
        files.emplace_back(p);
    }
    return files;
}

void AddRecentFile(const std::wstring& path)
{
    if (path.empty()) return;
    std::vector<std::wstring> files = LoadRecentFiles();
    std::erase_if(files, [&](const std::wstring& file) { return SamePath(file, path); });
    files.insert(files.begin(), path);
    if (files.size() > kMaxRecentFiles) files.resize(kMaxRecentFiles);
    SaveRecentFiles(files);
}

void RemoveRecentFile(const std::wstring& path)
{
    std::vector<std::wstring> files = LoadRecentFiles();
    if (std::erase_if(files, [&](const std::wstring& file) { return SamePath(file, path); }) > 0) {
        SaveRecentFiles(files);
    }
}

void ClearRecentFiles()
{
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kRecentFilesValue);
}
