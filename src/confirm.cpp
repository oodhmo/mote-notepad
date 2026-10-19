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
constexpr int kMessageWidth = 420;   // 본문이 문장(여러 줄)인 확인창
constexpr int kPadding = 24;
constexpr int kBottomPadding = 20;
constexpr int kTitleGap = 6;          // 제목과 본문 사이
constexpr int kButtonsGap = 24;       // 본문과 버튼 줄 사이
constexpr int kButtonHeight = 32;
constexpr int kButtonPadX = 16;
constexpr int kMinButtonWidth = 72;
constexpr int kButtonGap = 8;
constexpr int kCornerRadius = 6;
// 글자 크기 (96 DPI 기준 px)
// 기본 UI 글꼴을 쓴다 (한국어 화면이면 맑은 고딕, CreateUiFontPx 참고)
constexpr int kTitleFontPx = 15;   // 제목, 굵게
constexpr int kBodyFontPx = 13;    // 본문 (파일 이름 또는 안내 문장)
constexpr int kButtonFontPx = 13;  // 버튼 (파일 이름과 같은 크기)

constexpr int kMaxButtons = 3;

struct ConfirmState {
    HWND hwnd = nullptr;
    UINT dpi = 96;
    HFONT titleFont = nullptr;
    HFONT bodyFont = nullptr;
    HFONT buttonFont = nullptr;
    std::wstring title;
    std::wstring body;
    bool bodyIsPath = false;                  // 파일 이름이면 한 줄로 줄이고, 문장이면 여러 줄로 감싼다
    const wchar_t* labels[kMaxButtons] = {};  // 화면 왼쪽부터의 순서. 첫 번째는 "취소", 마지막은 주 버튼
    int buttonCount = 0;
    RECT buttons[kMaxButtons] = {};
    RECT titleRect = {};
    RECT bodyRect = {};
    int hot = -1;
    int pressed = -1;
    int focus = 0;
    bool keyboardCues = false;  // 키보드로 버튼을 옮긴 뒤에만 포커스 링을 보여 준다
    bool trackingLeave = false;
    bool drawBorder = false;    // DWM 둥근 모서리를 못 쓰는 환경(Windows 10)에서는 직접 테두리를 그린다
    bool done = false;
    int result = 0;             // 누른 버튼 번호 (Esc/Alt+F4는 0 = 취소)
};

ConfirmState g_confirm;

int PrimaryButton() { return g_confirm.buttonCount - 1; }

void Finish(int button)
{
    g_confirm.result = button;
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
    const int width = Scale(g_confirm.bodyIsPath ? kWidth : kMessageWidth, dpi);
    const int pad = Scale(kPadding, dpi);

    HDC dc = GetDC(nullptr);
    const int titleHeight = TextHeight(dc, g_confirm.titleFont);
    int bodyHeight = TextHeight(dc, g_confirm.bodyFont);
    if (!g_confirm.bodyIsPath) {
        g_confirm.body = WrapAtSpaces(dc, g_confirm.bodyFont, g_confirm.body, width - pad * 2);
        RECT measure = {0, 0, width - pad * 2, 0};
        HGDIOBJ oldBody = SelectObject(dc, g_confirm.bodyFont);
        DrawTextW(dc, g_confirm.body.c_str(), -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, oldBody);
        bodyHeight = std::max(bodyHeight, static_cast<int>(measure.bottom));
    }
    int buttonWidths[kMaxButtons] = {};
    HGDIOBJ old = SelectObject(dc, g_confirm.buttonFont);
    for (int i = 0; i < g_confirm.buttonCount; ++i) {
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
    for (int i = g_confirm.buttonCount - 1; i >= 0; --i) {
        g_confirm.buttons[i] = {right - buttonWidths[i], buttonTop, right, buttonBottom};
        right -= buttonWidths[i] + Scale(kButtonGap, dpi);
    }
    return {width, buttonBottom + Scale(kBottomPadding, dpi)};
}

int HitTest(POINT pt)
{
    for (int i = 0; i < g_confirm.buttonCount; ++i) {
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
        for (int i = 0; i < g_confirm.buttonCount; ++i) {
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

            if (i == PrimaryButton()) {
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
    DrawTextW(paint.dc, g_confirm.title.c_str(), -1, &g_confirm.titleRect,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    SelectObject(paint.dc, g_confirm.bodyFont);
    SetTextColor(paint.dc, theme.secondaryText);
    DrawTextW(paint.dc, g_confirm.body.c_str(), -1, &g_confirm.bodyRect,
              DT_LEFT | DT_NOPREFIX | (g_confirm.bodyIsPath ? DT_SINGLELINE | DT_PATH_ELLIPSIS : DT_WORDBREAK));

    SelectObject(paint.dc, g_confirm.buttonFont);
    for (int i = 0; i < g_confirm.buttonCount; ++i) {
        SetTextColor(paint.dc, i == PrimaryButton() ? theme.primaryText : theme.text);
        DrawTextW(paint.dc, g_confirm.labels[i], -1, &g_confirm.buttons[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(paint.dc, oldFont);
}

void MoveFocus(int delta)
{
    g_confirm.focus = (g_confirm.focus + delta + g_confirm.buttonCount) % g_confirm.buttonCount;
    g_confirm.keyboardCues = true;
    InvalidateRect(g_confirm.hwnd, nullptr, FALSE);
}

LRESULT CALLBACK ConfirmProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN:
        switch (wp) {
        case VK_RETURN:
        case VK_SPACE:  Finish(g_confirm.focus); return 0;
        case VK_ESCAPE: Finish(0); return 0;
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
            if (index >= 0 && HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}) == index) Finish(index);
        }
        return 0;

    case WM_CAPTURECHANGED:
        if (g_confirm.pressed >= 0) {
            g_confirm.pressed = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_CLOSE:
        // Alt+F4는 취소로 처리한다 (창은 ShowChoice가 정리한다)
        Finish(0);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        Paint(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// 제목, 본문, 버튼(왼쪽부터. 첫 번째는 취소, 마지막은 주 버튼)을 보여 주고 누른 버튼 번호를 돌려준다
int ShowChoice(HWND owner, const std::wstring& title, const std::wstring& body, bool bodyIsPath,
               std::initializer_list<const wchar_t*> labels)
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
    g_confirm.title = title;
    g_confirm.body = body;
    g_confirm.bodyIsPath = bodyIsPath;
    for (const wchar_t* label : labels) {
        if (g_confirm.buttonCount < kMaxButtons) g_confirm.labels[g_confirm.buttonCount++] = label;
    }
    g_confirm.focus = PrimaryButton();
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
        return 0;
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
            g_confirm.result = 0;
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

}  // namespace

SaveChoice AskSaveChanges(HWND owner, const std::wstring& documentName)
{
    switch (ShowChoice(owner, Str(StrId::SaveChangesTitle), documentName, true,
                       {Str(StrId::Cancel), Str(StrId::DontSave), Str(StrId::Save)})) {
    case 2:  return SaveChoice::Save;
    case 1:  return SaveChoice::DontSave;
    default: return SaveChoice::Cancel;
    }
}

bool AskConfirm(HWND owner, const std::wstring& title, const std::wstring& message, const wchar_t* okLabel,
                const wchar_t* cancelLabel)
{
    return ShowChoice(owner, title, message, false, {cancelLabel ? cancelLabel : Str(StrId::Cancel), okLabel}) == 1;
}
