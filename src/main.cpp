// mote: 선택한 글자의 색을 바꿀 수 있는 초경량 메모장
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <richedit.h>
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>
#include <string>

#include "editor.h"
#include "file_io.h"
#include "theme.h"
#include "topbar.h"
#include "../resources/resource.h"

namespace {

constexpr wchar_t kWindowClass[] = L"MoteMainWindow";
constexpr wchar_t kAppName[] = L"mote";
constexpr int kDefaultWidth = 960;   // 96 DPI 기준 초기 창 크기
constexpr int kDefaultHeight = 680;
constexpr int kMinWidth = 360;
constexpr int kMinHeight = 240;

// 상단 바 접기
constexpr UINT_PTR kHoverTimerId = 1;
constexpr UINT kHoverTimerMs = 100;
constexpr int kExpandZoneWidth = 72;       // 접힌 상태에서 펼치기 화살표를 보여 줄 오른쪽 위 영역의 폭 (96 DPI 기준)
constexpr DWORD kHideDelayMs = 300;        // 커서가 벗어난 뒤 숨기기까지 기다리는 시간
constexpr wchar_t kSettingsKey[] = L"Software\\mote";
constexpr wchar_t kToolbarCollapsedValue[] = L"ToolbarCollapsed";

struct AppState {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HWND topBar = nullptr;
    HWND editor = nullptr;
    HMENU menus = nullptr;       // File/Edit 팝업 메뉴 리소스
    HACCEL accelerators = nullptr;
    UINT dpi = 96;
    std::wstring filePath;       // 비어 있으면 저장된 적 없는 새 문서
    FileFormat format = FileFormat::Rtf;
    bool toolbarCollapsed = false;  // 사용자가 상단 바를 접었는지
    bool toolbarPeeking = false;    // 접힌 바를 Alt+F / Alt+E로 잠깐 보여 주는 중인지
    bool expandVisible = false;     // 접힌 상태에서 오른쪽 위 펼치기 화살표가 보이는 중인지
    DWORD leaveTick = 0;            // 커서가 바/화살표 영역을 벗어난 시각 (0이면 안에 있음)
};

AppState g_app;

void ShowError(const wchar_t* title, const std::wstring& detail)
{
    TaskDialog(g_app.window, nullptr, kAppName, title, detail.c_str(), TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr);
}

void UpdateTitle()
{
    std::wstring name = L"제목 없음";
    if (!g_app.filePath.empty()) {
        const size_t slash = g_app.filePath.find_last_of(L"\\/");
        name = slash == std::wstring::npos ? g_app.filePath : g_app.filePath.substr(slash + 1);
    }
    SetWindowTextW(g_app.window, (name + L" - " + kAppName).c_str());
}

// 에디터 본문 영역(세로 스크롤바 제외)의 오른쪽 끝 x 좌표 (메인 창 클라이언트 기준)
int EditorContentRight()
{
    RECT rc;
    GetClientRect(g_app.editor, &rc);
    MapWindowPoints(g_app.editor, g_app.window, reinterpret_cast<POINT*>(&rc), 2);
    return rc.right;
}

// 접힌 상태에서 펼치기 화살표를 보여 줄 오른쪽 위 영역 (메인 창 클라이언트 기준).
// 스크롤바를 가리지 않도록 에디터 본문 영역 안쪽에 둔다.
RECT ExpandZone()
{
    const int right = EditorContentRight();
    return {right - Scale(kExpandZoneWidth, g_app.dpi), 0, right, TopBarHeight(g_app.dpi)};
}

void PositionExpandButton()
{
    const int size = ExpandButtonSize(g_app.dpi);
    const int right = EditorContentRight() - ExpandButtonMargin(g_app.dpi);
    SetWindowPos(ExpandButtonWindow(), HWND_TOP, right - size, ExpandButtonTop(g_app.dpi), size, size,
                 SWP_NOACTIVATE | (g_app.expandVisible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

void LayoutChildren()
{
    RECT client;
    GetClientRect(g_app.window, &client);
    const int barHeight = TopBarHeight(g_app.dpi);
    // 접힌 상태에서는 에디터가 클라이언트 전체를 쓰고, 바는 Alt+F/E로 잠깐 보여 줄 때만 에디터 위에 겹친다
    const int editorTop = g_app.toolbarCollapsed ? 0 : barHeight;
    const bool barVisible = !g_app.toolbarCollapsed || g_app.toolbarPeeking;

    // 두 자식을 한 번에 옮겨 크기 조절 중 깜빡임을 줄인다.
    // 바를 HWND_TOP으로 두어 겹칠 때 에디터보다 위에 그려지게 한다 (둘 다 WS_CLIPSIBLINGS).
    HDWP dwp = BeginDeferWindowPos(2);
    dwp = DeferWindowPos(dwp, g_app.topBar, HWND_TOP, 0, 0, client.right, barHeight,
                         SWP_NOACTIVATE | (barVisible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    dwp = DeferWindowPos(dwp, g_app.editor, nullptr, 0, editorTop, client.right,
                         std::max(0, static_cast<int>(client.bottom) - editorTop), SWP_NOZORDER | SWP_NOACTIVATE);
    EndDeferWindowPos(dwp);
    PositionExpandButton();
}

// ---- 상단 바 접기 ----

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

// 잠깐 보이는 바 / 펼치기 화살표를 모두 숨기고 타이머를 멈춘다
void EndTransientUi()
{
    if (!g_app.toolbarPeeking && !g_app.expandVisible) return;
    g_app.toolbarPeeking = false;
    g_app.expandVisible = false;
    KillTimer(g_app.window, kHoverTimerId);
    LayoutChildren();
}

void StartTransientUi(bool peekBar)
{
    if (!g_app.toolbarCollapsed) return;
    if (peekBar) {
        g_app.toolbarPeeking = true;
        g_app.expandVisible = false;
    } else {
        g_app.expandVisible = true;
    }
    g_app.leaveTick = 0;
    LayoutChildren();
    // 커서가 영역을 떠났는지 주기적으로 확인한다 (자식 창들에 걸쳐 있어 WM_MOUSELEAVE만으로는
    // 메뉴/팝업이 열린 상태까지 함께 판단하기 어렵다)
    SetTimer(g_app.window, kHoverTimerId, kHoverTimerMs, nullptr);
}

void OnHoverTimer()
{
    if (IsTopBarBusy() || GetCapture() == ExpandButtonWindow()) {
        g_app.leaveTick = 0;
        return;
    }
    POINT cursor;
    GetCursorPos(&cursor);
    RECT area;
    if (g_app.toolbarPeeking) {
        GetWindowRect(g_app.topBar, &area);
    } else {
        area = ExpandZone();
        MapWindowPoints(g_app.window, nullptr, reinterpret_cast<POINT*>(&area), 2);
    }
    if (PtInRect(&area, cursor)) {
        g_app.leaveTick = 0;
    } else if (g_app.leaveTick == 0) {
        g_app.leaveTick = GetTickCount();
    } else if (GetTickCount() - g_app.leaveTick >= kHideDelayMs) {
        EndTransientUi();
    }
}

// 메시지 루프에서 모든 WM_MOUSEMOVE를 보고, 접힌 상태에서 커서가 오른쪽 위에 오면 펼치기 화살표를 보여 준다.
// 마우스는 대부분 에디터가 받기 때문에 메인 창 프로시저 대신 루프에서 확인한다.
void CheckExpandZone(const MSG& msg)
{
    if (!g_app.toolbarCollapsed || g_app.toolbarPeeking || g_app.expandVisible) return;
    if (msg.wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) return;  // 드래그로 선택 중이면 무시
    if (msg.hwnd != g_app.window && GetAncestor(msg.hwnd, GA_ROOT) != g_app.window) return;

    POINT pt = {GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam)};
    MapWindowPoints(msg.hwnd, g_app.window, &pt, 1);
    const RECT zone = ExpandZone();
    if (PtInRect(&zone, pt)) StartTransientUi(false);
}

void ToggleToolbar()
{
    g_app.toolbarCollapsed = !g_app.toolbarCollapsed;
    g_app.toolbarPeeking = false;
    g_app.expandVisible = false;
    KillTimer(g_app.window, kHoverTimerId);
    SetTopBarCollapsed(g_app.topBar, g_app.toolbarCollapsed);
    LayoutChildren();
    SaveToolbarCollapsed(g_app.toolbarCollapsed);
}

void OpenMenuFromKeyboard(int index)
{
    // 접힌 상태에서도 Alt+F / Alt+E가 동작하도록 바를 잠깐 보여 준 뒤 메뉴를 연다
    if (g_app.toolbarCollapsed && !g_app.toolbarPeeking) StartTransientUi(true);
    OpenTopBarMenu(g_app.topBar, index);
}

// 선택 영역 서식을 상단 바 버튼 상태에 반영한다
void RefreshFormatState()
{
    SetTopBarFormatState(g_app.topBar, GetSelectionFormat(g_app.editor));
}

// ---- 서식 명령 ----

void ApplyColor(COLORREF color)
{
    SetSelectionColor(g_app.editor, color);
    SetFocus(g_app.editor);
    RefreshFormatState();
}

void ChooseCustomColor()
{
    // Windows 기본 색 선택 대화상자. "사용자 지정 색" 칸은 실행 중에만 기억한다.
    static COLORREF customColors[16] = {
        RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255),
        RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255),
        RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255),
        RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255),
    };
    const FormatState current = GetSelectionFormat(g_app.editor);
    CHOOSECOLORW cc = {sizeof(cc)};
    cc.hwndOwner = g_app.window;
    cc.rgbResult = current.colorMixed ? DefaultTextColor() : current.color;
    cc.lpCustColors = customColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    if (ChooseColorW(&cc)) {
        ApplyColor(cc.rgbResult);
    } else {
        SetFocus(g_app.editor);
    }
}

void ToggleEffect(DWORD effect)
{
    ToggleSelectionEffect(g_app.editor, effect);
    RefreshFormatState();
}

// ---- 파일 명령 ----

void NewDocument()
{
    ResetEditorDocument(g_app.editor);
    g_app.filePath.clear();
    g_app.format = FileFormat::Rtf;
    UpdateTitle();
    RefreshFormatState();
}

bool OpenDocument(const std::wstring& path)
{
    FileFormat format = FileFormat::Rtf;
    std::wstring error;
    if (!LoadFileIntoEditor(g_app.editor, path, format, error)) {
        ShowError(L"파일을 열 수 없습니다.", path + L"\n\n" + error);
        return false;
    }
    g_app.filePath = path;
    g_app.format = format;
    UpdateTitle();
    RefreshFormatState();
    return true;
}

bool SaveDocumentAs()
{
    std::wstring path;
    if (!ShowSaveDialog(g_app.window, g_app.filePath, g_app.format, path)) return false;

    const FileFormat format = FormatFromPath(path);
    std::wstring error;
    if (!SaveEditorToFile(g_app.editor, path, format, error)) {
        ShowError(L"파일을 저장할 수 없습니다.", path + L"\n\n" + error);
        return false;
    }
    g_app.filePath = path;
    g_app.format = format;
    UpdateTitle();
    return true;
}

bool SaveDocument()
{
    if (g_app.filePath.empty()) return SaveDocumentAs();
    std::wstring error;
    if (!SaveEditorToFile(g_app.editor, g_app.filePath, g_app.format, error)) {
        ShowError(L"파일을 저장할 수 없습니다.", g_app.filePath + L"\n\n" + error);
        return false;
    }
    UpdateTitle();
    return true;
}

// 탭 대신 독립된 새 창(새 프로세스)으로 문서를 하나 더 연다.
// 창마다 프로세스가 분리되어 한 창의 문제가 다른 창에 영향을 주지 않는다.
void OpenNewWindow()
{
    wchar_t exePath[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
    if (length == 0 || length >= std::size(exePath)) {
        ShowError(L"새 창을 열 수 없습니다.", L"실행 파일 경로를 확인할 수 없습니다.");
        return;
    }

    std::wstring commandLine = L"\"" + std::wstring(exePath) + L"\"";  // CreateProcessW는 쓰기 가능한 버퍼가 필요하다
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exePath, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        const DWORD code = GetLastError();
        wchar_t* text = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
        ShowError(L"새 창을 열 수 없습니다.", text ? text : L"알 수 없는 오류");
        if (text) LocalFree(text);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

void CommandOpen()
{
    std::wstring path;
    if (ShowOpenDialog(g_app.window, path)) OpenDocument(path);
}

// ---- 메뉴 ----

void EnableItem(HMENU menu, UINT id, bool enabled)
{
    EnableMenuItem(menu, id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
}

// 메뉴가 열리기 직전에 현재 편집 상태에 맞게 항목을 활성/비활성화한다
void UpdateMenuState(HMENU menu)
{
    CHARRANGE sel;
    SendMessageW(g_app.editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    const bool hasSelection = sel.cpMax > sel.cpMin;
    EnableItem(menu, IDM_EDIT_UNDO, SendMessageW(g_app.editor, EM_CANUNDO, 0, 0) != 0);
    EnableItem(menu, IDM_EDIT_REDO, SendMessageW(g_app.editor, EM_CANREDO, 0, 0) != 0);
    EnableItem(menu, IDM_EDIT_CUT, hasSelection);
    EnableItem(menu, IDM_EDIT_COPY, hasSelection);
    EnableItem(menu, IDM_EDIT_PASTE, SendMessageW(g_app.editor, EM_CANPASTE, 0, 0) != 0);
    if (GetMenuState(menu, IDM_VIEW_TOOLBAR, MF_BYCOMMAND) != static_cast<UINT>(-1)) {
        ModifyMenuW(menu, IDM_VIEW_TOOLBAR, MF_BYCOMMAND | MF_STRING, IDM_VIEW_TOOLBAR,
                    g_app.toolbarCollapsed ? L"Show &Toolbar\tCtrl+\\" : L"Hide &Toolbar\tCtrl+\\");
    }
}

void OnCommand(UINT id)
{
    switch (id) {
    case IDM_FILE_NEW:       NewDocument(); break;
    case IDM_FILE_NEWWINDOW: OpenNewWindow(); break;
    case IDM_FILE_OPEN:      CommandOpen(); break;
    case IDM_FILE_SAVE:      SaveDocument(); break;
    case IDM_FILE_SAVEAS:    SaveDocumentAs(); break;
    case IDM_FILE_EXIT:      PostMessageW(g_app.window, WM_CLOSE, 0, 0); break;

    case IDM_EDIT_UNDO:      SendMessageW(g_app.editor, EM_UNDO, 0, 0); break;
    case IDM_EDIT_REDO:      SendMessageW(g_app.editor, EM_REDO, 0, 0); break;
    case IDM_EDIT_CUT:       SendMessageW(g_app.editor, WM_CUT, 0, 0); break;
    case IDM_EDIT_COPY:      SendMessageW(g_app.editor, WM_COPY, 0, 0); break;
    case IDM_EDIT_PASTE:     SendMessageW(g_app.editor, WM_PASTE, 0, 0); break;
    case IDM_EDIT_SELECTALL: {
        CHARRANGE all = {0, -1};
        SendMessageW(g_app.editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
        break;
    }

    case IDM_FORMAT_BOLD:        ToggleEffect(CFE_BOLD); break;
    case IDM_FORMAT_ITALIC:      ToggleEffect(CFE_ITALIC); break;
    case IDM_FORMAT_UNDERLINE:   ToggleEffect(CFE_UNDERLINE); break;
    case IDM_FORMAT_CUSTOMCOLOR: ChooseCustomColor(); break;

    case IDM_VIEW_TOOLBAR:   ToggleToolbar(); break;

    case IDM_OPEN_FILE_MENU: OpenMenuFromKeyboard(0); break;
    case IDM_OPEN_EDIT_MENU: OpenMenuFromKeyboard(1); break;
    }
}

bool OnCreate(HWND hwnd)
{
    g_app.window = hwnd;
    g_app.dpi = GetDpiForWindow(hwnd);

    // DPI에 맞춰 초기 창 크기를 조정한다 (CreateWindow 시점에는 DPI를 모르기 때문)
    SetWindowPos(hwnd, nullptr, 0, 0, Scale(kDefaultWidth, g_app.dpi), Scale(kDefaultHeight, g_app.dpi),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    g_app.menus = LoadMenuW(g_app.instance, MAKEINTRESOURCEW(IDR_MAINMENU));
    g_app.topBar = CreateTopBar(hwnd, g_app.instance, g_app.menus);
    g_app.editor = CreateEditor(hwnd, g_app.instance, GetSubMenu(g_app.menus, 1));
    if (!g_app.menus || !g_app.topBar || !g_app.editor) return false;

    SetEditorDpi(g_app.editor, g_app.dpi);
    g_app.toolbarCollapsed = LoadToolbarCollapsed();
    SetTopBarCollapsed(g_app.topBar, g_app.toolbarCollapsed);
    LayoutChildren();
    UpdateTitle();
    RefreshFormatState();
    return true;
}

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        return OnCreate(hwnd) ? 0 : -1;

    case WM_SIZE:
        if (g_app.editor) LayoutChildren();
        return 0;

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        const UINT dpi = GetDpiForWindow(hwnd);
        info->ptMinTrackSize = {Scale(kMinWidth, dpi), Scale(kMinHeight, dpi)};
        return 0;
    }

    case WM_DPICHANGED: {
        // 모니터를 옮기거나 배율이 바뀌면 시스템이 권장하는 크기로 바꾸고 UI를 다시 계산한다
        g_app.dpi = HIWORD(wp);
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        SetTopBarDpi(g_app.topBar, g_app.dpi);
        SetEditorDpi(g_app.editor, g_app.dpi);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_SETFOCUS:
        // 창이 활성화되면 항상 편집 영역에 입력 포커스를 준다
        if (g_app.editor) SetFocus(g_app.editor);
        return 0;

    case WM_INITMENUPOPUP:
        UpdateMenuState(reinterpret_cast<HMENU>(wp));
        return 0;

    case WM_COMMAND:
        // 메뉴/가속키/컨텍스트 메뉴 명령. 에디터 알림(EN_*)은 lParam이 컨트롤 핸들이다.
        if (lp == 0 || reinterpret_cast<HWND>(lp) != g_app.editor) OnCommand(LOWORD(wp));
        return 0;

    case WM_NOTIFY: {
        // 캐럿/선택이 바뀔 때마다 B/I/U 활성 상태와 색 표시를 갱신한다
        const auto* header = reinterpret_cast<const NMHDR*>(lp);
        if (header->hwndFrom == g_app.editor && header->code == EN_SELCHANGE) RefreshFormatState();
        return 0;
    }

    case WM_TIMER:
        if (wp == kHoverTimerId) OnHoverTimer();
        return 0;

    case WM_MOTE_SETCOLOR:
        ApplyColor(static_cast<COLORREF>(lp));
        return 0;

    case WM_NCACTIVATE:
        // 색상 팝업이 활성화되어도 메인 창 제목 표시줄은 활성 모양을 유지한다 (메뉴와 같은 동작)
        if (!wp && IsColorPopupOpen()) return DefWindowProcW(hwnd, msg, TRUE, lp);
        break;

    case WM_ERASEBKGND:
        return 1;  // 자식 창이 클라이언트 영역 전체를 덮는다

    case WM_DESTROY:
        if (g_app.menus) DestroyMenu(g_app.menus);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    g_app.instance = instance;

    // RichEdit의 클립보드/드래그 앤 드롭과 파일 대화상자는 OLE(STA)가 필요하다
    if (FAILED(OleInitialize(nullptr))) return 1;

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);

    // RichEdit 4.1 이상(RICHEDIT50W) 클래스를 등록한다
    if (!LoadLibraryW(L"Msftedit.dll")) {
        MessageBoxW(nullptr, L"Msftedit.dll을 불러올 수 없습니다.", kAppName, MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = MainWindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    HWND window = CreateWindowExW(0, kWindowClass, kAppName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                  CW_USEDEFAULT, CW_USEDEFAULT, kDefaultWidth, kDefaultHeight,
                                  nullptr, nullptr, instance, nullptr);
    if (!window) {
        MessageBoxW(nullptr, L"창을 만들 수 없습니다.", kAppName, MB_ICONERROR);
        return 1;
    }
    g_app.accelerators = LoadAcceleratorsW(instance, MAKEINTRESOURCEW(IDR_ACCEL));

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    // 탐색기에서 파일을 끌어다 놓거나 "연결 프로그램"으로 연 경우
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        if (argc > 1) OpenDocument(argv[1]);
        LocalFree(argv);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_MOUSEMOVE) CheckExpandZone(msg);
        if (TranslateAcceleratorW(window, g_app.accelerators, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    OleUninitialize();
    return static_cast<int>(msg.wParam);
}
