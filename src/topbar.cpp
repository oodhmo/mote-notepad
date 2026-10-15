#include "topbar.h"
#include "theme.h"
#include "../resources/resource.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <iterator>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

namespace {

constexpr wchar_t kBarClass[] = L"MoteTopBar";
constexpr wchar_t kPopupClass[] = L"MoteColorPopup";
constexpr wchar_t kExpandClass[] = L"MoteExpandButton";

// ---- 상단 바 치수 (96 DPI 기준) ----
constexpr int kBarHeight = 40;
constexpr int kButtonHeight = 28;
constexpr int kMenuPaddingX = 10;
constexpr int kEdgeMargin = 8;
constexpr int kItemGap = 2;
constexpr int kToolButtonWidth = 32;
constexpr int kToolGap = 4;           // B, I, U 사이 간격
constexpr int kColorGap = 10;         // U와 색 버튼 사이는 조금 더 띄운다
constexpr int kCollapseGap = 8;       // 색 버튼과 접기 화살표 사이
constexpr int kCollapseButtonWidth = 28;
constexpr int kRightMargin = 12;
constexpr int kIndicatorSize = 14;    // 원형 색 표시 지름
constexpr int kCornerRadius = 6;
constexpr int kMenuFontTenths = 100;  // 10pt: 본문(11.5pt)보다 약간 작게
constexpr int kIconPixelSize = 16;    // 아이콘 글꼴 크기 (작은 아이콘)
constexpr int kChevronPixelSize = 12;  // 접기/펼치기 화살표는 서식 아이콘보다 작고 옅게

// Segoe Fluent Icons / MDL2 Assets 화살표
constexpr wchar_t kChevronUp[] = L"\uE70E";
constexpr wchar_t kChevronDown[] = L"\uE70D";

// ---- 색상 팝업 치수 ----
constexpr int kPopupPadding = 10;
constexpr int kSwatchCell = 28;
constexpr int kSwatchSize = 18;
constexpr int kCustomRowHeight = 30;
constexpr DWORD kReopenGuardMs = 200;  // 팝업을 닫은 클릭이 곧바로 다시 여는 것을 막는 시간

constexpr int kPaletteCount = 8;
constexpr int kCustomItem = kPaletteCount;  // 팔레트 다음 항목 = Custom color…

// Notion 계열의 차분한 글자색. 0번(Black)은 테마 기본 글자색을 사용한다.
constexpr COLORREF kPalette[kPaletteCount] = {
    0,                       // Black (기본색, PaletteColor에서 대체)
    RGB(0x78, 0x77, 0x74),   // Gray
    RGB(0xD4, 0x4C, 0x47),   // Red
    RGB(0xD9, 0x73, 0x0D),   // Orange
    RGB(0xCB, 0x91, 0x2F),   // Yellow
    RGB(0x44, 0x83, 0x61),   // Green
    RGB(0x33, 0x7E, 0xA9),   // Blue
    RGB(0x90, 0x65, 0xB0),   // Purple
};

enum class ItemKind { Menu, Toggle, Color, Button };

struct BarItem {
    ItemKind kind;
    const wchar_t* label;
    UINT command;
    const wchar_t* tooltip;
    RECT rect;
};

enum { kFileItem, kEditItem, kBoldItem, kItalicItem, kUnderlineItem, kColorItem, kCollapseItem, kItemCount };

struct TopBarState {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    HWND tooltip = nullptr;
    HMENU menus = nullptr;
    UINT dpi = 96;
    HFONT menuFont = nullptr;
    HFONT iconFont = nullptr;
    HFONT chevronFont = nullptr;
    bool collapsed = false;  // 사용자가 바를 접었는지 (접힌 채 잠깐 보이는 중이면 화살표 방향이 바뀐다)
    BarItem items[kItemCount] = {
        {ItemKind::Menu, L"File", 0, nullptr, {}},
        {ItemKind::Menu, L"Edit", 0, nullptr, {}},
        {ItemKind::Toggle, L"", IDM_FORMAT_BOLD, L"굵게 (Ctrl+B)", {}},
        {ItemKind::Toggle, L"", IDM_FORMAT_ITALIC, L"기울임꼴 (Ctrl+I)", {}},
        {ItemKind::Toggle, L"", IDM_FORMAT_UNDERLINE, L"밑줄 (Ctrl+U)", {}},
        {ItemKind::Color, L"", 0, L"글자색", {}},
        {ItemKind::Button, kChevronUp, IDM_VIEW_TOOLBAR, L"툴바 숨기기 (Ctrl+\\)", {}},
    };
    int hot = -1;       // 마우스가 올라간 항목
    int pressed = -1;   // 눌린 버튼 / 팝업이 열려 있는 항목
    bool trackingLeave = false;
    FormatState format;
    DWORD popupClosedTick = 0;
};

struct PopupState {
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    RECT cells[kPaletteCount + 1] = {};  // 마지막은 Custom color 행
    RECT separator = {};
    int hot = -1;          // 마우스/키보드로 가리키는 항목
    bool closing = false;
    bool trackingLeave = false;
    bool drawBorder = false;    // DWM 둥근 모서리를 못 쓰는 환경(Windows 10)에서는 직접 테두리를 그린다
    bool lostToOtherApp = false;
};

// 바를 접었을 때 창 오른쪽 위에 나타나는 작은 "펼치기" 화살표 버튼
struct ExpandButtonState {
    HWND hwnd = nullptr;
    HWND tooltip = nullptr;
    bool hot = false;
    bool pressed = false;
    bool trackingLeave = false;
};

TopBarState g_bar;
PopupState g_popup;
ExpandButtonState g_expand;

// ================= 공통 그리기 =================

Gdiplus::Color ToGdiplus(COLORREF color)
{
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

Gdiplus::RectF CircleRect(const RECT& cell, int diameter)
{
    const auto d = static_cast<Gdiplus::REAL>(diameter);
    const auto cx = (cell.left + cell.right) / 2.0f;
    const auto cy = (cell.top + cell.bottom) / 2.0f;
    return Gdiplus::RectF(cx - d / 2, cy - d / 2, d, d);
}

int HitTestRects(const RECT* rects, int count, POINT pt)
{
    for (int i = 0; i < count; ++i) {
        if (PtInRect(&rects[i], pt)) return i;
    }
    return -1;
}

// ================= 색상 팝업 =================

COLORREF PaletteColor(int index)
{
    return index == 0 ? DefaultTextColor() : kPalette[index];
}

bool IsSelectedSwatch(int index)
{
    return !g_bar.format.colorMixed && g_bar.format.color == PaletteColor(index);
}

SIZE LayoutPopup(UINT dpi)
{
    const int pad = Scale(kPopupPadding, dpi);
    const int cell = Scale(kSwatchCell, dpi);
    for (int i = 0; i < kPaletteCount; ++i) {
        g_popup.cells[i] = {pad + i * cell, pad, pad + (i + 1) * cell, pad + cell};
    }
    const int width = pad * 2 + kPaletteCount * cell;
    const int separatorY = pad + cell + Scale(6, dpi);
    g_popup.separator = {pad, separatorY, width - pad, separatorY + 1};
    const int rowTop = separatorY + 1 + Scale(4, dpi);
    const int inset = Scale(4, dpi);
    g_popup.cells[kCustomItem] = {inset, rowTop, width - inset, rowTop + Scale(kCustomRowHeight, dpi)};
    return {width, g_popup.cells[kCustomItem].bottom + inset};
}

void PaintPopup(HWND hwnd)
{
    const ThemeColors& theme = CurrentTheme();
    const UINT dpi = g_bar.dpi;
    BufferedPaint paint(hwnd);
    FillSolid(paint.dc, paint.client, theme.windowBg);
    FillSolid(paint.dc, g_popup.separator, theme.border);

    {
        Gdiplus::Graphics graphics(paint.dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        if (g_popup.drawBorder) {
            Gdiplus::Pen pen(ToGdiplus(theme.border), 1.0f);
            graphics.DrawRectangle(&pen, 0.0f, 0.0f, paint.client.right - 1.0f, paint.client.bottom - 1.0f);
        }

        for (int i = 0; i < kPaletteCount; ++i) {
            const RECT& cell = g_popup.cells[i];
            if (i == g_popup.hot) {
                RECT inner = cell;
                InflateRect(&inner, -Scale(1, dpi), -Scale(1, dpi));
                FillRoundRect(graphics, inner, theme.hover, Scale(kCornerRadius, dpi));
            }
            const COLORREF color = PaletteColor(i);
            if (IsSelectedSwatch(i)) {
                // 현재 색은 바깥 고리로 표시한다
                Gdiplus::Pen ring(ToGdiplus(color), static_cast<Gdiplus::REAL>(Scale(2, dpi)));
                graphics.DrawEllipse(&ring, CircleRect(cell, Scale(kSwatchSize + 6, dpi)));
            }
            Gdiplus::SolidBrush brush(ToGdiplus(color));
            graphics.FillEllipse(&brush, CircleRect(cell, Scale(kSwatchSize, dpi)));
        }

        const RECT& row = g_popup.cells[kCustomItem];
        if (g_popup.hot == kCustomItem) FillRoundRect(graphics, row, theme.hover, Scale(kCornerRadius, dpi));

        // "Custom color" 아이콘: 네 가지 색으로 나눈 작은 원
        const int iconSize = Scale(16, dpi);
        RECT iconCell = {row.left + Scale(6, dpi), row.top, row.left + Scale(6, dpi) + Scale(kSwatchCell, dpi) - Scale(4, dpi), row.bottom};
        const Gdiplus::RectF icon = CircleRect(iconCell, iconSize);
        const COLORREF quadrants[4] = {kPalette[2], kPalette[4], kPalette[5], kPalette[6]};
        for (int q = 0; q < 4; ++q) {
            Gdiplus::SolidBrush brush(ToGdiplus(quadrants[q]));
            graphics.FillPie(&brush, icon, -90.0f + q * 90.0f, 90.0f);
        }
    }

    const RECT& row = g_popup.cells[kCustomItem];
    RECT text = {row.left + Scale(6 + kSwatchCell + 2, dpi), row.top, row.right, row.bottom};
    HGDIOBJ oldFont = SelectObject(paint.dc, g_popup.font);
    SetBkMode(paint.dc, TRANSPARENT);
    SetTextColor(paint.dc, theme.text);
    DrawTextW(paint.dc, L"Custom color…", -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(paint.dc, oldFont);
}

void SetPopupHot(int index)
{
    if (g_popup.hot == index) return;
    g_popup.hot = index;
    InvalidateRect(g_popup.hwnd, nullptr, FALSE);
}

void CloseColorPopup()
{
    if (!g_popup.hwnd || g_popup.closing) return;
    g_popup.closing = true;
    // 소유 창(메인 창)이 다시 활성화되고, 메인 창의 WM_SETFOCUS가 에디터로 포커스를 돌려준다
    DestroyWindow(g_popup.hwnd);
    g_popup.hwnd = nullptr;
    g_popup.closing = false;

    g_bar.popupClosedTick = GetTickCount();
    g_bar.pressed = -1;
    InvalidateRect(g_bar.hwnd, nullptr, FALSE);
}

void ChoosePopupItem(int index)
{
    if (index < 0 || index > kCustomItem) return;
    const COLORREF color = index < kPaletteCount ? PaletteColor(index) : 0;
    CloseColorPopup();
    if (index == kCustomItem) {
        PostMessageW(g_bar.owner, WM_COMMAND, IDM_FORMAT_CUSTOMCOLOR, 0);
    } else {
        PostMessageW(g_bar.owner, WM_MOTE_SETCOLOR, 0, static_cast<LPARAM>(color));
    }
}

// 방향키로 항목 사이를 이동한다 (팔레트 한 줄 + 아래 Custom color 행)
void MovePopupFocus(WPARAM key)
{
    int hot = g_popup.hot;
    if (hot < 0) {
        hot = 0;
        for (int i = 0; i < kPaletteCount; ++i) {
            if (IsSelectedSwatch(i)) hot = i;
        }
        SetPopupHot(hot);
        return;
    }
    switch (key) {
    case VK_LEFT:  hot = hot == kCustomItem ? kCustomItem : std::max(0, hot - 1); break;
    case VK_RIGHT: hot = hot == kCustomItem ? kCustomItem : std::min(kPaletteCount - 1, hot + 1); break;
    case VK_DOWN:  hot = kCustomItem; break;
    case VK_UP:    hot = hot == kCustomItem ? 0 : hot; break;
    case VK_HOME:  hot = 0; break;
    case VK_END:   hot = kCustomItem; break;
    }
    SetPopupHot(hot);
}

LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ACTIVATE:
        // 다른 곳을 클릭하는 등 팝업이 비활성화되면 닫는다.
        // 활성화 전환 도중에 창을 파괴하지 않도록 WM_CLOSE를 게시해 나중에 닫는다.
        if (LOWORD(wp) == WA_INACTIVE && !g_popup.closing) {
            HWND next = reinterpret_cast<HWND>(lp);
            g_popup.lostToOtherApp = !next || GetAncestor(next, GA_ROOTOWNER) != g_bar.owner;
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;

    case WM_CLOSE: {
        const bool otherApp = g_popup.lostToOtherApp;
        CloseColorPopup();
        // 메인 창이 계속 활성 모양이었으므로, 다른 앱으로 넘어갔다면 비활성 모양으로 다시 그린다
        if (otherApp) SendMessageW(g_bar.owner, WM_NCACTIVATE, FALSE, 0);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (!g_popup.trackingLeave) {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            g_popup.trackingLeave = TrackMouseEvent(&tme) != FALSE;
        }
        SetPopupHot(HitTestRects(g_popup.cells, kCustomItem + 1, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}));
        return 0;

    case WM_MOUSELEAVE:
        g_popup.trackingLeave = false;
        SetPopupHot(-1);
        return 0;

    case WM_LBUTTONUP:
        ChoosePopupItem(HitTestRects(g_popup.cells, kCustomItem + 1, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}));
        return 0;

    case WM_KEYDOWN:
        switch (wp) {
        case VK_ESCAPE:
            CloseColorPopup();
            return 0;
        case VK_RETURN:
        case VK_SPACE:
            ChoosePopupItem(g_popup.hot);
            return 0;
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_HOME: case VK_END:
            MovePopupFocus(wp);
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        PaintPopup(hwnd);
        return 0;

    case WM_DESTROY:
        if (g_popup.font) {
            DeleteObject(g_popup.font);
            g_popup.font = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ShowColorPopup(const RECT& anchor)
{
    const UINT dpi = g_bar.dpi;
    const SIZE size = LayoutPopup(dpi);

    // 색 버튼의 오른쪽 끝에 맞춰 바로 아래에 띄우고, 화면 밖으로 나가지 않게 한다
    int x = anchor.right - size.cx;
    int y = anchor.bottom + Scale(4, dpi);
    MONITORINFO mi = {sizeof(mi)};
    if (GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &mi)) {
        x = std::clamp(x, static_cast<int>(mi.rcWork.left), static_cast<int>(mi.rcWork.right - size.cx));
        if (y + size.cy > mi.rcWork.bottom) y = anchor.top - Scale(4, dpi) - size.cy;
    }

    g_popup.hot = -1;
    g_popup.trackingLeave = false;
    g_popup.lostToOtherApp = false;
    g_popup.font = CreateUiFont(kMenuFontTenths, dpi);
    g_popup.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kPopupClass, L"", WS_POPUP,
                                   x, y, size.cx, size.cy, g_bar.owner, nullptr, g_bar.instance, nullptr);
    if (!g_popup.hwnd) {
        DeleteObject(g_popup.font);
        g_popup.font = nullptr;
        return;
    }

    // Windows 11: 작은 둥근 모서리 + 시스템 테두리. 실패하면(Windows 10) 직접 테두리를 그린다.
    const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUNDSMALL;
    g_popup.drawBorder = FAILED(DwmSetWindowAttribute(g_popup.hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner)));

    g_bar.pressed = kColorItem;
    InvalidateRect(g_bar.hwnd, nullptr, FALSE);
    // 팝업을 활성화해 키보드(방향키/Enter/Esc)를 받는다
    ShowWindow(g_popup.hwnd, SW_SHOW);
}

// ================= 상단 바 =================

void CreateBarFonts()
{
    if (g_bar.menuFont) DeleteObject(g_bar.menuFont);
    if (g_bar.iconFont) DeleteObject(g_bar.iconFont);
    if (g_bar.chevronFont) DeleteObject(g_bar.chevronFont);
    g_bar.menuFont = CreateUiFont(kMenuFontTenths, g_bar.dpi);

    LOGFONTW lf = {};
    lf.lfHeight = -Scale(kIconPixelSize, g_bar.dpi);
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, IconFontFace(), _TRUNCATE);
    g_bar.iconFont = CreateFontIndirectW(&lf);
    lf.lfHeight = -Scale(kChevronPixelSize, g_bar.dpi);
    g_bar.chevronFont = CreateFontIndirectW(&lf);
}

void LayoutItems()
{
    RECT client;
    GetClientRect(g_bar.hwnd, &client);
    const UINT dpi = g_bar.dpi;
    const int buttonHeight = Scale(kButtonHeight, dpi);
    const int top = (Scale(kBarHeight, dpi) - buttonHeight) / 2;

    // 왼쪽: 메뉴 버튼은 글자 폭에 맞춘다
    HDC dc = GetDC(g_bar.hwnd);
    HGDIOBJ oldFont = SelectObject(dc, g_bar.menuFont);
    int x = Scale(kEdgeMargin, dpi);
    for (int i : {kFileItem, kEditItem}) {
        BarItem& item = g_bar.items[i];
        SIZE size = {};
        GetTextExtentPoint32W(dc, item.label, lstrlenW(item.label), &size);
        const int width = size.cx + Scale(kMenuPaddingX, dpi) * 2;
        item.rect = {x, top, x + width, top + buttonHeight};
        x += width + Scale(kItemGap, dpi);
    }
    SelectObject(dc, oldFont);
    ReleaseDC(g_bar.hwnd, dc);

    // 오른쪽: 맨 끝의 접기 화살표, 그 왼쪽으로 색 버튼, U, I, B를 배치한다
    const int width = Scale(kToolButtonWidth, dpi);
    int right = client.right - Scale(kRightMargin, dpi);
    const int collapseWidth = Scale(kCollapseButtonWidth, dpi);
    g_bar.items[kCollapseItem].rect = {right - collapseWidth, top, right, top + buttonHeight};
    right -= collapseWidth + Scale(kCollapseGap, dpi);
    g_bar.items[kColorItem].rect = {right - width, top, right, top + buttonHeight};
    right -= width + Scale(kColorGap, dpi);
    for (int i : {kUnderlineItem, kItalicItem, kBoldItem}) {
        g_bar.items[i].rect = {right - width, top, right, top + buttonHeight};
        right -= width + Scale(kToolGap, dpi);
    }

    if (g_bar.tooltip) {
        for (int i = kBoldItem; i <= kCollapseItem; ++i) {
            TTTOOLINFOW ti = {sizeof(ti)};
            ti.hwnd = g_bar.hwnd;
            ti.uId = static_cast<UINT_PTR>(i);
            ti.rect = g_bar.items[i].rect;
            SendMessageW(g_bar.tooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&ti));
        }
    }
}

int HitTest(POINT pt)
{
    for (int i = 0; i < kItemCount; ++i) {
        if (PtInRect(&g_bar.items[i].rect, pt)) return i;
    }
    return -1;
}

bool IsToggleActive(int index)
{
    switch (index) {
    case kBoldItem:      return g_bar.format.bold;
    case kItalicItem:    return g_bar.format.italic;
    case kUnderlineItem: return g_bar.format.underline;
    default:             return false;
    }
}

HFONT ItemFont(int index)
{
    switch (g_bar.items[index].kind) {
    case ItemKind::Toggle: return g_bar.iconFont;
    case ItemKind::Button: return g_bar.chevronFont;
    default:               return g_bar.menuFont;
    }
}

void PaintBar(HWND hwnd)
{
    const ThemeColors& theme = CurrentTheme();
    const UINT dpi = g_bar.dpi;
    BufferedPaint paint(hwnd);
    FillSolid(paint.dc, paint.client, theme.windowBg);

    // 편집 영역과의 경계는 1px의 아주 옅은 선으로만 표시한다
    FillSolid(paint.dc, {0, paint.client.bottom - 1, paint.client.right, paint.client.bottom}, theme.border);

    {
        Gdiplus::Graphics graphics(paint.dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        // 배경은 hover / 눌림 / 활성 상태일 때만 표시한다 (평소에는 투명)
        for (int i = 0; i < kItemCount; ++i) {
            const RECT& rect = g_bar.items[i].rect;
            if (i == g_bar.pressed || IsToggleActive(i)) {
                FillRoundRect(graphics, rect, theme.pressed, Scale(kCornerRadius, dpi));
            } else if (i == g_bar.hot) {
                FillRoundRect(graphics, rect, theme.hover, Scale(kCornerRadius, dpi));
            }
        }

        // 원형 색 표시: 섞인 색이면 속이 빈 고리로 그린다
        const Gdiplus::RectF dot = CircleRect(g_bar.items[kColorItem].rect, Scale(kIndicatorSize, dpi));
        if (g_bar.format.colorMixed) {
            Gdiplus::Pen pen(ToGdiplus(theme.secondaryText), static_cast<Gdiplus::REAL>(Scale(3, dpi)) / 2.0f);
            graphics.DrawEllipse(&pen, dot);
        } else {
            Gdiplus::SolidBrush brush(ToGdiplus(g_bar.format.color));
            graphics.FillEllipse(&brush, dot);
        }
    }

    SetBkMode(paint.dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(paint.dc, g_bar.menuFont);
    for (int i = kFileItem; i < kItemCount; ++i) {
        BarItem& item = g_bar.items[i];
        if (item.kind == ItemKind::Color) continue;  // 원형 색 표시는 위에서 그렸다
        // B/I/U 아이콘은 평소엔 보조 글자색, hover/활성일 때만 진하게 표시한다
        const bool emphasized = item.kind == ItemKind::Menu || i == g_bar.hot || i == g_bar.pressed || IsToggleActive(i);
        SetTextColor(paint.dc, emphasized ? theme.text : theme.secondaryText);
        SelectObject(paint.dc, ItemFont(i));
        DrawTextW(paint.dc, item.label, -1, &item.rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(paint.dc, oldFont);
}

void SetHot(int index)
{
    if (g_bar.hot == index) return;
    g_bar.hot = index;
    InvalidateRect(g_bar.hwnd, nullptr, FALSE);
}

void OpenMenu(int index, bool fromKeyboard)
{
    if (index != kFileItem && index != kEditItem) return;
    HMENU popup = GetSubMenu(g_bar.menus, index);
    if (!popup) return;

    g_bar.pressed = index;
    InvalidateRect(g_bar.hwnd, nullptr, FALSE);
    UpdateWindow(g_bar.hwnd);

    RECT button = g_bar.items[index].rect;
    MapWindowPoints(g_bar.hwnd, nullptr, reinterpret_cast<POINT*>(&button), 2);
    // 버튼 영역을 제외 영역으로 주어 메뉴가 버튼을 가리지 않고 바로 아래에 열리게 한다
    TPMPARAMS params = {sizeof(params), button};

    // 키보드로 열었을 때는 메뉴처럼 첫 항목이 선택된 상태로 시작한다
    if (fromKeyboard) PostMessageW(g_bar.owner, WM_KEYDOWN, VK_DOWN, 0);

    TrackPopupMenuEx(popup, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERTICAL | TPM_LEFTBUTTON,
                     button.left, button.bottom + Scale(2, g_bar.dpi), g_bar.owner, &params);

    g_bar.pressed = -1;
    // 열린 메뉴를 닫으려고 같은 버튼을 다시 누른 클릭이 메뉴를 다시 여는 것을 막는다
    MSG msg;
    if (PeekMessageW(&msg, g_bar.hwnd, WM_LBUTTONDOWN, WM_LBUTTONDOWN, PM_NOREMOVE)) {
        POINT pt = {GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam)};
        if (HitTest(pt) == index) PeekMessageW(&msg, g_bar.hwnd, WM_LBUTTONDOWN, WM_LBUTTONDOWN, PM_REMOVE);
    }
    POINT cursor;
    GetCursorPos(&cursor);
    ScreenToClient(g_bar.hwnd, &cursor);
    g_bar.hot = HitTest(cursor);
    InvalidateRect(g_bar.hwnd, nullptr, FALSE);
}

void OnBarButtonDown(int index)
{
    if (index < 0) return;
    switch (g_bar.items[index].kind) {
    case ItemKind::Menu:
        OpenMenu(index, false);
        break;
    case ItemKind::Toggle:
    case ItemKind::Button:
        // 일반 버튼처럼 눌렀다 뗄 때 실행한다. 마우스를 캡처해 버튼 밖에서 떼면 취소된다.
        g_bar.pressed = index;
        SetCapture(g_bar.hwnd);
        InvalidateRect(g_bar.hwnd, nullptr, FALSE);
        break;
    case ItemKind::Color: {
        // 열린 팝업을 닫으려고 색 버튼을 다시 누른 경우에는 다시 열지 않는다
        if (GetTickCount() - g_bar.popupClosedTick < kReopenGuardMs) break;
        RECT anchor = g_bar.items[kColorItem].rect;
        MapWindowPoints(g_bar.hwnd, nullptr, reinterpret_cast<POINT*>(&anchor), 2);
        ShowColorPopup(anchor);
        break;
    }
    }
}

LRESULT CALLBACK TopBarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        LayoutItems();
        return 0;

    case WM_MOUSEMOVE: {
        if (!g_bar.trackingLeave) {
            // 마우스가 바를 벗어날 때 hover 표시를 지우기 위해 WM_MOUSELEAVE를 요청한다
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            g_bar.trackingLeave = TrackMouseEvent(&tme) != FALSE;
        }
        SetHot(HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}));
        return 0;
    }
    case WM_MOUSELEAVE:
        g_bar.trackingLeave = false;
        SetHot(-1);
        return 0;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        // 바는 포커스를 가져가지 않는다 → 에디터의 선택 영역과 캐럿이 유지된다
        OnBarButtonDown(HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}));
        return 0;

    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) {
            const int index = g_bar.pressed;
            ReleaseCapture();  // WM_CAPTURECHANGED에서 눌림 상태를 해제한다
            if (index >= 0 && HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}) == index) {
                SendMessageW(g_bar.owner, WM_COMMAND, g_bar.items[index].command, 0);
            }
        }
        return 0;

    case WM_CAPTURECHANGED:
        if (g_bar.pressed >= 0 && (g_bar.items[g_bar.pressed].kind == ItemKind::Toggle ||
                                   g_bar.items[g_bar.pressed].kind == ItemKind::Button)) {
            g_bar.pressed = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;  // WM_PAINT에서 전체를 그린다

    case WM_PAINT:
        PaintBar(hwnd);
        return 0;

    case WM_DESTROY: {
        CloseColorPopup();
        if (g_bar.menuFont) DeleteObject(g_bar.menuFont);
        if (g_bar.iconFont) DeleteObject(g_bar.iconFont);
        if (g_bar.chevronFont) DeleteObject(g_bar.chevronFont);
        g_bar.menuFont = g_bar.iconFont = g_bar.chevronFont = nullptr;
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void CreateTooltips()
{
    g_bar.tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                    g_bar.hwnd, nullptr, g_bar.instance, nullptr);
    if (!g_bar.tooltip) return;
    for (int i = kBoldItem; i <= kCollapseItem; ++i) {
        TTTOOLINFOW ti = {sizeof(ti)};
        ti.uFlags = TTF_SUBCLASS;  // 툴팁이 바의 마우스 메시지를 직접 엿보게 한다
        ti.hwnd = g_bar.hwnd;
        ti.uId = static_cast<UINT_PTR>(i);
        ti.lpszText = const_cast<LPWSTR>(g_bar.items[i].tooltip);
        SendMessageW(g_bar.tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    }
}

// ================= 펼치기 화살표 버튼 =================

void PaintExpandButton(HWND hwnd)
{
    const ThemeColors& theme = CurrentTheme();
    BufferedPaint paint(hwnd);
    FillSolid(paint.dc, paint.client, theme.windowBg);
    if (g_expand.hot || g_expand.pressed) {
        Gdiplus::Graphics graphics(paint.dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        FillRoundRect(graphics, paint.client, g_expand.pressed ? theme.pressed : theme.hover, Scale(kCornerRadius, g_bar.dpi));
    }
    SetBkMode(paint.dc, TRANSPARENT);
    SetTextColor(paint.dc, g_expand.hot ? theme.text : theme.secondaryText);
    HGDIOBJ oldFont = SelectObject(paint.dc, g_bar.chevronFont);
    DrawTextW(paint.dc, kChevronDown, -1, &paint.client, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(paint.dc, oldFont);
}

LRESULT CALLBACK ExpandButtonProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!g_expand.trackingLeave) {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            g_expand.trackingLeave = TrackMouseEvent(&tme) != FALSE;
        }
        if (!g_expand.hot) {
            g_expand.hot = true;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSELEAVE:
        g_expand.trackingLeave = false;
        g_expand.hot = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        // 포커스를 가져가지 않으므로 에디터의 캐럿과 선택이 유지된다
        g_expand.pressed = true;
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) {
            ReleaseCapture();
            RECT client;
            GetClientRect(hwnd, &client);
            if (PtInRect(&client, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)})) {
                SendMessageW(g_bar.owner, WM_COMMAND, IDM_VIEW_TOOLBAR, 0);
            }
        }
        return 0;

    case WM_CAPTURECHANGED:
        g_expand.pressed = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        PaintExpandButton(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND CreateTopBar(HWND owner, HINSTANCE instance, HMENU menus)
{
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW;
    wc.lpfnWndProc = TopBarProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kBarClass;
    RegisterClassExW(&wc);

    // 색상 팝업: 메뉴처럼 그림자를 갖는 작은 팝업 창
    WNDCLASSEXW popup = {sizeof(popup)};
    popup.style = CS_DROPSHADOW;
    popup.lpfnWndProc = PopupProc;
    popup.hInstance = instance;
    popup.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    popup.lpszClassName = kPopupClass;
    RegisterClassExW(&popup);

    // 접힌 상태의 펼치기 화살표
    WNDCLASSEXW expand = {sizeof(expand)};
    expand.lpfnWndProc = ExpandButtonProc;
    expand.hInstance = instance;
    expand.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    expand.lpszClassName = kExpandClass;
    RegisterClassExW(&expand);

    g_bar.instance = instance;
    g_bar.owner = owner;
    g_bar.menus = menus;
    g_bar.format.color = DefaultTextColor();
    g_bar.hwnd = CreateWindowExW(0, kBarClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                 0, 0, 0, 0, owner, nullptr, instance, nullptr);
    if (!g_bar.hwnd) return nullptr;
    CreateTooltips();

    // 펼치기 화살표는 바가 아니라 메인 창의 자식이다 (바가 숨겨진 동안 에디터 위에 겹쳐 보여야 하므로)
    g_expand.hwnd = CreateWindowExW(0, kExpandClass, L"", WS_CHILD | WS_CLIPSIBLINGS,
                                    0, 0, 0, 0, owner, nullptr, instance, nullptr);
    if (g_expand.hwnd) {
        g_expand.tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                           CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                           g_expand.hwnd, nullptr, instance, nullptr);
        if (g_expand.tooltip) {
            TTTOOLINFOW ti = {sizeof(ti)};
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd = g_expand.hwnd;
            ti.uId = reinterpret_cast<UINT_PTR>(g_expand.hwnd);
            ti.lpszText = const_cast<LPWSTR>(L"툴바 표시 (Ctrl+\\)");
            SendMessageW(g_expand.tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        }
    }
    SetTopBarDpi(g_bar.hwnd, GetDpiForWindow(owner));
    return g_bar.hwnd;
}

int TopBarHeight(UINT dpi)
{
    return Scale(kBarHeight, dpi);
}

void SetTopBarDpi(HWND bar, UINT dpi)
{
    g_bar.dpi = dpi;
    CreateBarFonts();
    LayoutItems();
    InvalidateRect(bar, nullptr, FALSE);
}

void OpenTopBarMenu(HWND, int index)
{
    OpenMenu(index, true);
}

void SetTopBarFormatState(HWND bar, const FormatState& state)
{
    const FormatState& old = g_bar.format;
    if (old.bold == state.bold && old.italic == state.italic && old.underline == state.underline &&
        old.colorMixed == state.colorMixed && old.color == state.color) {
        return;  // 캐럿 이동마다 다시 그리지 않도록 바뀐 경우에만 갱신한다
    }
    g_bar.format = state;
    InvalidateRect(bar, nullptr, FALSE);
}

bool IsColorPopupOpen()
{
    return g_popup.hwnd != nullptr && !g_popup.closing;
}

bool IsTopBarBusy()
{
    // 메뉴가 열려 있는 동안에는 g_bar.pressed가 메뉴 항목을 가리킨다
    return g_bar.pressed >= 0 || IsColorPopupOpen();
}

void SetTopBarCollapsed(HWND bar, bool collapsed)
{
    g_bar.collapsed = collapsed;
    // 접힌 채 잠깐 보이는 바에서는 화살표가 아래를 향하고 "다시 펼쳐 고정"하는 버튼이 된다
    BarItem& item = g_bar.items[kCollapseItem];
    item.label = collapsed ? kChevronDown : kChevronUp;
    item.tooltip = collapsed ? L"툴바 표시 (Ctrl+\\)" : L"툴바 숨기기 (Ctrl+\\)";
    if (g_bar.tooltip) {
        TTTOOLINFOW ti = {sizeof(ti)};
        ti.hwnd = g_bar.hwnd;
        ti.uId = kCollapseItem;
        ti.lpszText = const_cast<LPWSTR>(item.tooltip);
        SendMessageW(g_bar.tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
    }
    InvalidateRect(bar, nullptr, FALSE);
}

HWND ExpandButtonWindow()
{
    return g_expand.hwnd;
}

int ExpandButtonSize(UINT dpi)
{
    return Scale(kButtonHeight, dpi);
}

int ExpandButtonMargin(UINT dpi)
{
    // 펼친 상태의 접기 화살표와 같은 위치에 오도록 바의 여백을 그대로 쓴다
    return Scale(kRightMargin, dpi);
}

int ExpandButtonTop(UINT dpi)
{
    return (Scale(kBarHeight, dpi) - Scale(kButtonHeight, dpi)) / 2;
}
