#include "editor.h"
#include "theme.h"

#include <commctrl.h>
#include <windowsx.h>
#include <richedit.h>
#include <richole.h>
#include <shellapi.h>
#include <tom.h>
#include <algorithm>

// 최신 SDK에만 있는 RichEdit 언어 옵션 비트
#ifndef IMF_SPELLCHECKING
#define IMF_SPELLCHECKING 0x0800
#endif
#ifndef IMF_TKBPREDICTION
#define IMF_TKBPREDICTION 0x1000
#endif
#ifndef IMF_TKBAUTOCORRECTION
#define IMF_TKBAUTOCORRECTION 0x2000
#endif

namespace {

constexpr LONG kFontSizeTwips = 230;     // 11.5pt
constexpr float kLineSpacing = 1.2f;     // 기본 RichEdit보다 약간 여유로운 줄간격
constexpr int kPaddingX = 32;            // 본문 좌우 여백 (96 DPI 기준)
constexpr int kPaddingTop = 22;          // 본문 위 여백

// SDK 버전에 따라 IID 정의가 라이브러리에 없을 수 있어 직접 정의한다
constexpr GUID kIID_IRichEditOleCallback = {0x00020D03, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
constexpr GUID kIID_ITextDocument = {0x8CC497C0, 0xA1DF, 0x11CE, {0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D}};

UINT g_dpi = 96;
HMENU g_contextMenu = nullptr;
ITextDocument* g_textDoc = nullptr;  // 문서 단위 작업(색 교체, 줄간격)에 사용하는 TOM 인터페이스
bool g_internalDrag = false;          // 이 에디터 안에서 시작된 드래그인지
bool g_fileDragOver = false;          // 탐색기에서 파일을 끌어 본문 위에 올려 둔 상태인지
HWND g_editor = nullptr;

// 클립보드 내용이 이 프로세스(=mote)에서 복사된 것인지 확인한다.
// RichEdit는 OLE 클립보드를 쓰므로 소유 창은 같은 프로세스의 OLE 숨김 창이다.
bool IsClipboardFromThisProcess()
{
    HWND owner = GetClipboardOwner();
    if (!owner) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(owner, &pid);
    return pid == GetCurrentProcessId();
}

bool DataObjectHas(IDataObject* data, CLIPFORMAT format)
{
    FORMATETC fe = {format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return data && data->QueryGetData(&fe) == S_OK;
}

// 탐색기에서 끌어온 파일 목록(CF_HDROP)의 첫 번째 경로
std::wstring FirstDroppedFile(IDataObject* data)
{
    FORMATETC fe = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium = {};
    if (FAILED(data->GetData(&fe, &medium))) return {};
    std::wstring path;
    const auto drop = static_cast<HDROP>(medium.hGlobal);
    const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
    if (length > 0) {
        path.resize(length + 1);
        DragQueryFileW(drop, 0, path.data(), length + 1);
        path.resize(length);
    }
    ReleaseStgMedium(&medium);
    return path;
}

// 붙여넣기/드롭/개체 삽입 정책을 결정하는 RichEdit 콜백.
// - 이미지·OLE 개체 삽입 차단
// - 외부에서 온 데이터는 일반 텍스트로만 받아 웹 글꼴/크기 등이 섞이지 않게 한다
// - mote 안에서 복사/드래그한 내용은 RTF로 받아 글자색을 유지한다
class OleCallback final : public IRichEditOleCallback {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == kIID_IRichEditOleCallback) {
            *ppv = static_cast<IRichEditOleCallback*>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    // 정적 객체이므로 참조 계수를 관리하지 않는다
    STDMETHODIMP_(ULONG) AddRef() override { return 1; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    // 저장소를 주지 않으면 OLE 개체(이미지 등)를 넣을 수 없다
    STDMETHODIMP GetNewStorage(LPSTORAGE*) override { return E_NOTIMPL; }
    STDMETHODIMP GetInPlaceContext(LPOLEINPLACEFRAME*, LPOLEINPLACEUIWINDOW*, LPOLEINPLACEFRAMEINFO) override { return E_NOTIMPL; }
    STDMETHODIMP ShowContainerUI(BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP QueryInsertObject(LPCLSID, LPSTORAGE, LONG) override { return S_FALSE; }
    STDMETHODIMP DeleteObject(LPOLEOBJECT) override { return S_OK; }
    STDMETHODIMP ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP GetContextMenu(WORD, LPOLEOBJECT, CHARRANGE*, HMENU*) override { return E_NOTIMPL; }
    STDMETHODIMP GetDragDropEffect(BOOL drag, DWORD, LPDWORD effect) override
    {
        // 파일은 본문에 넣지 않고 열기만 하므로 항상 "복사"로 표시한다.
        // ("이동"으로 알리면 탐색기가 원본 파일을 지울 수 있다)
        if (!drag && g_fileDragOver && effect) *effect = (*effect & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }

    STDMETHODIMP GetClipboardData(CHARRANGE*, DWORD reco, LPDATAOBJECT*) override
    {
        // 드래그를 시작할 때도 호출되므로 "내부 드래그" 여부를 기록해 둔다
        if (reco == RECO_DRAG) g_internalDrag = true;
        return E_NOTIMPL;  // 데이터 개체는 RichEdit 기본 구현을 사용
    }

    STDMETHODIMP QueryAcceptData(LPDATAOBJECT data, CLIPFORMAT* format, DWORD reco, BOOL really, HGLOBAL) override
    {
        if (*format != 0) return S_OK;  // 특정 형식을 명시한 붙여넣기는 그대로 둔다

        // 탐색기에서 끌어온 파일: 끄는 동안은 받아들이고, 놓으면 부모 창에 열기를 맡긴다.
        // 본문에는 아무것도 넣지 않도록 실제 드롭은 거절한다.
        g_fileDragOver = reco == RECO_DROP && DataObjectHas(data, CF_HDROP);
        if (g_fileDragOver) {
            if (!really) return S_OK;
            g_fileDragOver = false;
            std::wstring path = FirstDroppedFile(data);
            if (!path.empty()) {
                // 드롭 처리(OLE 모달 루프)가 끝난 뒤에 열도록 보낸다. 저장 확인창이 뜰 수 있기 때문이다.
                auto* message = new std::wstring(std::move(path));
                if (!PostMessageW(GetParent(g_editor), WM_MOTE_OPENFILE, 0, reinterpret_cast<LPARAM>(message))) delete message;
            }
            return E_FAIL;
        }

        static const CLIPFORMAT rtfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CF_RTF));
        const bool internal = (reco == RECO_DROP) ? g_internalDrag : IsClipboardFromThisProcess();
        if (reco == RECO_DROP && really) g_internalDrag = false;

        if (internal && DataObjectHas(data, rtfFormat)) {
            *format = rtfFormat;
            return S_OK;
        }
        if (DataObjectHas(data, CF_UNICODETEXT)) {
            *format = CF_UNICODETEXT;
            return S_OK;
        }
        return E_FAIL;  // 텍스트가 없는 데이터(이미지 등)는 받지 않는다
    }
};

OleCallback g_oleCallback;

// 워드프로세서용 RichEdit 기본 단축키를 막는다.
// (Ctrl+L/E/R/J 정렬, Ctrl+1/2/5 줄간격, Ctrl+= 아래첨자, Ctrl+Shift+A 대문자, Ctrl+Shift+L 목록 등)
// 메모장에 필요한 편집/이동 단축키만 허용 목록으로 통과시킨다.
bool IsBlockedShortcut(WPARAM vk)
{
    if (GetKeyState(VK_CONTROL) >= 0) return false;
    if (GetKeyState(VK_MENU) < 0) return false;  // AltGr(=Ctrl+Alt)로 입력하는 문자는 막지 않는다
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    switch (vk) {
    case 'C': case 'X': case 'Z': case 'Y':
        return false;
    case 'V': case 'A':
        return shift;
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_BACK: case VK_DELETE: case VK_INSERT:
    case VK_CONTROL: case VK_SHIFT: case VK_MENU: case VK_PROCESSKEY:
        return false;
    default:
        return true;
    }
}

void ApplyPadding(HWND editor)
{
    RECT rc;
    GetClientRect(editor, &rc);
    rc.left += Scale(kPaddingX, g_dpi);
    rc.right -= Scale(kPaddingX, g_dpi);
    rc.top += Scale(kPaddingTop, g_dpi);
    if (rc.right <= rc.left) rc.right = rc.left + 1;
    if (rc.bottom <= rc.top) rc.bottom = rc.top + 1;
    SendMessageW(editor, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&rc));
}

// range의 서식 사본 (호출자가 Release)
ITextFont* DuplicateRangeFont(ITextRange* range)
{
    ITextFont* duplicate = nullptr;
    ITextFont* font = nullptr;
    if (SUCCEEDED(range->GetFont(&font)) && font) {
        font->GetDuplicate(&duplicate);
        font->Release();
    }
    return duplicate;
}

// 첫 번째 보이는 글자의 서식 사본 (문서가 비어 있으면 nullptr, 호출자가 Release).
// 앞쪽 빈 줄의 단락 기호는 색이 보이지 않으므로 건너뛴다.
ITextFont* DuplicateLeadingTextFont()
{
    if (!g_textDoc) return nullptr;
    ITextRange* range = nullptr;
    if (FAILED(g_textDoc->Range(0, 0, &range)) || !range) return nullptr;
    ITextFont* duplicate = nullptr;
    long storyLength = 0;
    range->GetStoryLength(&storyLength);
    if (storyLength > 1) {  // 끝의 단락 기호(EOP)만 있으면 빈 문서
        const long last = storyLength - 1;
        long cp = 0;
        for (; cp < last; ++cp) {
            range->SetRange(cp, cp + 1);
            long ch = 0;
            if (FAILED(range->GetChar(&ch)) || ch != L'\r') break;
        }
        if (cp == last) cp = 0;  // 단락 기호뿐이면 첫 글자를 쓴다
        range->SetRange(cp, cp + 1);
        duplicate = DuplicateRangeFont(range);
    }
    range->Release();
    return duplicate;
}

// 입력 위치(선택)의 서식 사본 (호출자가 Release)
ITextFont* DuplicateSelectionFont()
{
    ITextSelection* selection = nullptr;
    if (!g_textDoc || FAILED(g_textDoc->GetSelection(&selection)) || !selection) return nullptr;
    ITextFont* duplicate = DuplicateRangeFont(selection);
    selection->Release();
    return duplicate;
}

// cp 위치의 글자 (읽을 수 없으면 0)
long CharAt(long cp)
{
    ITextRange* range = nullptr;
    if (cp < 0 || !g_textDoc || FAILED(g_textDoc->Range(cp, cp + 1, &range)) || !range) return 0;
    long ch = 0;
    range->GetChar(&ch);
    range->Release();
    return ch;
}

// 캐럿(빈 선택)이 빈 단락에 있으면 그 단락 기호의 위치를 cp에 담는다
bool IsCaretInEmptyParagraph(long* cp)
{
    CHARRANGE sel;
    SendMessageW(g_editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    if (sel.cpMin != sel.cpMax) return false;
    if (CharAt(sel.cpMin) != L'\r') return false;
    if (sel.cpMin > 0 && CharAt(sel.cpMin - 1) != L'\r') return false;
    *cp = sel.cpMin;
    return true;
}

bool IsDocumentEmpty()
{
    ITextRange* range = nullptr;
    if (!g_textDoc || FAILED(g_textDoc->Range(0, 0, &range)) || !range) return false;
    long storyLength = 0;
    range->GetStoryLength(&storyLength);
    range->Release();
    return storyLength <= 1;
}

// 입력 위치 서식을 font로 정한다.
// 캐럿이 빈 단락에 있으면 그 단락 기호에도 입힌다. RichEdit는 빈 단락에 캐럿이 다시 놓일 때(클릭, 방향키 등)
// 입력 서식을 단락 기호에서 읽어 오므로, 입력 위치에만 정해 두면 곧 기본색으로 돌아가기 때문이다.
// 수정 표시와 실행 취소 기록에는 남기지 않는다.
void ApplyInsertionFont(HWND editor, ITextFont* font)
{
    ITextSelection* selection = nullptr;
    if (FAILED(g_textDoc->GetSelection(&selection)) || !selection) return;
    const LPARAM wasModified = SendMessageW(editor, EM_GETMODIFY, 0, 0);
    g_textDoc->Undo(tomSuspend, nullptr);
    long cp = 0;
    ITextRange* mark = nullptr;
    if (IsCaretInEmptyParagraph(&cp) && SUCCEEDED(g_textDoc->Range(cp, cp + 1, &mark)) && mark) {
        mark->SetFont(font);
        mark->Release();
    }
    selection->SetFont(font);  // 문서 서식을 바꾸면 입력 서식이 다시 계산될 수 있어 마지막에 정한다
    g_textDoc->Undo(tomResume, nullptr);
    SendMessageW(editor, EM_SETMODIFY, wasModified, 0);
    selection->Release();

    // 삭제 중에 온 선택 변경 알림은 바뀌기 전 서식을 읽었으므로, 상단 바 색 표시를 다시 갱신하게 알린다
    SELCHANGE notify = {};
    notify.nmhdr.hwndFrom = editor;
    notify.nmhdr.idFrom = static_cast<UINT_PTR>(GetDlgCtrlID(editor));
    notify.nmhdr.code = EN_SELCHANGE;
    SendMessageW(editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&notify.chrg));
    notify.seltyp = SEL_EMPTY;
    SendMessageW(GetParent(editor), WM_NOTIFY, notify.nmhdr.idFrom, reinterpret_cast<LPARAM>(&notify));
}

// 본문을 지울 수 있는 입력인지 (Backspace/Delete/Ctrl+Backspace/잘라내기)
bool IsDeletingMessage(UINT msg, WPARAM wp)
{
    switch (msg) {
    case WM_KEYDOWN: return wp == VK_BACK || wp == VK_DELETE;
    case WM_CHAR: return wp == 0x08 || wp == 0x7F;
    case WM_CUT: return true;
    default: return false;
    }
}

// 줄을 바꾸는 입력인지 (Enter)
bool IsNewLineMessage(UINT msg, WPARAM wp)
{
    return (msg == WM_KEYDOWN && wp == VK_RETURN) || (msg == WM_CHAR && wp == L'\r');
}

// 캐럿이 빈 단락에 있으면 지금 입력 서식을 그 단락 기호에도 입혀 둔다
void KeepInsertionFontOnEmptyParagraph(HWND editor)
{
    long cp = 0;
    if (!g_textDoc || !IsCaretInEmptyParagraph(&cp)) return;
    ITextFont* font = DuplicateSelectionFont();
    if (!font) return;
    ApplyInsertionFont(editor, font);
    font->Release();
}

void ShowContextMenu(HWND editor, POINT screenPt)
{
    if (!g_contextMenu) return;
    // 명령은 부모(메인 창)로 보내 메뉴와 같은 경로로 처리한다
    TrackPopupMenuEx(g_contextMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                     screenPt.x, screenPt.y, GetParent(editor), nullptr);
}

LRESULT CALLBACK EditorSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    // 본문을 모두 지우면 RichEdit는 끝의 단락 기호 서식(기본색)으로 돌아간다.
    // Ctrl+A 선택에는 단락 기호가 들어가지 않아 사용자가 고른 색이 사라지므로,
    // 지운 첫 글자의 서식을 빈 문서의 입력 서식으로 이어받게 한다 (워드와 같은 동작).
    if (IsDeletingMessage(msg, wp)) {
        ITextFont* font = DuplicateLeadingTextFont();
        const LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
        if (font) {
            if (IsDocumentEmpty()) ApplyInsertionFont(hwnd, font);
            font->Release();
        }
        return result;
    }

    // Enter로 생긴 빈 줄의 끝은 원래 있던 단락 기호(기본색)라서, 그대로 두면 색이 기본색으로 돌아간다.
    // Enter 전의 입력 서식을 새 빈 줄에도 이어 준다. (Ctrl+Enter 등 막는 단축키는 아래에서 처리)
    if (IsNewLineMessage(msg, wp) && !(msg == WM_KEYDOWN && IsBlockedShortcut(wp))) {
        ITextFont* font = DuplicateSelectionFont();
        const LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
        if (font) {
            long cp = 0;
            if (IsCaretInEmptyParagraph(&cp)) ApplyInsertionFont(hwnd, font);
            font->Release();
        }
        return result;
    }

    switch (msg) {
    case WM_KEYDOWN:
        if (IsBlockedShortcut(wp)) {
            // TranslateMessage가 이미 넣어 둔 제어 문자(WM_CHAR)도 함께 버려야
            // 문서에 이상한 제어 문자가 들어가지 않는다
            MSG pending;
            PeekMessageW(&pending, hwnd, WM_CHAR, WM_CHAR, PM_REMOVE);
            return 0;
        }
        break;

    case WM_LBUTTONDOWN:
        g_internalDrag = false;  // 새 마우스 동작이 시작되면 이전 드래그 기록은 무효
        break;

    case WM_RBUTTONDOWN: {
        // 선택 영역 밖을 우클릭하면 메모장처럼 캐럿을 그 위치로 옮긴다
        POINTL pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const LONG cp = static_cast<LONG>(SendMessageW(hwnd, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&pt)));
        CHARRANGE sel;
        SendMessageW(hwnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
        if (cp < sel.cpMin || cp > sel.cpMax) {
            CHARRANGE caret = {cp, cp};
            SendMessageW(hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&caret));
        }
        SetFocus(hwnd);
        return 0;
    }

    case WM_RBUTTONUP: {
        // RichEdit는 기본 컨텍스트 메뉴가 없으므로 직접 띄운다
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(hwnd, &pt);
        ShowContextMenu(hwnd, pt);
        return 0;
    }

    case WM_CONTEXTMENU: {
        // 메뉴 키 / Shift+F10: 좌표가 (-1,-1)이면 캐럿 위치에 띄운다
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (pt.x == -1 && pt.y == -1) {
            CHARRANGE sel;
            SendMessageW(hwnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
            POINTL pos = {};
            SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&pos), sel.cpMax);
            pt = {pos.x, pos.y + Scale(20, g_dpi)};
            ClientToScreen(hwnd, &pt);
        }
        ShowContextMenu(hwnd, pt);
        return 0;
    }

    case WM_SIZE: {
        // RichEdit는 크기가 바뀌면 서식 영역을 초기화하므로 여백을 다시 적용한다.
        // 세로 스크롤바가 나타나거나 사라질 때도 WM_SIZE가 온다.
        const LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
        ApplyPadding(hwnd);
        return result;
    }

    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, EditorSubclassProc, 0);
        if (g_textDoc) {
            g_textDoc->Release();
            g_textDoc = nullptr;
        }
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

CHARFORMAT2W DefaultCharFormat()
{
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    cf.dwEffects = 0;  // CFE_AUTOCOLOR를 쓰지 않는다: 자동 색은 시스템 검정으로 고정되기 때문
    cf.yHeight = kFontSizeTwips;
    cf.crTextColor = DefaultTextColor();
    wcsncpy_s(cf.szFaceName, kEditorFontFace, _TRUNCATE);
    return cf;
}

// 문서 전체를 가리키는 TOM 범위 (호출자가 Release)
ITextRange* WholeStoryRange()
{
    ITextRange* range = nullptr;
    if (!g_textDoc || FAILED(g_textDoc->Range(0, 0, &range)) || !range) return nullptr;
    range->Expand(tomStory, nullptr);
    return range;
}

void ApplyLineSpacing()
{
    ITextRange* range = WholeStoryRange();
    if (!range) return;
    ITextPara* para = nullptr;
    if (SUCCEEDED(range->GetPara(&para)) && para) {
        para->SetLineSpacing(tomLineSpaceMultiple, kLineSpacing);
        para->SetSpaceBefore(0);
        para->SetSpaceAfter(0);
        para->Release();
    }
    range->Release();
}

}  // namespace

