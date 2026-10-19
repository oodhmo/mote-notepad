#include "confirm.h"
#include "theme.h"
#include "strings.h"

#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

namespace {

constexpr wchar_t kConfirmClass[] = L"MoteConfirm";

// ---- 치수 (96 DPI 기준) ----
constexpr int kWidth = 360;
constexpr int kPadding = 24;
constexpr int kBottomPadding = 20;
constexpr int kTitleGap = 6;          // 제목과 파일 이름 사이
constexpr int kButtonsGap = 24;       // 파일 이름과 버튼 줄 사이
constexpr int kButtonHeight = 32;
constexpr int kButtonPadX = 16;
constexpr int kMinButtonWidth = 72;
constexpr int kButtonGap = 8;
constexpr int kCornerRadius = 6;
// 글자 크기 (96 DPI 기준 px)
// 기본 UI 글꼴을 쓴다 (한국어 화면이면 맑은 고딕, CreateUiFontPx 참고)
constexpr int kTitleFontPx = 15;   // 제목, 굵게
constexpr int kBodyFontPx = 13;    // 파일 이름
constexpr int kButtonFontPx = 13;  // 버튼 (파일 이름과 같은 크기)


// 화면 왼쪽부터의 순서
enum { kCancelButton, kDontSaveButton, kSaveButton, kButtonCount };

struct ConfirmState {
    HWND hwnd = nullptr;
    UINT dpi = 96;
    HFONT titleFont = nullptr;
    HFONT bodyFont = nullptr;
    HFONT buttonFont = nullptr;
    std::wstring documentName;
    const wchar_t* labels[kButtonCount] = {Str(StrId::Cancel), Str(StrId::DontSave), Str(StrId::Save)};
    RECT buttons[kButtonCount] = {};
    RECT titleRect = {};
    RECT bodyRect = {};
    int hot = -1;
    int pressed = -1;
    int focus = kSaveButton;
    bool keyboardCues = false;  // 키보드로 버튼을 옮긴 뒤에만 포커스 링을 보여 준다
    bool trackingLeave = false;
    bool drawBorder = false;    // DWM 둥근 모서리를 못 쓰는 환경(Windows 10)에서는 직접 테두리를 그린다
    bool done = false;
    SaveChoice result = SaveChoice::Cancel;
};

ConfirmState g_confirm;

SaveChoice ChoiceOf(int button)
{
    switch (button) {
    case kSaveButton:     return SaveChoice::Save;
    case kDontSaveButton: return SaveChoice::DontSave;
    default:              return SaveChoice::Cancel;
    }
}

void Finish(SaveChoice choice)
{
    g_confirm.result = choice;
    g_confirm.done = true;
}

int TextHeight(HDC dc, HFONT font)
{
    HGDIOBJ old = SelectObject(dc, font);
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    return tm.tmHeight;
}

// 창 크기를 계산하고 각 영역을 배치한다
SIZE Layout()
{
    const UINT dpi = g_confirm.dpi;
    const int width = Scale(kWidth, dpi);
    const int pad = Scale(kPadding, dpi);

    HDC dc = GetDC(nullptr);
    const int titleHeight = TextHeight(dc, g_confirm.titleFont);
    const int bodyHeight = TextHeight(dc, g_confirm.bodyFont);
    int buttonWidths[kButtonCount] = {};
    HGDIOBJ old = SelectObject(dc, g_confirm.buttonFont);
    for (int i = 0; i < kButtonCount; ++i) {
        SIZE size = {};
        GetTextExtentPoint32W(dc, g_confirm.labels[i], static_cast<int>(wcslen(g_confirm.labels[i])), &size);
        buttonWidths[i] = std::max(static_cast<int>(size.cx) + Scale(kButtonPadX, dpi) * 2, Scale(kMinButtonWidth, dpi));
    }
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);

    g_confirm.titleRect = {pad, pad, width - pad, pad + titleHeight};
    const int bodyTop = g_confirm.titleRect.bottom + Scale(kTitleGap, dpi);
    g_confirm.bodyRect = {pad, bodyTop, width - pad, bodyTop + bodyHeight};

