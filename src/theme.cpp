#include "theme.h"

#include "strings.h"

#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

namespace {

// Light 모드 색 (디자인 요구사항의 Notion 계열 톤)
constexpr ThemeColors kLight = {
    RGB(0xFF, 0xFF, 0xFF),  // windowBg
    RGB(0xF7, 0xF7, 0xF5),  // secondaryBg
    RGB(0x37, 0x35, 0x2F),  // text
    RGB(0x78, 0x77, 0x74),  // secondaryText
    RGB(0xE9, 0xE9, 0xE7),  // border
    RGB(0xEF, 0xEF, 0xEE),  // hover
    RGB(0xE6, 0xE6, 0xE4),  // pressed
    RGB(0x37, 0x35, 0x2F),  // primaryBg
    RGB(0x4A, 0x48, 0x42),  // primaryHover
    RGB(0x2A, 0x28, 0x24),  // primaryPressed
    RGB(0xFF, 0xFF, 0xFF),  // primaryText
    RGB(0xD4, 0x4C, 0x47),  // notFound
};

// Dark 모드 색 (Notion 다크 톤: 순수 검정 대신 아주 어두운 회색, 글자는 살짝 낮춘 흰색)
constexpr ThemeColors kDark = {
    RGB(0x19, 0x19, 0x19),  // windowBg
    RGB(0x25, 0x25, 0x25),  // secondaryBg
    RGB(0xD4, 0xD4, 0xD2),  // text
    RGB(0x9B, 0x9B, 0x99),  // secondaryText
    RGB(0x2F, 0x2F, 0x2F),  // border
    RGB(0x2C, 0x2C, 0x2C),  // hover
    RGB(0x37, 0x37, 0x37),  // pressed
    RGB(0xE6, 0xE6, 0xE4),  // primaryBg
    RGB(0xFF, 0xFF, 0xFF),  // primaryHover
    RGB(0xC8, 0xC8, 0xC6),  // primaryPressed
    RGB(0x19, 0x19, 0x19),  // primaryText
    RGB(0xE5, 0x6F, 0x6A),  // notFound
};

bool g_dark = false;

// Windows 설정 > 개인 설정 > 색 > "기본 앱 모드"
bool SystemUsesDarkApps()
{
    DWORD value = 1, size = sizeof(value);
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return status == ERROR_SUCCESS && value == 0;
}

// uxtheme.dll의 문서화되지 않은 서수 함수 (Windows 10 1903 이상). 팝업 메뉴를 다크로 그리는 데만 쓴다.
enum class PreferredAppMode { Default, AllowDark, ForceDark, ForceLight };
using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using FlushMenuThemesFn = void(WINAPI*)();

int CALLBACK FontExistsProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found)
{
    *reinterpret_cast<bool*>(found) = true;
    return 0;  // 하나만 찾으면 충분하다
}

bool FontExists(const wchar_t* face)
{
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &lf, FontExistsProc, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

// 메뉴/버튼 같은 UI 글자의 기본 글꼴. 한국어 화면이면 문구가 모두 한글이라 맑은 고딕으로 그린다.
const wchar_t* DefaultUiTextFace()
{
    return IsKoreanUi() ? HangulUiFontFace() : UiFontFace();
}

}  // namespace

const ThemeColors& CurrentTheme()
{
    return g_dark ? kDark : kLight;
}

bool IsDarkTheme()
{
    return g_dark;
}

bool UpdateThemeFromSystem()
{
    const bool dark = SystemUsesDarkApps();
    if (dark == g_dark) return false;
    g_dark = dark;
    return true;
}

void ApplyMenuTheme()
{
    static HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme) return;
    static auto setMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    static auto flushMenus = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    if (!setMode || !flushMenus) return;
    setMode(g_dark ? PreferredAppMode::ForceDark : PreferredAppMode::ForceLight);
    flushMenus();
}

void ApplyTitleBarTheme(HWND window)
{
    const ThemeColors& theme = CurrentTheme();
    const BOOL dark = g_dark;
    // 다크 모드 제목 표시줄(Windows 10 2004 이상). 최소화/최대화/닫기 버튼 색도 함께 바뀐다.
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    // Windows 11: 제목 표시줄을 본문과 같은 색으로 칠해 상단 바와 하나로 이어 보이게 한다
    DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &theme.windowBg, sizeof(theme.windowBg));
    DwmSetWindowAttribute(window, DWMWA_TEXT_COLOR, &theme.text, sizeof(theme.text));
}

void ApplyControlTheme(HWND control)
{
    // "DarkMode_Explorer"는 탐색기가 쓰는 다크 비주얼 스타일 (스크롤바, 툴팁)
    SetWindowTheme(control, g_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

const wchar_t* UiFontFace()
{
    // Windows 11 기본 UI 글꼴. Windows 10에는 없으므로 한 번만 확인해 둔다.
    static const wchar_t* face = FontExists(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    return face;
}

const wchar_t* IconFontFace()
{
    // 두 글꼴은 굵게/기울임/밑줄 아이콘의 코드포인트가 같다
    static const wchar_t* face = FontExists(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    return face;
}

HFONT CreateUiFont(int pointTenths, UINT dpi, int weight, const wchar_t* face)
{
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(pointTenths, static_cast<int>(dpi), 720);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, face ? face : DefaultUiTextFace(), _TRUNCATE);
    return CreateFontIndirectW(&lf);
}

HFONT CreateUiFontPx(int pixelSize, UINT dpi, int weight, const wchar_t* face)
{
    LOGFONTW lf = {};
    lf.lfHeight = -Scale(pixelSize, dpi);  // 음수: 줄 간격이 아니라 글자 자체의 높이
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, face ? face : DefaultUiTextFace(), _TRUNCATE);
    return CreateFontIndirectW(&lf);
}

const wchar_t* HangulUiFontFace()
{
    static const wchar_t* face = FontExists(L"Malgun Gothic") ? L"Malgun Gothic" : UiFontFace();
    return face;
}

void FillRoundRect(Gdiplus::Graphics& graphics, const RECT& rect, COLORREF color, int radius)
{
    const auto left = static_cast<Gdiplus::REAL>(rect.left);
    const auto top = static_cast<Gdiplus::REAL>(rect.top);
    const auto right = static_cast<Gdiplus::REAL>(rect.right);
    const auto bottom = static_cast<Gdiplus::REAL>(rect.bottom);
    const auto d = static_cast<Gdiplus::REAL>(radius * 2);

    Gdiplus::GraphicsPath path;
    path.AddArc(left, top, d, d, 180.0f, 90.0f);
    path.AddArc(right - d, top, d, d, 270.0f, 90.0f);
    path.AddArc(right - d, bottom - d, d, d, 0.0f, 90.0f);
    path.AddArc(left, bottom - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    graphics.FillPath(&brush, &path);
}
