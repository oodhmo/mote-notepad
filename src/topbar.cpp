#include "topbar.h"
#include "theme.h"

#include <windowsx.h>
#include <algorithm>
#include <iterator>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

namespace {

constexpr wchar_t kClassName[] = L"MoteTopBar";

// 96 DPI 기준 치수
constexpr int kBarHeight = 40;
constexpr int kButtonHeight = 28;
constexpr int kMenuPaddingX = 10;
constexpr int kEdgeMargin = 8;
constexpr int kItemGap = 2;
constexpr int kCornerRadius = 6;
constexpr int kFontPointTenths = 100;  // 10pt: 본문(11.5pt)보다 약간 작게

struct MenuItem {
    const wchar_t* label;
    RECT rect;
};

struct TopBarState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    HMENU menus = nullptr;
    UINT dpi = 96;
    HFONT font = nullptr;
    MenuItem items[2] = {{L"File", {}}, {L"Edit", {}}};
    int hot = -1;       // 마우스가 올라간 항목
    int pressed = -1;   // 팝업이 열려 있는 항목
    bool trackingLeave = false;
};

TopBarState g_bar;

void LayoutItems()
{
    const int barHeight = Scale(kBarHeight, g_bar.dpi);
    const int buttonHeight = Scale(kButtonHeight, g_bar.dpi);
    const int top = (barHeight - buttonHeight) / 2;

    HDC dc = GetDC(g_bar.hwnd);
    HGDIOBJ oldFont = SelectObject(dc, g_bar.font);
    int x = Scale(kEdgeMargin, g_bar.dpi);
    for (MenuItem& item : g_bar.items) {
        SIZE size = {};
        GetTextExtentPoint32W(dc, item.label, lstrlenW(item.label), &size);
        const int width = size.cx + Scale(kMenuPaddingX, g_bar.dpi) * 2;
        item.rect = {x, top, x + width, top + buttonHeight};
        x += width + Scale(kItemGap, g_bar.dpi);
    }
    SelectObject(dc, oldFont);
    ReleaseDC(g_bar.hwnd, dc);
}

int HitTest(POINT pt)
{
    for (int i = 0; i < static_cast<int>(std::size(g_bar.items)); ++i) {
        if (PtInRect(&g_bar.items[i].rect, pt)) return i;
    }
    return -1;
}

Gdiplus::Color ToGdiplus(COLORREF color)
{
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

// GDI의 RoundRect는 안티앨리어싱이 없어 모서리가 거칠어 GDI+로 그린다
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
    Gdiplus::SolidBrush brush(ToGdiplus(color));
    graphics.FillPath(&brush, &path);
}

void Paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    const int width = client.right, height = client.bottom;
    const ThemeColors& theme = CurrentTheme();

    // 깜빡임을 막기 위해 메모리 비트맵에 그린 뒤 한 번에 복사한다
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
    HGDIOBJ oldBitmap = SelectObject(mem, bitmap);

    HBRUSH background = CreateSolidBrush(theme.windowBg);
    FillRect(mem, &client, background);
    DeleteObject(background);

    // 편집 영역과의 경계는 1px의 아주 옅은 선으로만 표시한다
    RECT line = {0, height - 1, width, height};
    HBRUSH border = CreateSolidBrush(theme.border);
    FillRect(mem, &line, border);
    DeleteObject(border);

    {
        Gdiplus::Graphics graphics(mem);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        for (int i = 0; i < static_cast<int>(std::size(g_bar.items)); ++i) {
            if (i == g_bar.pressed) {
                FillRoundRect(graphics, g_bar.items[i].rect, theme.pressed, Scale(kCornerRadius, g_bar.dpi));
            } else if (i == g_bar.hot) {
                FillRoundRect(graphics, g_bar.items[i].rect, theme.hover, Scale(kCornerRadius, g_bar.dpi));
            }
        }
    }

    HGDIOBJ oldFont = SelectObject(mem, g_bar.font);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, theme.text);
    for (MenuItem& item : g_bar.items) {
        DrawTextW(mem, item.label, -1, &item.rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(mem, oldFont);

    BitBlt(dc, 0, 0, width, height, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void SetHot(int index)
{
    if (g_bar.hot == index) return;
    g_bar.hot = index;
    InvalidateRect(g_bar.hwnd, nullptr, FALSE);
}

void OpenMenu(int index, bool fromKeyboard)
{
    if (index < 0 || index >= static_cast<int>(std::size(g_bar.items))) return;
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

LRESULT CALLBACK TopBarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
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

    case WM_LBUTTONDOWN: {
        // 바는 포커스를 가져가지 않는다 → 에디터의 선택 영역과 캐럿이 유지된다
        const int index = HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (index >= 0) OpenMenu(index, false);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;  // WM_PAINT에서 전체를 그린다

    case WM_PAINT:
        Paint(hwnd);
        return 0;

    case WM_DESTROY:
        if (g_bar.font) {
            DeleteObject(g_bar.font);
            g_bar.font = nullptr;
        }
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
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    g_bar.owner = owner;
    g_bar.menus = menus;
    g_bar.hwnd = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                 0, 0, 0, 0, owner, nullptr, instance, nullptr);
    if (g_bar.hwnd) SetTopBarDpi(g_bar.hwnd, GetDpiForWindow(owner));
    return g_bar.hwnd;
}

int TopBarHeight(UINT dpi)
{
    return Scale(kBarHeight, dpi);
}

void SetTopBarDpi(HWND bar, UINT dpi)
{
    g_bar.dpi = dpi;
    if (g_bar.font) DeleteObject(g_bar.font);
    g_bar.font = CreateUiFont(kFontPointTenths, dpi);
    LayoutItems();
    InvalidateRect(bar, nullptr, FALSE);
}

void OpenTopBarMenu(HWND, int index)
{
    OpenMenu(index, true);
}