COLORREF DefaultTextColor()
{
    return CurrentTheme().text;
}

HWND CreateEditor(HWND parent, HINSTANCE instance, HMENU contextMenu)
{
    g_contextMenu = contextMenu;
    g_dpi = GetDpiForWindow(parent);

    // WS_EX_CLIENTEDGE를 주지 않아 3D 테두리가 없는 편집 영역을 만든다
    HWND editor = CreateWindowExW(
        0, MSFTEDIT_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPSIBLINGS |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL,
        0, 0, 0, 0, parent, nullptr, instance, nullptr);
    if (!editor) return nullptr;
    g_editor = editor;

    // 텍스트 모드는 문서가 비어 있을 때만 바꿀 수 있으므로 가장 먼저 설정한다
    SendMessageW(editor, EM_SETTEXTMODE, TM_RICHTEXT | TM_MULTILEVELUNDO | TM_MULTICODEPAGE, 0);
    SendMessageW(editor, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
    SendMessageW(editor, EM_SETOLECALLBACK, 0, reinterpret_cast<LPARAM>(&g_oleCallback));

    // 맞춤법 검사(빨간 밑줄)와 터치 키보드 자동 교정/추천을 명시적으로 끈다.
    // 글꼴 자동 연결(IMF_AUTOFONT/IMF_DUALFONT)도 끈다. 켜 두면 한글이 나올 때마다 맑은 고딕 구간을 따로 만들어
    // 한글이 많은 큰 파일을 열 때 수십 초가 걸리고(50MB 기준 30초 → 3초), 한글이 있는 줄만 줄 간격이 넓어진다.
    // 꺼도 한글은 Windows 글꼴 대체로 같은 모양(맑은 고딕)으로 그려진다.
    LRESULT lang = SendMessageW(editor, EM_GETLANGOPTIONS, 0, 0);
    lang &= ~static_cast<LRESULT>(IMF_SPELLCHECKING | IMF_TKBPREDICTION | IMF_TKBAUTOCORRECTION |
                                  IMF_AUTOFONT | IMF_DUALFONT | IMF_AUTOFONTSIZEADJUST);
    SendMessageW(editor, EM_SETLANGOPTIONS, 0, lang);
    SendMessageW(editor, EM_AUTOURLDETECT, 0, 0);

    // IME를 TSF로 받는다. 예전 방식(IMM32)에서는 한글을 조합하는 동안 글자가 검은 블록으로 반전되어 보이는데,
    // TSF에서는 메모장처럼 얇은 커서로 보인다. 맞춤법/스마트 태그 같은 TSF 부가 기능(SES_CTFALLOW*)은 켜지 않는다.
    SendMessageW(editor, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);

    SendMessageW(editor, EM_SETBKGNDCOLOR, 0, CurrentTheme().windowBg);
    ApplyControlTheme(editor);
    SendMessageW(editor, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);

    // 문서 단위 작업에 쓸 TOM 인터페이스를 얻어 둔다
    IRichEditOle* ole = nullptr;
    if (SendMessageW(editor, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&ole)) && ole) {
        ole->QueryInterface(kIID_ITextDocument, reinterpret_cast<void**>(&g_textDoc));
        ole->Release();
    }

    SetWindowSubclass(editor, EditorSubclassProc, 0, 0);
    ResetEditorDocument(editor);
    return editor;
}

