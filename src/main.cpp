// mote: 선택한 글자의 색을 바꿀 수 있는 초경량 메모장
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <richedit.h>
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>
#include <string>
#include <vector>

#include "confirm.h"
#include "editor.h"
#include "file_io.h"
#include "findbar.h"
#include "settings.h"
#include "theme.h"
#include "topbar.h"
#include "../resources/resource.h"
#include "strings.h"

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

constexpr UINT kRecentPathChars = 60;      // 최근 파일 메뉴에서 경로를 줄여 보여 줄 길이

struct AppState {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HWND topBar = nullptr;
    HWND editor = nullptr;
    HWND findBar = nullptr;
    HWND restoreFocus = nullptr; // 창이 비활성화될 때 포커스가 있던 자식 (다시 활성화되면 돌려준다)
    HMENU menus = nullptr;       // File/Edit 팝업 메뉴 리소스
    HMENU recentMenu = nullptr;  // File > Open Recent 하위 메뉴
    HACCEL accelerators = nullptr;
    UINT dpi = 96;
    std::wstring filePath;       // 비어 있으면 저장된 적 없는 새 문서
    FileFormat format = FileFormat::Rtf;
    TextFileInfo textInfo;          // TXT의 원래 인코딩/줄바꿈 (저장할 때 그대로 쓴다)
    bool titleModified = false;     // 제목에 수정 표시(*)를 붙여 둔 상태인지
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

std::wstring DocumentName()
{
    if (g_app.filePath.empty()) return Str(StrId::Untitled);
    const size_t slash = g_app.filePath.find_last_of(L"\\/");
    return slash == std::wstring::npos ? g_app.filePath : g_app.filePath.substr(slash + 1);
}

bool IsDocumentModified()
{
    return SendMessageW(g_app.editor, EM_GETMODIFY, 0, 0) != 0;
}

// 제목: "파일명 - mote", 저장되지 않은 변경이 있으면 "*파일명 - mote"
void UpdateTitle()
{
    g_app.titleModified = IsDocumentModified();
    SetWindowTextW(g_app.window, ((g_app.titleModified ? L"*" : L"") + DocumentName() + L" - " + kAppName).c_str());
}

// 문서 내용이나 선택이 바뀔 때마다 호출된다. 수정 상태가 바뀐 경우에만 제목을 다시 쓴다.
void OnEditorChanged()
{
    if (IsDocumentModified() != g_app.titleModified) UpdateTitle();
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
    const int findTop = g_app.toolbarCollapsed ? 0 : barHeight;
    const bool barVisible = !g_app.toolbarCollapsed || g_app.toolbarPeeking;
    const bool findVisible = IsFindBarVisible();
    const int findHeight = findVisible ? FindBarHeight(g_app.dpi) : 0;
    const int editorTop = findTop + findHeight;

    // 자식들을 한 번에 옮겨 크기 조절 중 깜빡임을 줄인다.
    // 바를 HWND_TOP으로 두어 겹칠 때 에디터/찾기 바보다 위에 그려지게 한다 (모두 WS_CLIPSIBLINGS).
    HDWP dwp = BeginDeferWindowPos(3);
    dwp = DeferWindowPos(dwp, g_app.topBar, HWND_TOP, 0, 0, client.right, barHeight,
                         SWP_NOACTIVATE | (barVisible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    dwp = DeferWindowPos(dwp, g_app.findBar, nullptr, 0, findTop, client.right, std::max(findHeight, 1),
                         SWP_NOZORDER | SWP_NOACTIVATE | (findVisible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    dwp = DeferWindowPos(dwp, g_app.editor, nullptr, 0, editorTop, client.right,
                         std::max(0, static_cast<int>(client.bottom) - editorTop), SWP_NOZORDER | SWP_NOACTIVATE);
    EndDeferWindowPos(dwp);
    PositionExpandButton();
}

// ---- 창 위치/크기 기억 ----

// 다음 실행 때 같은 위치/크기로 열리도록 저장한다 (여러 창이면 마지막으로 닫은 창 기준)
void SaveWindowState()
{
    WINDOWPLACEMENT placement = {sizeof(placement)};
    if (GetWindowPlacement(g_app.window, &placement)) SaveWindowPlacement(placement);
}

// 저장된 위치/크기를 숨긴 창에 적용하고, 처음 보여 줄 때 쓸 ShowWindow 값을 돌려준다
int RestoreWindowState(HWND window, int showCommand)
{
    WINDOWPLACEMENT placement;
    if (!LoadWindowPlacement(placement)) return showCommand;
    const bool wasMaximized = placement.showCmd == SW_SHOWMAXIMIZED;
    placement.showCmd = SW_HIDE;
    placement.flags = 0;
    // 저장된 위치가 배율이 다른 모니터면 첫 번째 호출에서 WM_DPICHANGED가 와서 크기가 권장값으로 바뀐다.
    // 이미 그 모니터로 옮겨진 뒤 한 번 더 적용해 저장된 크기 그대로 맞춘다.
    // (모니터가 빠져 화면 밖이면 시스템이 보이는 위치로 옮겨 준다)
    SetWindowPlacement(window, &placement);
    SetWindowPlacement(window, &placement);
    // 바로 가기의 "최소화/최대화 상태로 실행" 설정은 그대로 따르고, 기본 실행일 때만 최대화를 되살린다
    const bool defaultShow = showCommand == SW_SHOWNORMAL || showCommand == SW_SHOWDEFAULT;
    return (defaultShow && wasMaximized) ? SW_SHOWMAXIMIZED : showCommand;
}

// ---- 상단 바 접기 ----

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
    if (IsFindBarVisible()) return;  // 펼치기 화살표 자리가 찾기 바의 닫기 버튼과 겹친다 (Ctrl+\로는 펼칠 수 있다)
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
    // Ctrl+B/I/U 가속키는 찾기 입력란에 포커스가 있어도 오므로, 그때는 본문 서식을 바꾸지 않는다
    if (FindBarHasFocus()) return;
    ToggleSelectionEffect(g_app.editor, effect);
    RefreshFormatState();
}

// ---- 파일 명령 ----

bool SaveDocument();

// 저장되지 않은 변경이 있으면 저장할지 묻는다. 계속 진행해도 되면 true (저장함 / 저장 안 함).
bool ConfirmDiscardChanges()
{
    if (!IsDocumentModified()) return true;

    switch (AskSaveChanges(g_app.window, DocumentName())) {
    case SaveChoice::Save:     return SaveDocument();  // 다른 이름으로 저장을 취소하거나 저장에 실패하면 진행하지 않는다
    case SaveChoice::DontSave: return true;
    default:                   return false;
    }
}

// 현재 파일 또는 가장 최근 파일이 있는 폴더 (열기 대화상자의 시작 위치)
std::wstring StartFolder()
{
    std::wstring path = g_app.filePath;
    if (path.empty()) {
        const std::vector<std::wstring> recent = LoadRecentFiles();
        if (!recent.empty()) path = recent.front();
    }
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

void NewDocument()
{
    if (!ConfirmDiscardChanges()) return;
    ResetEditorDocument(g_app.editor);
    g_app.filePath.clear();
    g_app.format = FileFormat::Rtf;
    g_app.textInfo = {};
    UpdateTitle();
    RefreshFormatState();
}

bool OpenDocument(const std::wstring& path)
{
    FileFormat format = FileFormat::Rtf;
    TextFileInfo textInfo;
    std::wstring error;
    if (!LoadFileIntoEditor(g_app.editor, path, format, textInfo, error)) {
        RemoveRecentFile(path);  // 지워졌거나 옮겨진 파일이 최근 목록에 계속 남지 않게 한다
        ShowError(Str(StrId::ErrorOpenFile), path + L"\n\n" + error);
        return false;
    }
    g_app.filePath = path;
    g_app.format = format;
    g_app.textInfo = textInfo;
    AddRecentFile(path);
    UpdateTitle();
    RefreshFormatState();
    return true;
}

// 파일로 저장한다. 원래 인코딩(ANSI)으로 저장할 수 없는 글자가 있으면 UTF-8로 저장할지 묻고,
// 그렇게 저장하면 textInfo의 인코딩을 UTF-8로 바꾼다. 저장하지 않았으면 false.
bool WriteDocument(const std::wstring& path, FileFormat format, TextFileInfo& textInfo)
{
    std::wstring error;
    SaveResult result = SaveEditorToFile(g_app.editor, path, format, textInfo, error);
    if (result == SaveResult::NeedsUnicode) {
        std::wstring message = Str(StrId::SaveUtf8Message);
        const size_t slot = message.find(L"%s");
        if (slot != std::wstring::npos) message.replace(slot, 2, EncodingName(textInfo.encoding));
        if (!AskConfirm(g_app.window, Str(StrId::SaveUtf8Title), message, Str(StrId::SaveUtf8Button))) return false;
        textInfo.encoding = TextEncoding::Utf8;
        result = SaveEditorToFile(g_app.editor, path, format, textInfo, error);
    }
    if (result != SaveResult::Ok) {
        ShowError(Str(StrId::ErrorSaveFile), path + L"\n\n" + error);
        return false;
    }
    return true;
}

bool SaveDocumentAs()
{
    std::wstring path;
    if (!ShowSaveDialog(g_app.window, g_app.filePath, g_app.format, path)) return false;

    // TXT를 다른 이름의 TXT로 저장하면 인코딩/줄바꿈을 이어 쓰고, 서식 문서에서 바꾸면 기본값(UTF-8, CRLF)을 쓴다
    const FileFormat format = FormatFromPath(path);
    TextFileInfo textInfo = g_app.format == FileFormat::Text ? g_app.textInfo : TextFileInfo{};
    if (!WriteDocument(path, format, textInfo)) return false;
    g_app.filePath = path;
    g_app.format = format;
    g_app.textInfo = textInfo;
    AddRecentFile(path);
    UpdateTitle();
    return true;
}

bool SaveDocument()
{
    if (g_app.filePath.empty()) return SaveDocumentAs();
    if (!WriteDocument(g_app.filePath, g_app.format, g_app.textInfo)) return false;
    AddRecentFile(g_app.filePath);
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
        ShowError(Str(StrId::ErrorNewWindow), Str(StrId::ErrorExePath));
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
        ShowError(Str(StrId::ErrorNewWindow), text ? text : Str(StrId::ErrorUnknown));
        if (text) LocalFree(text);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

void CommandOpen()
{
    if (!ConfirmDiscardChanges()) return;
    std::wstring path;
    if (ShowOpenDialog(g_app.window, StartFolder(), path)) OpenDocument(path);
}

void OpenRecent(size_t index)
{
    const std::vector<std::wstring> recent = LoadRecentFiles();
    if (index >= recent.size() || !ConfirmDiscardChanges()) return;
    OpenDocument(recent[index]);
}

// ---- 최근 파일 메뉴 ----

// 메뉴가 열릴 때마다 레지스트리의 목록으로 다시 채운다 (다른 mote 창에서 연 파일도 보인다)
void FillRecentMenu(HMENU menu)
{
    while (GetMenuItemCount(menu) > 0) DeleteMenu(menu, 0, MF_BYPOSITION);

    const std::vector<std::wstring> recent = LoadRecentFiles();
    if (recent.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, IDM_FILE_RECENT_EMPTY, Str(StrId::RecentEmpty));
        return;
    }
    for (size_t i = 0; i < recent.size(); ++i) {
        // 긴 경로는 "C:\...\note.rtf"처럼 줄인다
        wchar_t compact[MAX_PATH] = {};
        if (!PathCompactPathExW(compact, recent[i].c_str(), kRecentPathChars, 0)) {
            wcsncpy_s(compact, recent[i].c_str(), _TRUNCATE);
        }
        // 경로의 &는 메뉴에서 단축키 표시로 해석되므로 &&로 바꾼다
        std::wstring label = L"&" + std::to_wstring(i + 1) + L"  ";
        for (const wchar_t* p = compact; *p; ++p) {
            if (*p == L'&') label += L'&';
            label += *p;
        }
        AppendMenuW(menu, MF_STRING, IDM_FILE_RECENT_FIRST + i, label.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_FILE_RECENT_CLEAR, Str(StrId::RecentClear));
}

HMENU FindRecentSubmenu()
{
    HMENU fileMenu = GetSubMenu(g_app.menus, 0);
    const int count = GetMenuItemCount(fileMenu);
    for (int i = 0; i < count; ++i) {
        if (HMENU sub = GetSubMenu(fileMenu, i)) return sub;
    }
    return nullptr;
}

// ---- 메뉴 ----

void EnableItem(HMENU menu, UINT id, bool enabled)
{
    EnableMenuItem(menu, id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
}

// 메뉴가 열리기 직전에 현재 편집 상태에 맞게 항목을 활성/비활성화한다
void UpdateMenuState(HMENU menu)
{
    if (menu == g_app.recentMenu) {
        FillRecentMenu(menu);
        return;
    }
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
                    Str(g_app.toolbarCollapsed ? StrId::MenuShowToolbar : StrId::MenuHideToolbar));
    }
}

void OnCommand(UINT id)
{
    if (id >= IDM_FILE_RECENT_FIRST && id < IDM_FILE_RECENT_FIRST + kMaxRecentFiles) {
        OpenRecent(id - IDM_FILE_RECENT_FIRST);
        return;
    }
    switch (id) {
    case IDM_FILE_RECENT_CLEAR: ClearRecentFiles(); break;

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
    case IDM_EDIT_FIND:      ShowFindBar(false); break;
    case IDM_EDIT_REPLACE:   ShowFindBar(true); break;
    case IDM_EDIT_FINDNEXT:  FindNextFromBar(true); break;
    case IDM_EDIT_FINDPREV:  FindNextFromBar(false); break;

    case IDM_FORMAT_BOLD:        ToggleEffect(CFE_BOLD); break;
    case IDM_FORMAT_ITALIC:      ToggleEffect(CFE_ITALIC); break;
    case IDM_FORMAT_UNDERLINE:   ToggleEffect(CFE_UNDERLINE); break;
    case IDM_FORMAT_CUSTOMCOLOR: ChooseCustomColor(); break;

    case IDM_VIEW_TOOLBAR:   ToggleToolbar(); break;

    case IDM_OPEN_FILE_MENU: OpenMenuFromKeyboard(0); break;
    case IDM_OPEN_EDIT_MENU: OpenMenuFromKeyboard(1); break;
    }
}

// ---- 테마 ----

// 이 스레드의 툴팁 창들(상단 바/찾기 바 버튼 설명)을 현재 테마로 그리게 한다.
// 툴팁은 자식 창이 아니라 팝업이라 EnumChildWindows로는 찾을 수 없다.
void ApplyTooltipTheme()
{
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM) -> BOOL {
        wchar_t className[32];
        if (GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) &&
            wcscmp(className, TOOLTIPS_CLASSW) == 0) {
            ApplyControlTheme(hwnd);
        }
        return TRUE;
    }, 0);
}

// Windows 설정에서 라이트/다크 앱 모드가 바뀌었을 때 화면 전체를 새 테마로 다시 그린다
void OnSystemThemeChanged()
{
    const COLORREF previousText = DefaultTextColor();
    if (!UpdateThemeFromSystem()) return;

    ApplyTitleBarTheme(g_app.window);
    ApplyMenuTheme();
    ApplyTooltipTheme();
    ApplyEditorTheme(g_app.editor, previousText);
    RefreshFormatState();  // 색 표시가 "기본색"이면 새 기본색으로 바뀐다
    RedrawWindow(g_app.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

bool OnCreate(HWND hwnd)
{
    ApplyTitleBarTheme(hwnd);
    g_app.window = hwnd;
    g_app.dpi = GetDpiForWindow(hwnd);

    // DPI에 맞춰 초기 창 크기를 조정한다 (CreateWindow 시점에는 DPI를 모르기 때문)
    SetWindowPos(hwnd, nullptr, 0, 0, Scale(kDefaultWidth, g_app.dpi), Scale(kDefaultHeight, g_app.dpi),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    g_app.menus = LoadMenuW(g_app.instance, MAKEINTRESOURCEW(IsKoreanUi() ? IDR_MAINMENU_KO : IDR_MAINMENU));
    g_app.topBar = CreateTopBar(hwnd, g_app.instance, g_app.menus);
    g_app.editor = CreateEditor(hwnd, g_app.instance, GetSubMenu(g_app.menus, 1));
    if (!g_app.menus || !g_app.topBar || !g_app.editor) return false;
    g_app.findBar = CreateFindBar(hwnd, g_app.instance, g_app.editor);
    if (!g_app.findBar) return false;
    g_app.recentMenu = FindRecentSubmenu();

    SetEditorDpi(g_app.editor, g_app.dpi);
    g_app.toolbarCollapsed = LoadToolbarCollapsed();
    SetTopBarCollapsed(g_app.topBar, g_app.toolbarCollapsed);
    LayoutChildren();
    UpdateTitle();
    RefreshFormatState();
    ApplyTooltipTheme();
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
        SetFindBarDpi(g_app.dpi);
        SetEditorDpi(g_app.editor, g_app.dpi);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        // 크기가 바뀔 때 시스템이 이전 배율의 화면을 그대로 복사해 두는데, RichEdit는 본문 영역만 다시 그려
        // 여백(EM_SETRECT 바깥)에 이전 글자 조각이 남는다. 창 전체를 다시 그리게 한다.
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;
    }

    case WM_ACTIVATE:
        // 다른 창으로 전환될 때 포커스가 있던 곳(에디터 또는 찾기 입력란)을 기억해 둔다
        if (LOWORD(wp) == WA_INACTIVE) {
            HWND focus = GetFocus();
            g_app.restoreFocus = (focus && IsChild(hwnd, focus)) ? focus : nullptr;
        }
        break;

    case WM_SETFOCUS: {
        // 창이 다시 활성화되면 기억해 둔 곳에, 없으면 편집 영역에 입력 포커스를 준다
        HWND target = g_app.restoreFocus;
        if (!target || !IsWindowVisible(target)) target = g_app.editor;
        if (target) SetFocus(target);
        return 0;
    }

    case WM_CLOSE:
        // 저장되지 않은 변경이 있으면 확인한 뒤에만 창을 닫는다 (File > Exit도 여기로 온다)
        if (ConfirmDiscardChanges()) DestroyWindow(hwnd);
        return 0;

    case WM_QUERYENDSESSION:
        // 로그오프/종료 시에도 저장 여부를 묻는다. 취소하면 종료를 막는다.
        return ConfirmDiscardChanges() ? TRUE : FALSE;

    case WM_ENDSESSION:
        // 로그오프/종료 때는 WM_DESTROY 없이 프로세스가 끝나므로 여기서 창 상태를 저장한다
        if (wp) SaveWindowState();
        return 0;

    case WM_MOTE_RELAYOUT:
        LayoutChildren();
        return 0;

    case WM_SETTINGCHANGE:
        // 라이트/다크 앱 모드가 바뀌면 lParam이 "ImmersiveColorSet"인 알림이 온다
        if (lp && CompareStringOrdinal(reinterpret_cast<LPCWSTR>(lp), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL) {
            OnSystemThemeChanged();
        }
        break;

    case WM_INITMENUPOPUP:
        UpdateMenuState(reinterpret_cast<HMENU>(wp));
        return 0;

    case WM_COMMAND:
        // 메뉴/가속키/컨텍스트 메뉴 명령. 에디터 알림(EN_*)은 lParam이 컨트롤 핸들이다.
        if (lp == 0 || reinterpret_cast<HWND>(lp) != g_app.editor) {
            OnCommand(LOWORD(wp));
        } else if (HIWORD(wp) == EN_CHANGE) {
            OnEditorChanged();  // 입력/삭제/붙여넣기/실행 취소 → 제목의 수정 표시 갱신
            NotifyFindBarDocumentChanged();
        }
        return 0;

    case WM_NOTIFY: {
        // 캐럿/선택이 바뀔 때마다 B/I/U 활성 상태와 색 표시를 갱신한다.
        // 모두 바꾸기처럼 EN_CHANGE가 오지 않을 수 있는 경우를 위해 수정 표시도 함께 확인한다.
        const auto* header = reinterpret_cast<const NMHDR*>(lp);
        if (header->hwndFrom == g_app.editor && header->code == EN_SELCHANGE) {
            RefreshFormatState();
            OnEditorChanged();
            NotifyFindBarDocumentChanged();  // 캐럿이 옮겨지면 "현재 번호"가 바뀐다
        }
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
        SaveWindowState();
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
        MessageBoxW(nullptr, Str(StrId::ErrorRichEdit), kAppName, MB_ICONERROR);
        return 1;
    }

    // 창을 만들기 전에 시스템 테마(라이트/다크)를 정해 처음부터 맞는 색으로 그린다
    UpdateThemeFromSystem();
    ApplyMenuTheme();

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = MainWindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // 큰 아이콘(Alt+Tab/작업 표시줄)과 작은 아이콘(제목 표시줄)을 각각 알맞은 크기로 불러온다
    LoadIconMetric(instance, MAKEINTRESOURCEW(IDI_APP), LIM_LARGE, &wc.hIcon);
    LoadIconMetric(instance, MAKEINTRESOURCEW(IDI_APP), LIM_SMALL, &wc.hIconSm);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    HWND window = CreateWindowExW(0, kWindowClass, kAppName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                  CW_USEDEFAULT, CW_USEDEFAULT, kDefaultWidth, kDefaultHeight,
                                  nullptr, nullptr, instance, nullptr);
    if (!window) {
        MessageBoxW(nullptr, Str(StrId::ErrorCreateWindow), kAppName, MB_ICONERROR);
        return 1;
    }
    g_app.accelerators = LoadAcceleratorsW(instance, MAKEINTRESOURCEW(IDR_ACCEL));

    ShowWindow(window, RestoreWindowState(window, showCommand));
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
        // 본문에서 Esc를 눌러도 찾기 바를 닫는다 (RichEdit는 Esc를 쓰지 않는다)
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && msg.hwnd == g_app.editor && IsFindBarVisible()) {
            HideFindBar();
            continue;
        }
        if (TranslateAcceleratorW(window, g_app.accelerators, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    OleUninitialize();
    return static_cast<int>(msg.wParam);
}
