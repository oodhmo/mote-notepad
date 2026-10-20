#include "topbar.h"
#include "settings.h"
#include "theme.h"
#include "../resources/resource.h"
#include "strings.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <iterator>
#include <vector>
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
constexpr int kBadgeHeight = 11;       // 색 아래 단축키 표시(F1/F2/F3) 줄
constexpr int kBadgeFontPx = 10;
constexpr int kCaptionHeight = 16;     // "최근 사용한 색" 제목 줄
constexpr DWORD kReopenGuardMs = 200;  // 팝업을 닫은 클릭이 곧바로 다시 여는 것을 막는 시간

// 팝업 항목 번호: 기본 팔레트(0~7) → 최근 사용한 색(8~15) → Custom color…
constexpr int kPaletteCount = 8;
constexpr int kRecentFirst = kPaletteCount;
constexpr int kCustomItem = kRecentFirst + static_cast<int>(kMaxRecentColors);
constexpr int kPopupItemCount = kCustomItem + 1;

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

enum { kFileItem, kEditItem, kHelpItem, kBoldItem, kItalicItem, kUnderlineItem, kColorItem, kCollapseItem, kItemCount };

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
        {ItemKind::Menu, Str(StrId::MenuFile), 0, nullptr, {}},
        {ItemKind::Menu, Str(StrId::MenuEdit), 0, nullptr, {}},
        {ItemKind::Menu, Str(StrId::MenuHelp), 0, nullptr, {}},
        {ItemKind::Toggle, L"", IDM_FORMAT_BOLD, Str(StrId::TipBold), {}},
        {ItemKind::Toggle, L"", IDM_FORMAT_ITALIC, Str(StrId::TipItalic), {}},
        {ItemKind::Toggle, L"", IDM_FORMAT_UNDERLINE, Str(StrId::TipUnderline), {}},
        {ItemKind::Color, L"", 0, Str(StrId::TipTextColor), {}},
        {ItemKind::Button, kChevronUp, IDM_VIEW_TOOLBAR, Str(StrId::TipHideToolbar), {}},
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
    HFONT badgeFont = nullptr;
    // Ctrl+F1~F3에 지정된 색과 최근 사용한 색 (팝업을 열 때 레지스트리에서 다시 읽는다)
    ColorShortcuts shortcuts = {};
    std::vector<COLORREF> recent;
    RECT cells[kPopupItemCount] = {};  // 쓰지 않는 최근 색 칸은 빈 사각형
    RECT recentCaption = {};           // 최근 색이 없으면 빈 사각형
    RECT separators[2] = {};
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

int RecentCount()
{
    return static_cast<int>(g_popup.recent.size());
}

bool IsRecentItem(int index)
{
    return index >= kRecentFirst && index < kRecentFirst + RecentCount();
}

// 색 견본 항목인지 (팔레트 또는 최근 사용한 색)
bool IsColorItem(int index)
{
    return (index >= 0 && index < kPaletteCount) || IsRecentItem(index);
}

COLORREF ItemColor(int index)
{
    return index < kPaletteCount ? PaletteColor(index) : g_popup.recent[index - kRecentFirst];
}

// 색 견본 항목을 단축키에 저장하는 값으로 나타낸다
ColorShortcut ItemShortcut(int index)
{
    if (index < kPaletteCount) return {index};
    return {-1, ItemColor(index)};
}

bool IsSelectedSwatch(int index)
{
    return !g_bar.format.colorMixed && g_bar.format.color == ItemColor(index);
}

// index번 색 견본이 지정된 단축키 슬롯 (없으면 -1)
int ShortcutSlotOf(int index)
{
    const ColorShortcut value = ItemShortcut(index);
    for (int slot = 0; slot < kColorShortcutCount; ++slot) {
        if (g_popup.shortcuts[slot] == value) return slot;
    }
    return -1;
}

COLORREF ColorOf(const ColorShortcut& shortcut)
{
    return shortcut.IsCustom() ? shortcut.custom : PaletteColor(shortcut.palette);
}

bool IsPaletteColor(COLORREF color)
{
    for (int i = 0; i < kPaletteCount; ++i) {
        if (PaletteColor(i) == color) return true;
    }
    return false;
}

// 단축키에 지정된 사용자 지정 색인지 (최근 색 목록에서 밀려나지 않는다)
bool IsShortcutColor(const ColorShortcuts& shortcuts, COLORREF color)
{
    return std::ranges::any_of(shortcuts, [&](const ColorShortcut& s) { return s.IsCustom() && s.custom == color; });
}

