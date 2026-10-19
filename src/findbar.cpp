#include "findbar.h"
#include "editor.h"
#include "theme.h"
#include "strings.h"

#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <iterator>
#include <string>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

namespace {

constexpr wchar_t kFindBarClass[] = L"MoteFindBar";

// ---- 치수 (96 DPI 기준) ----
constexpr int kPadY = 4;              // 바 위/아래 여백
constexpr int kRowHeight = 36;
constexpr int kFieldHeight = 28;
constexpr int kFieldWidth = 280;      // 입력란 기본 폭 (창이 좁으면 줄어든다)
constexpr int kMinFieldWidth = 80;
constexpr int kFieldPadX = 10;        // 입력란 둥근 배경 안쪽 여백
constexpr int kEdgeMargin = 12;
constexpr int kIconButtonWidth = 32;
constexpr int kTextButtonPadX = 12;
constexpr int kGap = 4;
constexpr int kFieldGap = 8;          // 입력란과 버튼 사이
constexpr int kCornerRadius = 6;
constexpr int kFontTenths = 100;      // 10pt: 상단 바 메뉴와 같은 크기
constexpr int kIconPixelSize = 12;
constexpr size_t kMaxPrefillLength = 200;
constexpr int kMaxCount = 9999;           // 이 개수를 넘으면 세기를 멈추고 "9999+"로 표시
constexpr UINT_PTR kRecountTimerId = 1;
constexpr UINT kRecountDelayMs = 150;     // 본문 편집이 이어지는 동안에는 다시 세지 않고 기다린다


// Segoe Fluent Icons / MDL2 Assets
constexpr wchar_t kIconUp[] = L"";
constexpr wchar_t kIconDown[] = L"";
constexpr wchar_t kIconClose[] = L"";
constexpr wchar_t kIconReplace[] = L"\xE8AB";  // ⇄ (Switch)

enum { kPrevButton, kNextButton, kToggleReplaceButton, kCloseButton, kReplaceButton, kReplaceAllButton, kButtonCount };

struct Button {
    const wchar_t* label;
    bool icon;
    const wchar_t* tooltip;
    RECT rect;
};

struct FindBarState {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    HWND editor = nullptr;
    HWND tooltip = nullptr;
    HWND findEdit = nullptr;
    HWND replaceEdit = nullptr;
    UINT dpi = 96;
    HFONT font = nullptr;
    HFONT iconFont = nullptr;
    bool visible = false;
    bool replaceMode = false;
    bool hasCount = false;      // 찾기 칸에 내용이 있어 개수를 표시하는 중
    MatchCount count;           // 찾기 칸 안 오른쪽에 "현재/전체"로 표시
    bool selfChange = false;    // 찾기 바가 본문을 바꾸는 중 (그 알림으로 다시 세지 않는다)
    std::wstring status;        // 바 오른쪽의 보조 문구 ("n개 바꿈")
    RECT findField = {};
    RECT countRect = {};
    RECT replaceField = {};
    RECT statusRect = {};
    Button buttons[kButtonCount] = {
        {kIconUp, true, Str(StrId::TipFindPrev), {}},
        {kIconDown, true, Str(StrId::TipFindNext), {}},
        {kIconReplace, true, Str(StrId::TipToggleReplace), {}},
        {kIconClose, true, Str(StrId::TipCloseFind), {}},
        {Str(StrId::ReplaceButton), false, Str(StrId::TipReplace), {}},
        {Str(StrId::ReplaceAllButton), false, Str(StrId::TipReplaceAll), {}},
    };
    int hot = -1;
    int pressed = -1;
    bool trackingLeave = false;
};

FindBarState g_find;

std::wstring EditText(HWND edit)
{
    const int length = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    text.resize(static_cast<size_t>(GetWindowTextW(edit, text.data(), length + 1)));
    return text;
}

bool IsButtonVisible(int index)
{
    return g_find.replaceMode || (index != kReplaceButton && index != kReplaceAllButton);
}

bool IsNotFound()
{
    return g_find.hasCount && g_find.count.total == 0;
}

// "%d"가 하나 들어 있는 형식 문자열에 숫자를 넣는다 ("5개", "5 found")
std::wstring Format(const wchar_t* format, int value)
{
    wchar_t buffer[64];
    swprintf_s(buffer, format, value);
    return buffer;
}

// "2/5", 선택이 일치 항목 위에 없으면 "5개"(영어는 "5 found"), 너무 많으면 "9999+"
std::wstring CountText()
{
    if (!g_find.hasCount) return {};
    const MatchCount& c = g_find.count;
    if (c.capped) return std::to_wstring(kMaxCount) + L"+";
    if (c.total == 0) return L"0/0";
    if (c.current == 0) return Format(Str(StrId::MatchCountFormat), c.total);
    return std::to_wstring(c.current) + L"/" + std::to_wstring(c.total);
}

// 개수를 다시 세고 표시를 갱신한다
void Refresh(const std::wstring& status)
{
    KillTimer(g_find.hwnd, kRecountTimerId);
    const std::wstring text = EditText(g_find.findEdit);
    g_find.hasCount = !text.empty();
    g_find.count = g_find.hasCount ? CountMatches(g_find.editor, text, kMaxCount) : MatchCount{};
    g_find.status = status;
    InvalidateRect(g_find.hwnd, nullptr, FALSE);
    InvalidateRect(g_find.findEdit, nullptr, TRUE);  // 못 찾았을 때 글자색을 바꾸려면 입력란도 다시 그려야 한다
}

// 찾기 바가 본문을 바꾸는 동안 오는 변경 알림은 무시한다 (끝나고 한 번에 다시 센다)
struct SelfChangeScope {
    SelfChangeScope() { g_find.selfChange = true; }
    ~SelfChangeScope() { g_find.selfChange = false; }
    SelfChangeScope(const SelfChangeScope&) = delete;
    SelfChangeScope& operator=(const SelfChangeScope&) = delete;
};

// ---- 동작 ----

void DoFind(bool forward, bool fromSelectionStart = false)
{
    const std::wstring text = EditText(g_find.findEdit);
    if (!text.empty()) {
        SelfChangeScope scope;
        FindInEditor(g_find.editor, text, forward, fromSelectionStart);
    }
    Refresh({});
}

void DoReplace()
{
    const std::wstring find = EditText(g_find.findEdit);
    if (find.empty()) return;
    {
        SelfChangeScope scope;
        ReplaceCurrent(g_find.editor, find, EditText(g_find.replaceEdit));
    }
    Refresh({});
}

void DoReplaceAll()
{
    const std::wstring find = EditText(g_find.findEdit);
    if (find.empty()) return;
    int replaced = 0;
    {
        SelfChangeScope scope;
        replaced = ReplaceAllInEditor(g_find.editor, find, EditText(g_find.replaceEdit));
    }
    Refresh(replaced > 0 ? Format(Str(StrId::ReplacedFormat), replaced) : std::wstring());
}

void SetReplaceMode(bool on);

void RunButton(int index)
{
    switch (index) {
    case kPrevButton:       DoFind(false); break;
    case kNextButton:       DoFind(true); break;
    case kToggleReplaceButton: SetReplaceMode(!g_find.replaceMode); break;
    case kCloseButton:      HideFindBar(); break;
    case kReplaceButton:    DoReplace(); break;
    case kReplaceAllButton: DoReplaceAll(); break;
    }
}

// ---- 레이아웃 / 그리기 ----

int TextWidth(const wchar_t* text)
{
    HDC dc = GetDC(g_find.hwnd);
    HGDIOBJ old = SelectObject(dc, g_find.font);
    SIZE size = {};
    GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
    SelectObject(dc, old);
    ReleaseDC(g_find.hwnd, dc);
    return size.cx;
}

int FontLineHeight()
{
    HDC dc = GetDC(g_find.hwnd);
    HGDIOBJ old = SelectObject(dc, g_find.font);
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(g_find.hwnd, dc);
    return tm.tmHeight;
}

// 입력란 둥근 배경 안에 테두리 없는 EDIT를 세로 가운데로 둔다
// rightReserve: 입력란 오른쪽에 남겨 둘 폭 (찾기 칸의 개수 표시 자리)
void PlaceEdit(HWND edit, const RECT& field, int rightReserve, bool show)
{
    const int padX = Scale(kFieldPadX, g_find.dpi);
    const int lineHeight = FontLineHeight();
    const int top = field.top + (field.bottom - field.top - lineHeight) / 2;
    SetWindowPos(edit, nullptr, field.left + padX, top,
                 std::max(1, static_cast<int>(field.right - field.left) - padX * 2 - rightReserve),
                 lineHeight, SWP_NOZORDER | SWP_NOACTIVATE | (show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

void LayoutFindBar()
{
    const UINT dpi = g_find.dpi;
    RECT client;
    GetClientRect(g_find.hwnd, &client);

    const int margin = Scale(kEdgeMargin, dpi);
    const int gap = Scale(kGap, dpi);
    const int fieldGap = Scale(kFieldGap, dpi);
    const int iconWidth = Scale(kIconButtonWidth, dpi);
    const int buttonHeight = Scale(kFieldHeight, dpi);
    const int rowHeight = Scale(kRowHeight, dpi);
    const int textPad = Scale(kTextButtonPadX, dpi);
    const int replaceWidth = TextWidth(g_find.buttons[kReplaceButton].label) + textPad * 2;
    const int replaceAllWidth = TextWidth(g_find.buttons[kReplaceAllButton].label) + textPad * 2;

    // 입력란 폭: 두 행 모두 버튼이 들어갈 자리를 남긴다
    const int row1Room = client.right - margin * 2 - fieldGap - iconWidth * 4 - gap * 3;
    const int row2Room = client.right - margin * 2 - fieldGap - replaceWidth - replaceAllWidth - gap;
    int fieldWidth = std::min(Scale(kFieldWidth, dpi), row1Room);
    if (g_find.replaceMode) fieldWidth = std::min(fieldWidth, row2Room);
    fieldWidth = std::max(fieldWidth, Scale(kMinFieldWidth, dpi));

    auto rowRect = [&](int row, int left, int width) {
        const int top = Scale(kPadY, dpi) + row * rowHeight + (rowHeight - buttonHeight) / 2;
        return RECT{left, top, left + width, top + buttonHeight};
    };

    g_find.findField = rowRect(0, margin, fieldWidth);
    int x = g_find.findField.right + fieldGap;
    g_find.buttons[kPrevButton].rect = rowRect(0, x, iconWidth);
    x += iconWidth + gap;
    g_find.buttons[kNextButton].rect = rowRect(0, x, iconWidth);
    x += iconWidth + gap;
    g_find.buttons[kToggleReplaceButton].rect = rowRect(0, x, iconWidth);
    x += iconWidth;
    g_find.buttons[kCloseButton].rect = rowRect(0, std::max(x + gap, static_cast<int>(client.right) - margin - iconWidth), iconWidth);
    g_find.statusRect = rowRect(0, x + fieldGap, std::max(0, static_cast<int>(g_find.buttons[kCloseButton].rect.left) - x - fieldGap * 2));

    g_find.replaceField = rowRect(1, margin, fieldWidth);
    x = g_find.replaceField.right + fieldGap;
    g_find.buttons[kReplaceButton].rect = rowRect(1, x, replaceWidth);
    x += replaceWidth + gap;
    g_find.buttons[kReplaceAllButton].rect = rowRect(1, x, replaceAllWidth);

    // 개수 표시 폭은 가장 긴 경우 기준으로 고정해 입력 중에 입력란 폭이 흔들리지 않게 한다
    const int padX = Scale(kFieldPadX, dpi);
    // 가장 긴 표시 기준으로 폭을 고정해 입력 중 흔들리지 않게 한다
    const int widest = std::max(TextWidth(L"9999/9999"), TextWidth(Format(Str(StrId::MatchCountFormat), kMaxCount).c_str()));
    const int countWidth = std::min(widest, static_cast<int>(g_find.findField.right - g_find.findField.left) / 2);
    g_find.countRect = {g_find.findField.right - padX - countWidth, g_find.findField.top,
                        g_find.findField.right - padX, g_find.findField.bottom};
    PlaceEdit(g_find.findEdit, g_find.findField, countWidth + padX / 2, true);
    PlaceEdit(g_find.replaceEdit, g_find.replaceField, 0, g_find.replaceMode);
    InvalidateRect(g_find.hwnd, nullptr, FALSE);
}

int HitTest(POINT pt)
{
    for (int i = 0; i < kButtonCount; ++i) {
        if (IsButtonVisible(i) && PtInRect(&g_find.buttons[i].rect, pt)) return i;
    }
    return -1;
}

void PaintFindBar(HWND hwnd)
{
    const ThemeColors& theme = CurrentTheme();
    const int radius = Scale(kCornerRadius, g_find.dpi);
    BufferedPaint paint(hwnd);
    FillSolid(paint.dc, paint.client, theme.windowBg);
    // 편집 영역과의 경계는 상단 바와 같은 1px의 옅은 선
    FillSolid(paint.dc, {0, paint.client.bottom - 1, paint.client.right, paint.client.bottom}, theme.border);

    {
        Gdiplus::Graphics graphics(paint.dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        FillRoundRect(graphics, g_find.findField, theme.secondaryBg, radius);
        if (g_find.replaceMode) FillRoundRect(graphics, g_find.replaceField, theme.secondaryBg, radius);
        for (int i = 0; i < kButtonCount; ++i) {
            if (!IsButtonVisible(i)) continue;
            const bool active = i == kToggleReplaceButton && g_find.replaceMode;  // 상단 바 B/I/U의 켜짐 표시와 같은 모양
            if (i == g_find.pressed || active) {
                FillRoundRect(graphics, g_find.buttons[i].rect, theme.pressed, radius);
            } else if (i == g_find.hot) {
                FillRoundRect(graphics, g_find.buttons[i].rect, theme.hover, radius);
            }
        }
    }

    SetBkMode(paint.dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(paint.dc, g_find.font);
    for (int i = 0; i < kButtonCount; ++i) {
        if (!IsButtonVisible(i)) continue;
        Button& button = g_find.buttons[i];
        // 아이콘은 평소엔 옅게, hover일 때만 진하게 (상단 바와 같은 규칙)
        const bool emphasized = !button.icon || i == g_find.hot || i == g_find.pressed ||
                                (i == kToggleReplaceButton && g_find.replaceMode);
        SetTextColor(paint.dc, emphasized ? theme.text : theme.secondaryText);
        SelectObject(paint.dc, button.icon ? g_find.iconFont : g_find.font);
        DrawTextW(paint.dc, button.label, -1, &button.rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    const std::wstring countText = CountText();
    if (!countText.empty()) {
        SelectObject(paint.dc, g_find.font);
        SetTextColor(paint.dc, theme.secondaryText);
        DrawTextW(paint.dc, countText.c_str(), -1, &g_find.countRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (!g_find.status.empty()) {
        SelectObject(paint.dc, g_find.font);
        SetTextColor(paint.dc, theme.secondaryText);
        DrawTextW(paint.dc, g_find.status.c_str(), -1, &g_find.statusRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    SelectObject(paint.dc, oldFont);
}

void SetHot(int index)
{
    if (g_find.hot == index) return;
    g_find.hot = index;
    InvalidateRect(g_find.hwnd, nullptr, FALSE);
}

void CreateFonts()
{
    if (g_find.font) DeleteObject(g_find.font);
    if (g_find.iconFont) DeleteObject(g_find.iconFont);
    g_find.font = CreateUiFont(kFontTenths, g_find.dpi);

    LOGFONTW lf = {};
    lf.lfHeight = -Scale(kIconPixelSize, g_find.dpi);
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, IconFontFace(), _TRUNCATE);
    g_find.iconFont = CreateFontIndirectW(&lf);

    for (HWND edit : {g_find.findEdit, g_find.replaceEdit}) {
        if (edit) SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_find.font), TRUE);
    }
}

// ---- 입력란 ----

// 입력란 키 처리: Enter = 찾기/바꾸기, Shift+Enter = 이전 찾기, Ctrl+Alt+Enter = 모두 바꾸기,
// Esc = 닫기, Tab = 두 입력란 사이 이동
LRESULT CALLBACK FieldSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    switch (msg) {
    case WM_KEYDOWN:
        switch (wp) {
        case VK_RETURN:
            if (g_find.replaceMode && GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_MENU) < 0) {
                DoReplaceAll();  // Ctrl+Alt+Enter (Windows 11 메모장과 같은 단축키)
            } else if (hwnd == g_find.replaceEdit) {
                DoReplace();
            } else {
                DoFind(GetKeyState(VK_SHIFT) >= 0);
            }
            return 0;
        case VK_ESCAPE:
            HideFindBar();
            return 0;
        case VK_TAB:
            if (g_find.replaceMode) {
                HWND other = hwnd == g_find.findEdit ? g_find.replaceEdit : g_find.findEdit;
                SetFocus(other);
                SendMessageW(other, EM_SETSEL, 0, -1);
            }
            return 0;
        }
        break;

    case WM_CHAR:
        // 한 줄 EDIT는 Enter/Tab/Esc 문자를 받으면 경고음을 내므로 여기서 버린다
        if (wp == L'\r' || wp == L'\t' || wp == 0x1B) return 0;
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, FieldSubclassProc, 0);
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

HWND CreateField(const wchar_t* cueBanner)
{
    HWND edit = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                                0, 0, 0, 0, g_find.hwnd, nullptr, g_find.instance, nullptr);
    if (!edit) return nullptr;
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    // TRUE: 포커스가 있어도 입력 전까지 자리표시 문구를 보여 준다
    SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cueBanner));
    SetWindowSubclass(edit, FieldSubclassProc, 0, 0);
    return edit;
}

void CreateTooltips()
{
    g_find.tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                     CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                     g_find.hwnd, nullptr, g_find.instance, nullptr);
    if (!g_find.tooltip) return;
    for (int i = 0; i < kButtonCount; ++i) {
        TTTOOLINFOW ti = {sizeof(ti)};
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = g_find.hwnd;
        ti.uId = static_cast<UINT_PTR>(i);
        ti.lpszText = const_cast<LPWSTR>(g_find.buttons[i].tooltip);
        SendMessageW(g_find.tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    }
}

// 버튼 위치가 바뀔 때마다 툴팁 영역도 맞춘다 (숨은 버튼은 빈 영역)
void UpdateTooltipRects()
{
    if (!g_find.tooltip) return;
    for (int i = 0; i < kButtonCount; ++i) {
        TTTOOLINFOW ti = {sizeof(ti)};
        ti.hwnd = g_find.hwnd;
        ti.uId = static_cast<UINT_PTR>(i);
        ti.rect = IsButtonVisible(i) ? g_find.buttons[i].rect : RECT{};
        SendMessageW(g_find.tooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&ti));
    }
}

// 찾기 바의 바꾸기 줄을 열거나 닫는다 (찾기 칸 내용은 그대로 둔다)
void SetReplaceMode(bool on)
{
    if (g_find.replaceMode == on) return;
    g_find.replaceMode = on;
    SendMessageW(g_find.owner, WM_MOTE_RELAYOUT, 0, 0);
    LayoutFindBar();
    UpdateTooltipRects();
    HWND target = on ? g_find.replaceEdit : g_find.findEdit;
    SetFocus(target);
    SendMessageW(target, EM_SETSEL, 0, -1);
}

LRESULT CALLBACK FindBarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        LayoutFindBar();
        UpdateTooltipRects();
        return 0;

    case WM_COMMAND:
        // 입력란 내용이 바뀌면 이전 결과 표시를 지운다. 찾기 칸은 입력하는 동안 바로 찾아 준다.
        if (HIWORD(wp) == EN_CHANGE) {
            if (reinterpret_cast<HWND>(lp) == g_find.findEdit) {
                DoFind(true, true);
            } else {
                g_find.status.clear();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;

    case WM_CTLCOLOREDIT: {
        // 입력란 배경을 둥근 사각형과 같은 색으로 칠해 테두리 없는 입력란처럼 보이게 한다
        HDC dc = reinterpret_cast<HDC>(wp);
        const bool red = IsNotFound() && reinterpret_cast<HWND>(lp) == g_find.findEdit;
        const ThemeColors& theme = CurrentTheme();
        SetTextColor(dc, red ? theme.notFound : theme.text);
        SetBkColor(dc, theme.secondaryBg);
        // DC 브러시를 쓰면 테마가 바뀌어도 브러시를 다시 만들 필요가 없다
        SetDCBrushColor(dc, theme.secondaryBg);
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }

    case WM_MOUSEMOVE:
        if (!g_find.trackingLeave) {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            g_find.trackingLeave = TrackMouseEvent(&tme) != FALSE;
        }
        SetHot(HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}));
        return 0;

    case WM_MOUSELEAVE:
        g_find.trackingLeave = false;
        SetHot(-1);
        return 0;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        // 버튼은 포커스를 가져가지 않는다 → 입력란에 계속 입력할 수 있다
        const int index = HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (index >= 0) {
            g_find.pressed = index;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (GetCapture() == hwnd) {
            const int index = g_find.pressed;
            ReleaseCapture();  // WM_CAPTURECHANGED에서 눌림 상태를 해제한다
            if (index >= 0 && HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}) == index) RunButton(index);
        }
        return 0;

    case WM_CAPTURECHANGED:
        if (g_find.pressed >= 0) {
            g_find.pressed = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_TIMER:
        if (wp == kRecountTimerId) Refresh(g_find.status);
        return 0;

    case WM_ERASEBKGND:
        return 1;  // WM_PAINT에서 전체를 그린다

    case WM_PAINT:
        PaintFindBar(hwnd);
        return 0;

    case WM_DESTROY:
        if (g_find.font) DeleteObject(g_find.font);
        if (g_find.iconFont) DeleteObject(g_find.iconFont);
        g_find.font = g_find.iconFont = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND CreateFindBar(HWND owner, HINSTANCE instance, HWND editor)
{
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW;
    wc.lpfnWndProc = FindBarProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kFindBarClass;
    RegisterClassExW(&wc);

    g_find.instance = instance;
    g_find.owner = owner;
    g_find.editor = editor;
    g_find.dpi = GetDpiForWindow(owner);
    // 처음에는 숨겨 둔다. WS_CLIPCHILDREN: 바를 그릴 때 입력란을 덮어 그리지 않게 한다.
    g_find.hwnd = CreateWindowExW(0, kFindBarClass, L"", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                  0, 0, 0, 0, owner, nullptr, instance, nullptr);
    if (!g_find.hwnd) return nullptr;

    g_find.findEdit = CreateField(Str(StrId::FindCue));
    g_find.replaceEdit = CreateField(Str(StrId::ReplaceCue));
    if (!g_find.findEdit || !g_find.replaceEdit) return nullptr;
    CreateFonts();
    CreateTooltips();
    return g_find.hwnd;
}

void ShowFindBar(bool replaceMode)
{
    const bool changed = !g_find.visible || g_find.replaceMode != replaceMode;
    g_find.visible = true;
    g_find.replaceMode = replaceMode;

    // 한 줄짜리 선택이 있으면 그 내용을 찾는다 (입력란 변경 → EN_CHANGE로 바로 검색된다)
    const std::wstring selection = GetSingleLineSelection(g_find.editor, kMaxPrefillLength);
    if (!selection.empty()) SetWindowTextW(g_find.findEdit, selection.c_str());

    if (changed) {
        SendMessageW(g_find.owner, WM_MOTE_RELAYOUT, 0, 0);
        LayoutFindBar();
        UpdateTooltipRects();
    }
    SetFocus(g_find.findEdit);
    SendMessageW(g_find.findEdit, EM_SETSEL, 0, -1);
    Refresh({});  // 찾기 칸 내용이 그대로여도 바가 닫혀 있던 동안 본문이 바뀌었을 수 있다
}

void HideFindBar()
{
    if (!g_find.visible) return;
    g_find.visible = false;
    KillTimer(g_find.hwnd, kRecountTimerId);
    g_find.status.clear();
    SendMessageW(g_find.owner, WM_MOTE_RELAYOUT, 0, 0);
    SetFocus(g_find.editor);
}

bool IsFindBarVisible()
{
    return g_find.visible;
}

bool FindBarHasFocus()
{
    HWND focus = GetFocus();
    return focus && (focus == g_find.findEdit || focus == g_find.replaceEdit);
}

int FindBarHeight(UINT dpi)
{
    const int rows = g_find.replaceMode ? 2 : 1;
    return Scale(kPadY * 2 + kRowHeight * rows, dpi);
}

void SetFindBarDpi(UINT dpi)
{
    g_find.dpi = dpi;
    CreateFonts();
    LayoutFindBar();
    UpdateTooltipRects();
}

void FindNextFromBar(bool forward)
{
    if (GetWindowTextLengthW(g_find.findEdit) == 0) {
        ShowFindBar(false);
        return;
    }
    DoFind(forward);
}

void NotifyFindBarDocumentChanged()
{
    // 타이핑/캐럿 이동마다 문서 전체를 세지 않도록 잠시 모았다가 한 번만 센다
    if (!g_find.visible || g_find.selfChange) return;
    SetTimer(g_find.hwnd, kRecountTimerId, kRecountDelayMs, nullptr);
}
