// mote: 선택한 글자의 색을 바꿀 수 있는 초경량 메모장
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
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

void LayoutChildren()
{
    RECT client;
    GetClientRect(g_app.window, &client);
    const int barHeight = TopBarHeight(g_app.dpi);
    // 두 자식을 한 번에 옮겨 크기 조절 중 깜빡임을 줄인다
    HDWP dwp = BeginDeferWindowPos(2);
    dwp = DeferWindowPos(dwp, g_app.topBar, nullptr, 0, 0, client.right, barHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    dwp = DeferWindowPos(dwp, g_app.editor, nullptr, 0, barHeight, client.right,
                         std::max(0, static_cast<int>(client.bottom) - barHeight), SWP_NOZORDER | SWP_NOACTIVATE);
    EndDeferWindowPos(dwp);
}

// ---- 파일 명령 ----

void NewDocument()
{
    ResetEditorDocument(g_app.editor);
    g_app.filePath.clear();
    g_app.format = FileFormat::Rtf;
    UpdateTitle();
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
}

void OnCommand(UINT id)
{
    switch (id) {
    case IDM_FILE_NEW:       NewDocument(); break;
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

    case IDM_OPEN_FILE_MENU: OpenTopBarMenu(g_app.topBar, 0); break;
    case IDM_OPEN_EDIT_MENU: OpenTopBarMenu(g_app.topBar, 1); break;
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
    LayoutChildren();
    UpdateTitle();
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
        if (TranslateAcceleratorW(window, g_app.accelerators, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    OleUninitialize();
    return static_cast<int>(msg.wParam);
}