// 최근 색 목록을 최대 개수로 줄인다. 단축키에 지정된 색은 남기고 오래된 색부터 뺀다.
void TrimRecentColors(std::vector<COLORREF>& recent, const ColorShortcuts& shortcuts)
{
    for (size_t i = recent.size(); i-- > 0 && recent.size() > kMaxRecentColors;) {
        if (!IsShortcutColor(shortcuts, recent[i])) recent.erase(recent.begin() + static_cast<ptrdiff_t>(i));
    }
}

// 팝업에 보여 줄 최근 색. 단축키에 지정된 사용자 지정 색은 목록에 없더라도 함께 보여 준다.
std::vector<COLORREF> LoadPopupRecentColors(const ColorShortcuts& shortcuts)
{
    std::vector<COLORREF> recent = LoadRecentColors();
    // 테마가 바뀌면 예전 색이 팔레트 기본색과 같아질 수 있어 겹치지 않게 뺀다
    std::erase_if(recent, IsPaletteColor);
    for (const ColorShortcut& shortcut : shortcuts) {
        if (shortcut.IsCustom() && std::ranges::find(recent, shortcut.custom) == recent.end()) {
            recent.push_back(shortcut.custom);
        }
    }
    TrimRecentColors(recent, shortcuts);
    return recent;
}

// slot에 value를 지정한다. 같은 색이 이미 다른 슬롯에 있으면 두 슬롯의 색을 맞바꾼다 (같은 색이 두 단축키에 겹치지 않게)
void AssignShortcut(ColorShortcuts& shortcuts, int slot, const ColorShortcut& value)
{
    for (int other = 0; other < kColorShortcutCount; ++other) {
        if (other != slot && shortcuts[other] == value) shortcuts[other] = shortcuts[slot];
    }
    shortcuts[slot] = value;
    SaveColorShortcuts(shortcuts);
}

// 원형 견본이 들어가는 칸의 윗부분 (아래에는 단축키 표시가 붙는다)
RECT SwatchArea(const RECT& cell, UINT dpi)
{
    return {cell.left, cell.top, cell.right, cell.bottom - Scale(kBadgeHeight, dpi)};
}

