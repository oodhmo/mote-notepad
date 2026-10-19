#include "help.h"
#include "strings.h"
#include "theme.h"
#include "topbar.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <string>
#include <vector>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

namespace {

constexpr wchar_t kHelpClass[] = L"MoteHelp";
constexpr wchar_t kRepositoryUrl[] = L"https://github.com/oodhmo/mote-notepad";

// ---- 치수 (96 DPI 기준) ----
constexpr int kPadding = 24;
constexpr int kShortcutsColumnWidth = 280;
constexpr int kColumnGap = 32;
constexpr int kAboutWidth = 400;
constexpr int kRowHeight = 24;
constexpr int kSectionGap = 14;       // 구역 사이
constexpr int kKeyGap = 16;           // 단축키와 설명 사이
constexpr int kSwatchSize = 12;
constexpr int kSwatchGap = 6;         // 단축키 글자와 색 견본 사이
constexpr int kSummaryGap = 8;        // 제목과 요약 사이
constexpr int kContentGap = 18;       // 요약과 표 사이
constexpr int kButtonsGap = 24;       // 내용과 닫기 버튼 사이
constexpr int kButtonHeight = 32;
constexpr int kButtonPadX = 16;
constexpr int kMinButtonWidth = 72;
constexpr int kBottomPadding = 20;
constexpr int kCornerRadius = 6;
// 글자 크기 (96 DPI 기준 px). 확인창(confirm.cpp)과 같은 크기를 쓴다.
constexpr int kTitleFontPx = 15;
constexpr int kAboutTitleFontPx = 20;
constexpr int kHeaderFontPx = 12;
constexpr int kBodyFontPx = 13;

enum class HelpMode { Shortcuts, About };

// 화면에 그릴 글자 한 조각
struct TextPiece {
    RECT rect;
    std::wstring text;
    HFONT font;
    COLORREF color;
    UINT format;
};

struct ColorSwatch {
    RECT rect;
    COLORREF color;
};

struct HelpState {
    HWND hwnd = nullptr;
    UINT dpi = 96;
    HFONT titleFont = nullptr;
    HFONT headerFont = nullptr;
    HFONT keyFont = nullptr;
    HFONT bodyFont = nullptr;
    std::vector<TextPiece> pieces;
    std::vector<ColorSwatch> swatches;
    RECT closeButton = {};
    RECT link = {};          // mote 정보의 GitHub 주소 (없으면 빈 사각형)
    bool closeHot = false;
    bool closePressed = false;
    bool linkHot = false;
    bool trackingLeave = false;
    bool drawBorder = false;  // DWM 둥근 모서리를 못 쓰는 환경(Windows 10)에서는 직접 테두리를 그린다
    bool done = false;
};

HelpState g_help;

// ---- 단축키 표 ----

struct HelpRow {
    const wchar_t* keys;
    StrId description;
    int colorSlot;        // Ctrl+F1~F3 행이면 0~2 (색 견본을 함께 그린다)
};

struct HelpSection {
    StrId title;
    std::vector<HelpRow> rows;
    StrId note;           // 구역 아래 작은 안내 (없으면 Count)
};

HelpRow Row(const wchar_t* keys, StrId description, int colorSlot = -1)
{
    return {keys, description, colorSlot};
}

// 왼쪽 열과 오른쪽 열에 들어갈 구역. 메뉴 리소스(app.rc)의 단축키와 같게 유지한다.
std::vector<HelpSection> LeftColumn()
{
    return {
        {StrId::HelpSectionFile, {
            Row(L"Ctrl+N", StrId::HelpNew),
            Row(L"Ctrl+Shift+N", StrId::HelpNewWindow),
            Row(L"Ctrl+O", StrId::HelpOpen),
            Row(L"Ctrl+S", StrId::Save),
            Row(L"Ctrl+Shift+S", StrId::HelpSaveAs),
        }, StrId::HelpDropNote},
        {StrId::HelpSectionEdit, {
            Row(L"Ctrl+Z", StrId::HelpUndo),
            Row(L"Ctrl+Y", StrId::HelpRedo),
            Row(L"Ctrl+X / C / V", StrId::HelpClipboard),
            Row(L"Ctrl+A", StrId::HelpSelectAll),
        }, StrId::Count},
        {StrId::HelpSectionView, {
            Row(L"Ctrl+\\", StrId::HelpToolbar),
            Row(L"F1", StrId::HelpTitle),
        }, StrId::Count},
    };
}

std::vector<HelpSection> RightColumn()
{
    return {
        {StrId::HelpSectionFormat, {
            Row(L"Ctrl+B", StrId::HelpBold),
            Row(L"Ctrl+I", StrId::HelpItalic),
            Row(L"Ctrl+U", StrId::HelpUnderline),
            Row(L"Ctrl+F1", StrId::HelpColor, 0),
            Row(L"Ctrl+F2", StrId::HelpColor, 1),
            Row(L"Ctrl+F3", StrId::HelpColor, 2),
        }, StrId::HelpColorNote},
        {StrId::HelpSectionFind, {
            Row(L"Ctrl+F", StrId::HelpFind),
            Row(L"Ctrl+H", StrId::HelpReplace),
            Row(L"F3", StrId::HelpFindNext),
            Row(L"Shift+F3", StrId::HelpFindPrevious),
            Row(L"Esc", StrId::HelpCloseFind),
        }, StrId::Count},
    };
}

// ---- 공통 ----

int TextWidth(HDC dc, HFONT font, const std::wstring& text)
{
    HGDIOBJ old = SelectObject(dc, font);
    SIZE size = {};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(dc, old);
    return size.cx;
}

int TextHeight(HDC dc, HFONT font)
{
    HGDIOBJ old = SelectObject(dc, font);
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    return tm.tmHeight;
}

// 폭에 맞춰 줄바꿈했을 때의 높이 (text는 WrapAtSpaces로 미리 나눠 둔다)
int WrappedHeight(HDC dc, HFONT font, const std::wstring& text, int width)
{
    RECT measure = {0, 0, width, 0};
    HGDIOBJ old = SelectObject(dc, font);
    DrawTextW(dc, text.c_str(), -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    return measure.bottom;
}

void AddText(const RECT& rect, std::wstring text, HFONT font, COLORREF color, UINT format)
{
    g_help.pieces.push_back({rect, std::move(text), font, color, format | DT_NOPREFIX});
}

COLORREF LinkColor()
{
    return IsDarkTheme() ? RGB(0x6A, 0xB0, 0xDE) : RGB(0x33, 0x7E, 0xA9);
}

// 실행 파일의 버전 리소스에서 "0.1.0" 형식의 버전을 읽는다 (app.rc의 FILEVERSION)
std::wstring AppVersion()
{
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(VS_VERSION_INFO), RT_VERSION);
    HGLOBAL loaded = resource ? LoadResource(nullptr, resource) : nullptr;
    const auto* data = static_cast<const BYTE*>(loaded ? LockResource(loaded) : nullptr);
    if (!data) return {};
    const DWORD size = SizeofResource(nullptr, resource);
    // VS_FIXEDFILEINFO는 4바이트 경계에 놓이고 0xFEEF04BD 서명으로 시작한다
    for (DWORD offset = 0; offset + sizeof(VS_FIXEDFILEINFO) <= size; offset += 4) {
        const auto* info = reinterpret_cast<const VS_FIXEDFILEINFO*>(data + offset);
        if (info->dwSignature != 0xFEEF04BD) continue;
        return std::to_wstring(HIWORD(info->dwFileVersionMS)) + L"." + std::to_wstring(LOWORD(info->dwFileVersionMS)) +
               L"." + std::to_wstring(HIWORD(info->dwFileVersionLS));
    }
    return {};
}

// 닫기 버튼을 오른쪽 아래에 놓고 창 높이를 돌려준다
int PlaceCloseButton(HDC dc, int width, int contentBottom)
{
    const UINT dpi = g_help.dpi;
    const int pad = Scale(kPadding, dpi);
    const int buttonWidth = std::max(TextWidth(dc, g_help.bodyFont, Str(StrId::Close)) + Scale(kButtonPadX, dpi) * 2,
                                     Scale(kMinButtonWidth, dpi));
    const int top = contentBottom + Scale(kButtonsGap, dpi);
    g_help.closeButton = {width - pad - buttonWidth, top, width - pad, top + Scale(kButtonHeight, dpi)};
    return g_help.closeButton.bottom + Scale(kBottomPadding, dpi);
}

// 구역들을 한 열에 쌓고 열의 아래쪽 y를 돌려준다
int LayoutColumn(HDC dc, const std::vector<HelpSection>& sections, int left, int top, int width)
{
    const UINT dpi = g_help.dpi;
    const ThemeColors& theme = CurrentTheme();
    const int rowHeight = Scale(kRowHeight, dpi);

    // 열 안의 단축키 폭을 맞춰 설명이 한 줄로 정렬되게 한다
    int keyWidth = 0;
    for (const HelpSection& section : sections) {
        for (const HelpRow& row : section.rows) {
            int w = TextWidth(dc, g_help.keyFont, row.keys);
            if (row.colorSlot >= 0) w += Scale(kSwatchGap + kSwatchSize, dpi);
            keyWidth = std::max(keyWidth, w);
        }
    }
    const int descriptionLeft = left + keyWidth + Scale(kKeyGap, dpi);

    int y = top;
    for (size_t s = 0; s < sections.size(); ++s) {
        const HelpSection& section = sections[s];
        if (s > 0) y += Scale(kSectionGap, dpi);
        AddText({left, y, left + width, y + rowHeight}, Str(section.title), g_help.headerFont, theme.secondaryText,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        y += rowHeight;
        for (const HelpRow& row : section.rows) {
            const RECT line = {left, y, left + width, y + rowHeight};
            const std::wstring keys = row.keys;
            AddText(line, keys, g_help.keyFont, theme.text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            if (row.colorSlot >= 0) {
                const int x = left + TextWidth(dc, g_help.keyFont, keys) + Scale(kSwatchGap, dpi);
                const int size = Scale(kSwatchSize, dpi);
                const int swatchTop = y + (rowHeight - size) / 2;
                g_help.swatches.push_back({{x, swatchTop, x + size, swatchTop + size}, ShortcutColor(row.colorSlot)});
            }
            AddText({descriptionLeft, y, left + width, y + rowHeight}, Str(row.description), g_help.bodyFont, theme.text,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += rowHeight;
        }
        if (section.note != StrId::Count) {
            const std::wstring note = WrapAtSpaces(dc, g_help.headerFont, Str(section.note), width);
            const int height = WrappedHeight(dc, g_help.headerFont, note, width);
            AddText({left, y + Scale(2, dpi), left + width, y + Scale(2, dpi) + height}, note, g_help.headerFont,
                    theme.secondaryText, DT_LEFT | DT_WORDBREAK);
            y += Scale(2, dpi) + height;
        }
    }
    return y;
}

SIZE LayoutShortcuts(HDC dc)
{
    const UINT dpi = g_help.dpi;
    const ThemeColors& theme = CurrentTheme();
    const int pad = Scale(kPadding, dpi);
    const int columnWidth = Scale(kShortcutsColumnWidth, dpi);
    const int width = pad * 2 + columnWidth * 2 + Scale(kColumnGap, dpi);
    const int contentWidth = width - pad * 2;

    int y = pad;
    const int titleHeight = TextHeight(dc, g_help.titleFont);
    AddText({pad, y, width - pad, y + titleHeight}, Str(StrId::HelpTitle), g_help.titleFont, theme.text,
            DT_LEFT | DT_SINGLELINE);
    y += titleHeight + Scale(kSummaryGap, dpi);

    const std::wstring summary = WrapAtSpaces(dc, g_help.bodyFont, Str(StrId::HelpSummary), contentWidth);
    const int summaryHeight = WrappedHeight(dc, g_help.bodyFont, summary, contentWidth);
    AddText({pad, y, width - pad, y + summaryHeight}, summary, g_help.bodyFont, theme.secondaryText, DT_LEFT | DT_WORDBREAK);
    y += summaryHeight + Scale(kContentGap, dpi);

    const int leftBottom = LayoutColumn(dc, LeftColumn(), pad, y, columnWidth);
    const int rightBottom = LayoutColumn(dc, RightColumn(), pad + columnWidth + Scale(kColumnGap, dpi), y, columnWidth);
    return {width, PlaceCloseButton(dc, width, std::max(leftBottom, rightBottom))};
}

SIZE LayoutAbout(HDC dc)
{
    const UINT dpi = g_help.dpi;
    const ThemeColors& theme = CurrentTheme();
    const int pad = Scale(kPadding, dpi);
    const int width = Scale(kAboutWidth, dpi);
    const int contentWidth = width - pad * 2;

    int y = pad;
    const int titleHeight = TextHeight(dc, g_help.titleFont);
    AddText({pad, y, width - pad, y + titleHeight}, L"mote", g_help.titleFont, theme.text, DT_LEFT | DT_SINGLELINE);
    y += titleHeight + Scale(2, dpi);

    const int bodyHeight = TextHeight(dc, g_help.bodyFont);
    std::wstring version = Str(StrId::AboutVersion);
    const size_t slot = version.find(L"%s");
    if (slot != std::wstring::npos) version.replace(slot, 2, AppVersion());
    AddText({pad, y, width - pad, y + bodyHeight}, version, g_help.bodyFont, theme.secondaryText, DT_LEFT | DT_SINGLELINE);
    y += bodyHeight + Scale(kContentGap, dpi);

    const std::wstring description = WrapAtSpaces(dc, g_help.bodyFont, Str(StrId::AboutDescription), contentWidth);
    const int descriptionHeight = WrappedHeight(dc, g_help.bodyFont, description, contentWidth);
    AddText({pad, y, width - pad, y + descriptionHeight}, description, g_help.bodyFont, theme.text, DT_LEFT | DT_WORDBREAK);
    y += descriptionHeight + Scale(kSummaryGap, dpi);

    // GitHub 주소: 글자 폭만큼만 클릭 영역으로 쓴다 (밑줄과 손 모양 커서는 그릴 때/WM_SETCURSOR에서)
    const std::wstring linkText = kRepositoryUrl + 8;  // "https://"는 빼고 보여 준다
    const int linkWidth = std::min(TextWidth(dc, g_help.bodyFont, linkText), contentWidth);
    g_help.link = {pad, y, pad + linkWidth, y + bodyHeight};
    y += bodyHeight;

    return {width, PlaceCloseButton(dc, width, y)};
}

bool HitClose(POINT pt) { return PtInRect(&g_help.closeButton, pt) != FALSE; }
bool HitLink(POINT pt) { return PtInRect(&g_help.link, pt) != FALSE; }

void Paint(HWND hwnd)
{
    const ThemeColors& theme = CurrentTheme();
    const int radius = Scale(kCornerRadius, g_help.dpi);
    BufferedPaint paint(hwnd);
    FillSolid(paint.dc, paint.client, theme.windowBg);
    if (g_help.drawBorder) {
        HBRUSH border = CreateSolidBrush(theme.border);
        FrameRect(paint.dc, &paint.client, border);
        DeleteObject(border);
    }

    {
        Gdiplus::Graphics graphics(paint.dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        for (const ColorSwatch& swatch : g_help.swatches) {
            Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(swatch.color), GetGValue(swatch.color), GetBValue(swatch.color)));
            graphics.FillEllipse(&brush, static_cast<Gdiplus::REAL>(swatch.rect.left), static_cast<Gdiplus::REAL>(swatch.rect.top),
                                 static_cast<Gdiplus::REAL>(swatch.rect.right - swatch.rect.left),
                                 static_cast<Gdiplus::REAL>(swatch.rect.bottom - swatch.rect.top));
        }
        const COLORREF button = g_help.closePressed ? theme.primaryPressed : g_help.closeHot ? theme.primaryHover : theme.primaryBg;
        FillRoundRect(graphics, g_help.closeButton, button, radius);
    }

    SetBkMode(paint.dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(paint.dc, g_help.bodyFont);
    for (const TextPiece& piece : g_help.pieces) {
        SelectObject(paint.dc, piece.font);
        SetTextColor(paint.dc, piece.color);
        RECT rect = piece.rect;
        DrawTextW(paint.dc, piece.text.c_str(), -1, &rect, piece.format);
    }

    if (!IsRectEmpty(&g_help.link)) {
        SelectObject(paint.dc, g_help.bodyFont);
        SetTextColor(paint.dc, LinkColor());
        RECT rect = g_help.link;
        DrawTextW(paint.dc, kRepositoryUrl + 8, -1, &rect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (g_help.linkHot) {
            RECT underline = {g_help.link.left, g_help.link.bottom - 1, g_help.link.right, g_help.link.bottom};
            FillSolid(paint.dc, underline, LinkColor());
        }
    }

    SelectObject(paint.dc, g_help.bodyFont);
    SetTextColor(paint.dc, theme.primaryText);
    RECT closeRect = g_help.closeButton;
    DrawTextW(paint.dc, Str(StrId::Close), -1, &closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(paint.dc, oldFont);
}

void SetHot(bool closeHot, bool linkHot)
{
    if (closeHot == g_help.closeHot && linkHot == g_help.linkHot) return;
    g_help.closeHot = closeHot;
    g_help.linkHot = linkHot;
    InvalidateRect(g_help.hwnd, nullptr, FALSE);
}

LRESULT CALLBACK HelpProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_RETURN || wp == VK_SPACE) {
            g_help.done = true;
            return 0;
        }
        break;

    case WM_MOUSEMOVE: {
        if (!g_help.trackingLeave) {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            g_help.trackingLeave = TrackMouseEvent(&tme) != FALSE;
        }
        const POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        SetHot(HitClose(pt), HitLink(pt));
        return 0;
    }

    case WM_MOUSELEAVE:
        g_help.trackingLeave = false;
        SetHot(false, false);
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && g_help.linkHot) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        // 확인창 버튼처럼 눌렀다 뗄 때 실행한다. 버튼 밖에서 떼면 취소된다.
        if (HitClose({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)})) {
            g_help.closePressed = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONUP: {
        const POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (GetCapture() == hwnd) {
            ReleaseCapture();
            if (HitClose(pt)) g_help.done = true;
        } else if (HitLink(pt)) {
            ShellExecuteW(hwnd, L"open", kRepositoryUrl, nullptr, nullptr, SW_SHOWNORMAL);
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (g_help.closePressed) {
            g_help.closePressed = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_CLOSE:
        g_help.done = true;  // Alt+F4 (창은 ShowHelpWindow가 정리한다)
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        Paint(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ShowHelpWindow(HWND owner, HelpMode mode)
{
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.style = CS_DROPSHADOW;  // 확인창과 같은 옅은 그림자
        wc.lpfnWndProc = HelpProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kHelpClass;
        registered = RegisterClassExW(&wc) != 0;
    }

    g_help = HelpState{};
    g_help.dpi = GetDpiForWindow(owner);
    g_help.titleFont = CreateUiFontPx(mode == HelpMode::About ? kAboutTitleFontPx : kTitleFontPx, g_help.dpi, FW_BOLD);
    g_help.headerFont = CreateUiFontPx(kHeaderFontPx, g_help.dpi);
    g_help.keyFont = CreateUiFontPx(kBodyFontPx, g_help.dpi, FW_SEMIBOLD);
    g_help.bodyFont = CreateUiFontPx(kBodyFontPx, g_help.dpi);

    HDC dc = GetDC(nullptr);
    const SIZE size = mode == HelpMode::About ? LayoutAbout(dc) : LayoutShortcuts(dc);
    ReleaseDC(nullptr, dc);

    // owner 창 가운데에 띄우고 화면 밖으로 나가지 않게 한다
    RECT ownerRect;
    GetWindowRect(owner, &ownerRect);
    int x = (ownerRect.left + ownerRect.right - size.cx) / 2;
    int y = (ownerRect.top + ownerRect.bottom - size.cy) / 2;
    MONITORINFO mi = {sizeof(mi)};
    if (GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi)) {
        const int left = mi.rcWork.left, top = mi.rcWork.top;
        x = std::clamp(x, left, std::max(left, static_cast<int>(mi.rcWork.right - size.cx)));
        y = std::clamp(y, top, std::max(top, static_cast<int>(mi.rcWork.bottom - size.cy)));
    }

    g_help.hwnd = CreateWindowExW(0, kHelpClass, L"mote", WS_POPUP, x, y, size.cx, size.cy, owner, nullptr, instance, nullptr);
    if (g_help.hwnd) {
        // Windows 11: 둥근 모서리 + 시스템 테두리. 실패하면(Windows 10) 직접 테두리를 그린다.
        const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        g_help.drawBorder = FAILED(DwmSetWindowAttribute(g_help.hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner)));

        // 모달: owner를 비활성화하고 창이 닫힐 때까지 자체 메시지 루프를 돈다
        EnableWindow(owner, FALSE);
        ShowWindow(g_help.hwnd, SW_SHOW);
        SetFocus(g_help.hwnd);

        MSG msg;
        while (!g_help.done) {
            const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
            if (got == 0) {
                PostQuitMessage(static_cast<int>(msg.wParam));  // 바깥 루프도 끝나도록 WM_QUIT을 되돌려 놓는다
                break;
            }
            if (got == -1) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // owner를 먼저 다시 활성화해야 창을 없앨 때 활성화가 다른 앱으로 넘어가지 않는다
        EnableWindow(owner, TRUE);
        DestroyWindow(g_help.hwnd);
        g_help.hwnd = nullptr;
    }
    DeleteObject(g_help.titleFont);
    DeleteObject(g_help.headerFont);
    DeleteObject(g_help.keyFont);
    DeleteObject(g_help.bodyFont);
}

}  // namespace

void ShowShortcutsHelp(HWND owner)
{
    ShowHelpWindow(owner, HelpMode::Shortcuts);
}

void ShowAbout(HWND owner)
{
    ShowHelpWindow(owner, HelpMode::About);
}