    // 버튼은 오른쪽 정렬: [취소] [저장 안 함] [저장]
    const int buttonTop = g_confirm.bodyRect.bottom + Scale(kButtonsGap, dpi);
    const int buttonBottom = buttonTop + Scale(kButtonHeight, dpi);
    int right = width - pad;
    for (int i = kButtonCount - 1; i >= 0; --i) {
        g_confirm.buttons[i] = {right - buttonWidths[i], buttonTop, right, buttonBottom};
        right -= buttonWidths[i] + Scale(kButtonGap, dpi);
    }
    return {width, buttonBottom + Scale(kBottomPadding, dpi)};
}

int HitTest(POINT pt)
{
    for (int i = 0; i < kButtonCount; ++i) {
        if (PtInRect(&g_confirm.buttons[i], pt)) return i;
    }
    return -1;
}

void Paint(HWND hwnd)
{
    const ThemeColors& theme = CurrentTheme();
    const int radius = Scale(kCornerRadius, g_confirm.dpi);
    BufferedPaint paint(hwnd);
    FillSolid(paint.dc, paint.client, theme.windowBg);
    if (g_confirm.drawBorder) {
        HBRUSH border = CreateSolidBrush(theme.border);
        FrameRect(paint.dc, &paint.client, border);
        DeleteObject(border);
    }

    {
        Gdiplus::Graphics graphics(paint.dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        for (int i = 0; i < kButtonCount; ++i) {
            const RECT& rect = g_confirm.buttons[i];
            const bool pressed = i == g_confirm.pressed;
            const bool hot = i == g_confirm.hot;

            // 포커스 링: 버튼보다 조금 큰 둥근 사각형을 칠한 뒤 1px 안쪽을 배경색으로 지워 얇은 고리를 만든다
            if (g_confirm.keyboardCues && i == g_confirm.focus) {
                const int outer = std::max(2, Scale(2, g_confirm.dpi));
                RECT ring = rect;
                InflateRect(&ring, outer, outer);
                FillRoundRect(graphics, ring, theme.secondaryText, radius + outer);
                InflateRect(&ring, -1, -1);
                FillRoundRect(graphics, ring, theme.windowBg, radius + outer - 1);
            }

            if (i == kSaveButton) {
                FillRoundRect(graphics, rect, pressed ? theme.primaryPressed : hot ? theme.primaryHover : theme.primaryBg, radius);
            } else if (pressed) {
                FillRoundRect(graphics, rect, theme.pressed, radius);
            } else if (hot) {
                FillRoundRect(graphics, rect, theme.hover, radius);
            }
        }
    }

    SetBkMode(paint.dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(paint.dc, g_confirm.titleFont);
    SetTextColor(paint.dc, theme.text);
    DrawTextW(paint.dc, Str(StrId::SaveChangesTitle), -1, &g_confirm.titleRect,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    SelectObject(paint.dc, g_confirm.bodyFont);
    SetTextColor(paint.dc, theme.secondaryText);
    DrawTextW(paint.dc, g_confirm.documentName.c_str(), -1, &g_confirm.bodyRect,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_PATH_ELLIPSIS);

    SelectObject(paint.dc, g_confirm.buttonFont);
    for (int i = 0; i < kButtonCount; ++i) {
        SetTextColor(paint.dc, i == kSaveButton ? theme.primaryText : theme.text);
        DrawTextW(paint.dc, g_confirm.labels[i], -1, &g_confirm.buttons[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(paint.dc, oldFont);
}

void MoveFocus(int delta)
{
    g_confirm.focus = (g_confirm.focus + delta + kButtonCount) % kButtonCount;
    g_confirm.keyboardCues = true;
    InvalidateRect(g_confirm.hwnd, nullptr, FALSE);
}

LRESULT CALLBACK ConfirmProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN:
        switch (wp) {
        case VK_RETURN:
        case VK_SPACE:  Finish(ChoiceOf(g_confirm.focus)); return 0;
        case VK_ESCAPE: Finish(SaveChoice::Cancel); return 0;
        case VK_TAB:    MoveFocus(GetKeyState(VK_SHIFT) < 0 ? -1 : 1); return 0;
        case VK_LEFT:   MoveFocus(-1); return 0;
        case VK_RIGHT:  MoveFocus(1); return 0;
        }
        break;

    case WM_MOUSEMOVE: {
        if (!g_confirm.trackingLeave) {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            g_confirm.trackingLeave = TrackMouseEvent(&tme) != FALSE;
        }
        const int hot = HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (hot != g_confirm.hot) {
            g_confirm.hot = hot;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_confirm.trackingLeave = false;
        g_confirm.hot = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        // 일반 버튼처럼 눌렀다 뗄 때 실행한다. 버튼 밖에서 떼면 취소된다.
        const int index = HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (index >= 0) {
            g_confirm.pressed = index;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) {
            const int index = g_confirm.pressed;
            ReleaseCapture();
            if (index >= 0 && HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}) == index) Finish(ChoiceOf(index));
        }
        return 0;

    case WM_CAPTURECHANGED:
        if (g_confirm.pressed >= 0) {
            g_confirm.pressed = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_CLOSE:
        // Alt+F4는 취소로 처리한다 (창은 AskSaveChanges가 정리한다)
        Finish(SaveChoice::Cancel);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        Paint(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

SaveChoice AskSaveChanges(HWND owner, const std::wstring& documentName)
{
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.style = CS_DROPSHADOW;  // 메뉴/색상 팝업과 같은 옅은 그림자
        wc.lpfnWndProc = ConfirmProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kConfirmClass;
        registered = RegisterClassExW(&wc) != 0;
    }

    g_confirm = ConfirmState{};
    g_confirm.dpi = GetDpiForWindow(owner);
    g_confirm.documentName = documentName;
    g_confirm.titleFont = CreateUiFontPx(kTitleFontPx, g_confirm.dpi, FW_BOLD);
    g_confirm.bodyFont = CreateUiFontPx(kBodyFontPx, g_confirm.dpi);
    g_confirm.buttonFont = CreateUiFontPx(kButtonFontPx, g_confirm.dpi);
    const SIZE size = Layout();

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

    g_confirm.hwnd = CreateWindowExW(0, kConfirmClass, L"mote", WS_POPUP, x, y, size.cx, size.cy,
                                     owner, nullptr, instance, nullptr);
    if (!g_confirm.hwnd) {
        DeleteObject(g_confirm.titleFont);
        DeleteObject(g_confirm.bodyFont);
        DeleteObject(g_confirm.buttonFont);
        return SaveChoice::Cancel;
    }
    // Windows 11: 둥근 모서리 + 시스템 테두리. 실패하면(Windows 10) 직접 테두리를 그린다.
    const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    g_confirm.drawBorder = FAILED(DwmSetWindowAttribute(g_confirm.hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner)));

    // 모달: owner를 비활성화하고 창이 닫힐 때까지 자체 메시지 루프를 돈다
    EnableWindow(owner, FALSE);
    ShowWindow(g_confirm.hwnd, SW_SHOW);
    SetFocus(g_confirm.hwnd);

    MSG msg;
    while (!g_confirm.done) {
        const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0) {
            PostQuitMessage(static_cast<int>(msg.wParam));  // 바깥 루프도 끝나도록 WM_QUIT을 되돌려 놓는다
            g_confirm.result = SaveChoice::Cancel;
            break;
        }
        if (got == -1) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // owner를 먼저 다시 활성화해야 창을 없앨 때 활성화가 다른 앱으로 넘어가지 않는다
    EnableWindow(owner, TRUE);
    DestroyWindow(g_confirm.hwnd);
    g_confirm.hwnd = nullptr;
    DeleteObject(g_confirm.titleFont);
    DeleteObject(g_confirm.bodyFont);
    DeleteObject(g_confirm.buttonFont);
    return g_confirm.result;
}