SIZE LayoutPopup(UINT dpi)
{
    const int pad = Scale(kPopupPadding, dpi);
    const int cell = Scale(kSwatchCell, dpi);
    const int cellHeight = cell + Scale(kBadgeHeight, dpi);
    const int width = pad * 2 + kPaletteCount * cell;
    const int gap = Scale(6, dpi);
    for (int i = 0; i < kPaletteCount; ++i) {
        g_popup.cells[i] = {pad + i * cell, pad, pad + (i + 1) * cell, pad + cellHeight};
    }
    int y = pad + cellHeight + gap;
    g_popup.separators[0] = {pad, y, width - pad, y + 1};
    y += 1;

    // 최근 사용한 색 줄: 제목 + 견본. 최근 색이 없으면 줄 자체를 만들지 않는다.
    for (int i = kRecentFirst; i < kCustomItem; ++i) g_popup.cells[i] = {};
    g_popup.recentCaption = {};
    g_popup.separators[1] = {};
    if (RecentCount() > 0) {
        y += Scale(4, dpi);
        g_popup.recentCaption = {pad + Scale(4, dpi), y, width - pad, y + Scale(kCaptionHeight, dpi)};
        y = g_popup.recentCaption.bottom;
        for (int i = 0; i < RecentCount(); ++i) {
            g_popup.cells[kRecentFirst + i] = {pad + i * cell, y, pad + (i + 1) * cell, y + cellHeight};
        }
        y += cellHeight + gap;
        g_popup.separators[1] = {pad, y, width - pad, y + 1};
        y += 1;
    }

    const int rowTop = y + Scale(4, dpi);
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
    for (const RECT& separator : g_popup.separators) FillSolid(paint.dc, separator, theme.border);

    {
        Gdiplus::Graphics graphics(paint.dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        if (g_popup.drawBorder) {
            Gdiplus::Pen pen(ToGdiplus(theme.border), 1.0f);
            graphics.DrawRectangle(&pen, 0.0f, 0.0f, paint.client.right - 1.0f, paint.client.bottom - 1.0f);
        }

        for (int i = 0; i < kCustomItem; ++i) {
            if (!IsColorItem(i)) continue;
            const RECT& cell = g_popup.cells[i];
            if (i == g_popup.hot) {
                RECT inner = cell;
                InflateRect(&inner, -Scale(1, dpi), -Scale(1, dpi));
                FillRoundRect(graphics, inner, theme.hover, Scale(kCornerRadius, dpi));
            }
            const COLORREF color = ItemColor(i);
            const RECT swatch = SwatchArea(cell, dpi);
            if (IsSelectedSwatch(i)) {
                // 현재 색은 바깥 고리로 표시한다
                Gdiplus::Pen ring(ToGdiplus(color), static_cast<Gdiplus::REAL>(Scale(2, dpi)));
                graphics.DrawEllipse(&ring, CircleRect(swatch, Scale(kSwatchSize + 6, dpi)));
            }
            Gdiplus::SolidBrush brush(ToGdiplus(color));
            graphics.FillEllipse(&brush, CircleRect(swatch, Scale(kSwatchSize, dpi)));
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

    SetBkMode(paint.dc, TRANSPARENT);

    // 단축키가 지정된 색 아래에 "F1"처럼 작게 표시한다
    HGDIOBJ oldFont = SelectObject(paint.dc, g_popup.badgeFont);
    SetTextColor(paint.dc, theme.secondaryText);
    for (int i = 0; i < kCustomItem; ++i) {
        if (!IsColorItem(i)) continue;
        const int slot = ShortcutSlotOf(i);
        if (slot < 0) continue;
        const wchar_t badge[] = {L'F', static_cast<wchar_t>(L'1' + slot), L'\0'};
        RECT badgeRect = g_popup.cells[i];
        badgeRect.top = badgeRect.bottom - Scale(kBadgeHeight + 2, dpi);
        DrawTextW(paint.dc, badge, -1, &badgeRect, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (RecentCount() > 0) {
        DrawTextW(paint.dc, Str(StrId::RecentColors), -1, &g_popup.recentCaption,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    const RECT& row = g_popup.cells[kCustomItem];
    RECT text = {row.left + Scale(6 + kSwatchCell + 2, dpi), row.top, row.right, row.bottom};
    SelectObject(paint.dc, g_popup.font);
    SetTextColor(paint.dc, theme.text);
    DrawTextW(paint.dc, Str(StrId::CustomColor), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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
    if (index == kCustomItem) {
        CloseColorPopup();
        PostMessageW(g_bar.owner, WM_COMMAND, IDM_FORMAT_CUSTOMCOLOR, 0);
    } else if (IsColorItem(index)) {
        const COLORREF color = ItemColor(index);
        CloseColorPopup();
        PostMessageW(g_bar.owner, WM_MOTE_SETCOLOR, 0, static_cast<LPARAM>(color));
    }
}

// index번 색 견본을 단축키 슬롯에 지정하는 작은 메뉴를 연다 (우클릭, Shift+F10, 메뉴 키)
void ShowShortcutMenu(int index, POINT screenPt)
{
    if (!IsColorItem(index)) return;
    const ColorShortcut value = ItemShortcut(index);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (int slot = 0; slot < kColorShortcutCount; ++slot) {
        wchar_t label[64];
        swprintf_s(label, Str(StrId::AssignColorShortcut), slot + 1);
        const UINT checked = g_popup.shortcuts[slot] == value ? MF_CHECKED : MF_UNCHECKED;
        AppendMenuW(menu, MF_STRING | checked, static_cast<UINT_PTR>(slot + 1), label);
    }
    const int chosen = static_cast<int>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
                                                         screenPt.x, screenPt.y, g_popup.hwnd, nullptr));
    DestroyMenu(menu);
    if (chosen <= 0 || !g_popup.hwnd) return;

    AssignShortcut(g_popup.shortcuts, chosen - 1, value);
    InvalidateRect(g_popup.hwnd, nullptr, FALSE);
}

// 방향키로 항목 사이를 이동한다 (팔레트 줄, 최근 색 줄, 아래 Custom color 행)
void MovePopupFocus(WPARAM key)
{
    int hot = g_popup.hot;
    if (hot < 0) {
        hot = 0;
        for (int i = 0; i < kCustomItem; ++i) {
            if (IsColorItem(i) && IsSelectedSwatch(i)) hot = i;
        }
        SetPopupHot(hot);
        return;
    }
    const bool inPalette = hot < kPaletteCount;
    const bool inRecent = IsRecentItem(hot);
    const int rowFirst = inPalette ? 0 : kRecentFirst;
    const int rowLast = inPalette ? kPaletteCount - 1 : kRecentFirst + RecentCount() - 1;
    switch (key) {
    case VK_LEFT:  if (inPalette || inRecent) hot = std::max(rowFirst, hot - 1); break;
    case VK_RIGHT: if (inPalette || inRecent) hot = std::min(rowLast, hot + 1); break;
    case VK_DOWN:
        if (inPalette && RecentCount() > 0) hot = kRecentFirst + std::min(hot, RecentCount() - 1);
        else hot = kCustomItem;
        break;
    case VK_UP:
        if (hot == kCustomItem) hot = RecentCount() > 0 ? kRecentFirst : 0;
        else if (inRecent) hot -= kRecentFirst;  // 같은 열의 팔레트 색
        break;
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
        // 이미 닫혀 다른 팝업으로 바뀐 뒤에 도착한 WM_CLOSE면 이 창만 없앤다
        if (hwnd != g_popup.hwnd) {
            DestroyWindow(hwnd);
            return 0;
        }
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

    case WM_CONTEXTMENU: {
        // 우클릭이면 커서 위치의 색, Shift+F10/메뉴 키면(lParam = -1) 키보드로 가리킨 색
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int index = g_popup.hot;
        if (lp == -1) {
            if (!IsColorItem(index)) return 0;
            pt = {g_popup.cells[index].left, g_popup.cells[index].bottom};
            ClientToScreen(hwnd, &pt);
        } else {
            POINT client = pt;
            ScreenToClient(hwnd, &client);
            index = HitTestRects(g_popup.cells, kCustomItem + 1, client);
        }
        ShowShortcutMenu(index, pt);
        return 0;
    }

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
        if (hwnd != g_popup.hwnd) return 0;  // 글꼴은 지금 떠 있는 팝업의 것이다
        if (g_popup.font) {
            DeleteObject(g_popup.font);
            g_popup.font = nullptr;
        }
        if (g_popup.badgeFont) {
            DeleteObject(g_popup.badgeFont);
            g_popup.badgeFont = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ShowColorPopup(const RECT& anchor)
{
    if (g_popup.hwnd) return;  // 팝업은 하나만 띄운다
    const UINT dpi = g_bar.dpi;
    // 다른 mote 창에서 바꾼 지정과 최근 색도 반영하도록 열 때마다 다시 읽는다 (최근 색 수에 따라 팝업 높이가 바뀐다)
    g_popup.shortcuts = LoadColorShortcuts(kPaletteCount);
    g_popup.recent = LoadPopupRecentColors(g_popup.shortcuts);
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
    g_popup.badgeFont = CreateUiFontPx(kBadgeFontPx, dpi);
    g_popup.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kPopupClass, L"", WS_POPUP,
                                   x, y, size.cx, size.cy, g_bar.owner, nullptr, g_bar.instance, nullptr);
    if (!g_popup.hwnd) {
        DeleteObject(g_popup.font);
        DeleteObject(g_popup.badgeFont);
        g_popup.font = nullptr;
        g_popup.badgeFont = nullptr;
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
    for (int i : {kFileItem, kEditItem, kHelpItem}) {
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
    if (index != kFileItem && index != kEditItem && index != kHelpItem) return;
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
        // 팝업이 열려 있으면 닫기만 한다. 비활성화로 게시된 WM_CLOSE보다 이 클릭이 먼저 처리될 수 있다.
        if (g_popup.hwnd) {
            CloseColorPopup();
            break;
        }
        // 팝업이 먼저 닫힌 뒤에 도착한, 닫으려고 누른 클릭이면 다시 열지 않는다
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
            ti.lpszText = const_cast<LPWSTR>(Str(StrId::TipShowToolbar));
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
    item.tooltip = Str(collapsed ? StrId::TipShowToolbar : StrId::TipHideToolbar);
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

COLORREF ShortcutColor(int slot)
{
    if (slot < 0 || slot >= kColorShortcutCount) return DefaultTextColor();
    return ColorOf(LoadColorShortcuts(kPaletteCount)[slot]);
}

void AddRecentColor(COLORREF color)
{
    if (IsPaletteColor(color)) return;  // 팔레트 색은 이미 위 줄에 있다
    std::vector<COLORREF> recent = LoadRecentColors();
    std::erase(recent, color);
    recent.insert(recent.begin(), color);
    TrimRecentColors(recent, LoadColorShortcuts(kPaletteCount));
    SaveRecentColors(recent);
}
