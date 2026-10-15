#include "theme.h"

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
};

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

}  // namespace

const ThemeColors& CurrentTheme()
{
    return kLight;
}

const wchar_t* UiFontFace()
{
    // Windows 11 기본 UI 글꼴. Windows 10에는 없으므로 한 번만 확인해 둔다.
    static const wchar_t* face = FontExists(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    return face;
}

HFONT CreateUiFont(int pointTenths, UINT dpi, int weight, const wchar_t* face)
{
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(pointTenths, static_cast<int>(dpi), 720);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, face ? face : UiFontFace(), _TRUNCATE);
    return CreateFontIndirectW(&lf);
}