void ApplyEditorTheme(HWND editor, COLORREF previousText)
{
    SendMessageW(editor, EM_SETBKGNDCOLOR, 0, CurrentTheme().windowBg);
    ApplyControlTheme(editor);  // 세로 스크롤바
    ReplaceTextColor(editor, static_cast<long>(previousText), static_cast<long>(DefaultTextColor()));

    // 빈 문서에서 새로 입력할 글자의 색. 색만 바꾸므로 글꼴 등 다른 기본 서식은 건드리지 않는다.
    // 테마 변경은 문서 수정이 아니므로 수정 상태를 그대로 유지한다.
    const LPARAM wasModified = SendMessageW(editor, EM_GETMODIFY, 0, 0);
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = DefaultTextColor();
    SendMessageW(editor, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(editor, EM_SETMODIFY, wasModified, 0);
    InvalidateRect(editor, nullptr, TRUE);
}

void SetEditorDpi(HWND editor, UINT dpi)
{
    g_dpi = dpi;
    // RichEdit는 DPI 변경 알림을 받기 전까지 주 모니터(시스템) DPI로 글자를 그린다.
    // 배율이 다른 보조 모니터에서 시작하면 글자가 그만큼 크거나 작게 나오므로 직접 알려 준다.
    // (모니터를 옮길 때 시스템이 자식 창에 보내는 순서 그대로 보낸다)
    SendMessageW(editor, WM_DPICHANGED_BEFOREPARENT, 0, 0);
    SendMessageW(editor, WM_DPICHANGED_AFTERPARENT, 0, 0);
    ApplyPadding(editor);
}

void ResetEditorDocument(HWND editor)
{
    SetWindowTextW(editor, L"");
    CHARFORMAT2W cf = DefaultCharFormat();
    SendMessageW(editor, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(editor, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    ApplyLineSpacing();
    MarkEditorClean(editor);
}

void ApplyPlainTextFormatting(HWND editor)
{
    CHARFORMAT2W cf = DefaultCharFormat();
    SendMessageW(editor, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(editor, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    ApplyLineSpacing();
}

void NormalizeLoadedRichText(HWND editor)
{
    // 주의: 여기서 SCF_DEFAULT를 바꾸면 기본 서식을 공유하는 구간의 서식까지 바뀌므로 건드리지 않는다.
    // 파일의 "자동 색"은 현재 테마 기본색으로 보여 준다 (다크 모드에서도 읽히도록)
    ReplaceTextColor(editor, kAutoColor, static_cast<long>(DefaultTextColor()));
    ApplyLineSpacing();
}

void ReplaceTextColor(HWND editor, long from, long to)
{
    if (!g_textDoc || from == to) return;

    const LPARAM wasModified = SendMessageW(editor, EM_GETMODIFY, 0, 0);
    long freezeCount = 0;
    g_textDoc->Freeze(&freezeCount);          // 중간 상태가 화면에 그려지지 않게 한다
    g_textDoc->Undo(tomSuspend, nullptr);     // 색 교체가 실행 취소 기록에 남지 않게 한다

    ITextRange* range = nullptr;
    if (SUCCEEDED(g_textDoc->Range(0, 0, &range)) && range) {
        long storyLength = 0;
        range->GetStoryLength(&storyLength);
        long pos = 0;
        // 같은 서식이 이어지는 구간(run) 단위로 훑으며 색이 일치하는 구간만 바꾼다
        while (pos < storyLength) {
            range->SetRange(pos, pos);
            long moved = 0;
            if (FAILED(range->MoveEnd(tomCharFormat, 1, &moved)) || moved == 0) break;
            long end = pos;
            range->GetEnd(&end);
            if (end <= pos) break;

            ITextFont* font = nullptr;
            if (SUCCEEDED(range->GetFont(&font)) && font) {
                long color = 0;
                if (SUCCEEDED(font->GetForeColor(&color)) && color == from) font->SetForeColor(to);
                font->Release();
            }
            pos = end;
        }
        range->Release();
    }

    g_textDoc->Undo(tomResume, nullptr);
    g_textDoc->Unfreeze(&freezeCount);
    SendMessageW(editor, EM_SETMODIFY, wasModified, 0);
}

FormatState GetSelectionFormat(HWND editor)
{
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    SendMessageW(editor, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));

    // dwMask에 비트가 있으면 선택 영역 전체가 같은 값이라는 뜻이다
    FormatState state;
    state.bold = (cf.dwMask & CFM_BOLD) && (cf.dwEffects & CFE_BOLD);
    state.italic = (cf.dwMask & CFM_ITALIC) && (cf.dwEffects & CFE_ITALIC);
    state.underline = (cf.dwMask & CFM_UNDERLINE) && (cf.dwEffects & CFE_UNDERLINE);
    state.colorMixed = !(cf.dwMask & CFM_COLOR);
    state.color = (cf.dwEffects & CFE_AUTOCOLOR) ? DefaultTextColor() : cf.crTextColor;
    return state;
}

void ToggleSelectionEffect(HWND editor, DWORD effect)
{
    // CFE_* 효과 비트와 CFM_* 마스크 비트는 같은 값이다 (CFE_BOLD == CFM_BOLD 등)
    CHARFORMAT2W current = {};
    current.cbSize = sizeof(current);
    SendMessageW(editor, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&current));
    const bool allOn = (current.dwMask & effect) && (current.dwEffects & effect);

    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = effect;
    cf.dwEffects = allOn ? 0 : effect;
    if (effect == CFE_UNDERLINE) {
        cf.dwMask |= CFM_UNDERLINETYPE;
        cf.bUnderlineType = allOn ? CFU_UNDERLINENONE : CFU_UNDERLINE;
    }
    SendMessageW(editor, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    KeepInsertionFontOnEmptyParagraph(editor);
}

void SetSelectionColor(HWND editor, COLORREF color)
{
    // CFE_AUTOCOLOR를 끄고 명시 색을 지정한다 (기본색도 명시 색으로 다룬다: DefaultCharFormat 참고)
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.dwEffects = 0;
    cf.crTextColor = color;
    SendMessageW(editor, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    KeepInsertionFontOnEmptyParagraph(editor);  // 빈 줄에서 고른 색이 캐럿을 옮겨도 남도록
}

// ---- 찾기 / 바꾸기 ----

namespace {

LONG TextLength(HWND editor)
{
    GETTEXTLENGTHEX gtl = {GTL_NUMCHARS | GTL_PRECISE, 1200};
    return static_cast<LONG>(SendMessageW(editor, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gtl), 0));
}

// chrg 범위에서 text를 찾는다. FR_DOWN이 없으면 cpMin에서 cpMax(더 작은 위치) 쪽으로 거꾸로 찾는다.
bool FindRange(HWND editor, const std::wstring& text, LONG from, LONG to, bool forward, CHARRANGE& found)
{
    FINDTEXTEXW ft = {};
    ft.chrg = {from, to};
    ft.lpstrText = text.c_str();
    const LRESULT pos = SendMessageW(editor, EM_FINDTEXTEXW, forward ? FR_DOWN : 0, reinterpret_cast<LPARAM>(&ft));
    if (pos < 0) return false;
    found = ft.chrgText;
    return true;
}

std::wstring SelectedText(HWND editor)
{
    CHARRANGE sel;
    SendMessageW(editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    if (sel.cpMax <= sel.cpMin) return {};
    std::wstring text(static_cast<size_t>(sel.cpMax - sel.cpMin) + 1, L'\0');
    const LRESULT length = SendMessageW(editor, EM_GETSELTEXT, 0, reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<size_t>(std::max<LRESULT>(0, std::min<LRESULT>(length, static_cast<LRESULT>(text.size())))));
    return text;
}

bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b)
{
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

}  // namespace

bool FindInEditor(HWND editor, const std::wstring& text, bool forward, bool fromSelectionStart)
{
    if (text.empty()) return false;
    CHARRANGE sel;
    SendMessageW(editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));

    CHARRANGE found = {};
    bool ok;
    if (forward) {
        const LONG start = fromSelectionStart ? sel.cpMin : sel.cpMax;
        ok = FindRange(editor, text, start, -1, true, found) || FindRange(editor, text, 0, -1, true, found);
    } else {
        ok = FindRange(editor, text, sel.cpMin, 0, false, found) ||
             FindRange(editor, text, TextLength(editor), 0, false, found);
    }
    if (!ok) return false;

    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&found));
    SendMessageW(editor, EM_SCROLLCARET, 0, 0);
    return true;
}

bool ReplaceCurrent(HWND editor, const std::wstring& find, const std::wstring& replacement)
{
    if (find.empty()) return false;
    if (EqualsIgnoreCase(SelectedText(editor), find)) {
        // EM_REPLACESEL은 선택 영역 첫 글자의 서식을 이어받으므로 글자색/굵게 등이 유지된다
        SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(replacement.c_str()));
        return FindInEditor(editor, find, true);
    }
    // 아직 일치 항목이 선택되지 않았으면 먼저 찾기만 한다
    return FindInEditor(editor, find, true, true);
}

int ReplaceAllInEditor(HWND editor, const std::wstring& find, const std::wstring& replacement)
{
    if (find.empty()) return 0;

    CHARRANGE original;
    SendMessageW(editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&original));

    // 바꾸는 동안 화면 갱신과 선택 변경 알림(상단 바 갱신)을 멈춘다
    const LRESULT eventMask = SendMessageW(editor, EM_GETEVENTMASK, 0, 0);
    SendMessageW(editor, EM_SETEVENTMASK, 0, eventMask & ~ENM_SELCHANGE);
    long freezeCount = 0;
    if (g_textDoc) {
        g_textDoc->Freeze(&freezeCount);
        // 모든 치환을 실행 취소 한 단계로 묶는다 (지원하지 않는 환경이면 단계별로 남는다)
        g_textDoc->BeginEditCollection();
    }

    int count = 0;
    LONG pos = 0;
    CHARRANGE found = {};
    while (FindRange(editor, find, pos, -1, true, found)) {
        SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&found));
        SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(replacement.c_str()));
        pos = found.cpMin + static_cast<LONG>(replacement.size());
        ++count;
    }

    if (g_textDoc) {
        g_textDoc->EndEditCollection();
        g_textDoc->Unfreeze(&freezeCount);
    }
    SendMessageW(editor, EM_SETEVENTMASK, 0, eventMask);

    // 캐럿은 원래 선택의 시작 위치로 돌려 놓는다 (이 선택 변경으로 상단 바/제목도 갱신된다)
    const LONG caret = std::min(original.cpMin, TextLength(editor));
    CHARRANGE restore = {caret, caret};
    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&restore));
    SendMessageW(editor, EM_SCROLLCARET, 0, 0);
    return count;
}

MatchCount CountMatches(HWND editor, const std::wstring& text, int limit)
{
    MatchCount result;
    if (text.empty()) return result;
    CHARRANGE sel;
    SendMessageW(editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));

    LONG pos = 0;
    CHARRANGE found = {};
    while (FindRange(editor, text, pos, -1, true, found)) {
        if (result.total == limit) {
            result.capped = true;
            break;
        }
        ++result.total;
        if (found.cpMin == sel.cpMin && found.cpMax == sel.cpMax) result.current = result.total;
        pos = std::max(found.cpMax, found.cpMin + 1);
    }
    return result;
}

std::wstring GetSingleLineSelection(HWND editor, size_t maxLength)
{
    CHARRANGE sel;
    SendMessageW(editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    if (sel.cpMax <= sel.cpMin || static_cast<size_t>(sel.cpMax - sel.cpMin) > maxLength) return {};
    std::wstring text = SelectedText(editor);
    if (text.find_first_of(L"\r\n\v") != std::wstring::npos) return {};
    return text;
}

void MarkEditorClean(HWND editor)
{
    SendMessageW(editor, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(editor, EM_SETMODIFY, FALSE, 0);
}
