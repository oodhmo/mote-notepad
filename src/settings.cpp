#include "settings.h"

#include <windows.h>
#include <algorithm>

namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\mote";
constexpr wchar_t kToolbarCollapsedValue[] = L"ToolbarCollapsed";
constexpr wchar_t kRecentFilesValue[] = L"RecentFiles";
constexpr wchar_t kWindowPlacementValue[] = L"WindowPlacement";
constexpr wchar_t kColorShortcutsValue[] = L"ColorShortcuts";
constexpr wchar_t kOpenWithAskedValue[] = L"OpenWithAsked";
constexpr wchar_t kRecentColorsValue[] = L"RecentColors";
constexpr DWORD kCustomColorFlag = 0x01000000;  // 색 단축키 저장값에서 사용자 지정 색을 나타내는 비트

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

bool LoadOpenWithAsked()
{
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kOpenWithAskedValue, RRF_RT_REG_DWORD,
                        nullptr, &value, &size) == ERROR_SUCCESS && value != 0;
}

void SaveOpenWithAsked()
{
    const DWORD value = 1;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kOpenWithAskedValue, REG_DWORD, &value, sizeof(value));
}

bool LoadWindowPlacement(WINDOWPLACEMENT& placement)
{
    WINDOWPLACEMENT value = {};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kWindowPlacementValue, RRF_RT_REG_BINARY,
                     nullptr, &value, &size) != ERROR_SUCCESS) {
        return false;
    }
    if (size != sizeof(value) || value.length != sizeof(value)) return false;
    const RECT& rc = value.rcNormalPosition;
    if (rc.right <= rc.left || rc.bottom <= rc.top) return false;
    placement = value;
    return true;
}

void SaveWindowPlacement(const WINDOWPLACEMENT& placement)
{
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kWindowPlacementValue, REG_BINARY, &placement, sizeof(placement));
}

// 저장 형식: 슬롯마다 DWORD 하나. 팔레트 번호이거나, 사용자 지정 색이면 kCustomColorFlag | RGB.
// (이전 버전은 슬롯마다 팔레트 번호 BYTE 하나였으므로 그 형식도 읽는다)
ColorShortcuts LoadColorShortcuts(int paletteCount)
{
    ColorShortcuts shortcuts = {{{2}, {6}, {5}}};  // 빨강, 파랑, 초록 (topbar.cpp의 kPalette 순서)
    DWORD value[kColorShortcutCount] = {};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kColorShortcutsValue, RRF_RT_REG_BINARY,
                     nullptr, value, &size) != ERROR_SUCCESS) {
        return shortcuts;
    }
    if (size == sizeof(value)) {
        for (int i = 0; i < kColorShortcutCount; ++i) {
            if ((value[i] & 0xFF000000) == kCustomColorFlag) {
                shortcuts[i] = {-1, static_cast<COLORREF>(value[i] & 0x00FFFFFF)};
            } else if (value[i] < static_cast<DWORD>(paletteCount)) {
                shortcuts[i] = {static_cast<int>(value[i])};
            }
        }
    } else if (size == kColorShortcutCount) {
        const auto* bytes = reinterpret_cast<const BYTE*>(value);
        for (int i = 0; i < kColorShortcutCount; ++i) {
            if (bytes[i] < paletteCount) shortcuts[i] = {bytes[i]};
        }
    }
    return shortcuts;
}

void SaveColorShortcuts(const ColorShortcuts& shortcuts)
{
    DWORD value[kColorShortcutCount] = {};
    for (int i = 0; i < kColorShortcutCount; ++i) {
        const ColorShortcut& shortcut = shortcuts[i];
        value[i] = shortcut.IsCustom() ? (kCustomColorFlag | (shortcut.custom & 0x00FFFFFF))
                                       : static_cast<DWORD>(shortcut.palette);
    }
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kColorShortcutsValue, REG_BINARY, value, sizeof(value));
}

// 저장 형식: 색마다 COLORREF(DWORD) 하나
std::vector<COLORREF> LoadRecentColors()
{
    COLORREF value[kMaxRecentColors] = {};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kRecentColorsValue, RRF_RT_REG_BINARY,
                     nullptr, value, &size) != ERROR_SUCCESS) {
        return {};
    }
    std::vector<COLORREF> colors;
    for (size_t i = 0; i < size / sizeof(COLORREF); ++i) colors.push_back(value[i] & 0x00FFFFFF);
    return colors;
}

void SaveRecentColors(const std::vector<COLORREF>& colors)
{
    const size_t count = std::min(colors.size(), kMaxRecentColors);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kRecentColorsValue, REG_BINARY, colors.data(),
                    static_cast<DWORD>(count * sizeof(COLORREF)));
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
