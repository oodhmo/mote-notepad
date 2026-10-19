#include "strings.h"

#include <windows.h>

namespace {

struct Entry {
    const wchar_t* en;
    const wchar_t* ko;
};

// StrId와 같은 순서여야 한다
constexpr Entry kStrings[] = {
    {L"File", L"파일"},
    {L"Edit", L"편집"},
    {L"Bold (Ctrl+B)", L"굵게 (Ctrl+B)"},
    {L"Italic (Ctrl+I)", L"기울임꼴 (Ctrl+I)"},
    {L"Underline (Ctrl+U)", L"밑줄 (Ctrl+U)"},
    {L"Text color", L"글자색"},
    {L"Hide toolbar (Ctrl+\\)", L"툴바 숨기기 (Ctrl+\\)"},
    {L"Show toolbar (Ctrl+\\)", L"툴바 표시 (Ctrl+\\)"},
    {L"Custom color…", L"사용자 지정 색…"},

    {L"(Empty)", L"(없음)"},
    {L"&Clear Recent List", L"목록 지우기(&C)"},
    {L"Hide &Toolbar\tCtrl+\\", L"툴바 숨기기(&T)\tCtrl+\\"},
    {L"Show &Toolbar\tCtrl+\\", L"툴바 표시(&T)\tCtrl+\\"},

    {L"Find", L"찾기"},
    {L"Replace", L"바꾸기"},
    {L"Replace", L"바꾸기"},
    {L"Replace All", L"모두 바꾸기"},
    {L"Find previous (Shift+F3)", L"이전 찾기 (Shift+F3)"},
    {L"Find next (F3)", L"다음 찾기 (F3)"},
    {L"Close (Esc)", L"닫기 (Esc)"},
    {L"Toggle replace (Ctrl+H)", L"바꾸기 표시/숨기기 (Ctrl+H)"},
    {L"Replace (Enter)", L"바꾸기 (Enter)"},
    {L"Replace all (Ctrl+Alt+Enter)", L"모두 바꾸기 (Ctrl+Alt+Enter)"},
    {L"%d found", L"%d개"},
    {L"%d replaced", L"%d개 바꿈"},

    {L"Do you want to save changes?", L"변경 내용을 저장하시겠습니까?"},
    {L"Save", L"저장"},
    {L"Don't Save", L"저장 안 함"},
    {L"Cancel", L"취소"},
    {L"Save as UTF-8?", L"UTF-8로 저장할까요?"},
    {L"This document contains characters that can't be saved in its original encoding, %s. Saving as UTF-8 keeps every character.",
     L"이 문서에는 원래 인코딩인 %s 형식으로 저장할 수 없는 글자가 있습니다. UTF-8로 저장하면 모든 글자가 그대로 유지됩니다."},
    {L"Save as UTF-8", L"UTF-8로 저장"},

    {L"Untitled", L"제목 없음"},
    {L"Documents (*.rtf, *.txt)", L"문서 (*.rtf, *.txt)"},
    {L"Rich Text Format (*.rtf)", L"서식 있는 텍스트 (*.rtf)"},
    {L"Text Documents (*.txt)", L"텍스트 문서 (*.txt)"},
    {L"All Files (*.*)", L"모든 파일 (*.*)"},

    {L"Can't open the file.", L"파일을 열 수 없습니다."},
    {L"Can't save the file.", L"파일을 저장할 수 없습니다."},
    {L"Can't open a new window.", L"새 창을 열 수 없습니다."},
    {L"Can't find the path of the executable.", L"실행 파일 경로를 확인할 수 없습니다."},
    {L"Unknown error", L"알 수 없는 오류"},
    {L"The file is too large (max 512 MB).", L"파일이 너무 큽니다 (최대 512MB)."},
    {L"Can't read the file. It may be damaged.", L"파일 내용을 읽을 수 없습니다. 손상된 파일일 수 있습니다."},
    {L"Can't convert the document.", L"문서 내용을 변환하지 못했습니다."},
    {L"Can't load Msftedit.dll.", L"Msftedit.dll을 불러올 수 없습니다."},
    {L"Can't create the window.", L"창을 만들 수 없습니다."},
};
static_assert(ARRAYSIZE(kStrings) == static_cast<size_t>(StrId::Count), "kStrings와 StrId의 개수가 다르다");

}  // namespace

bool IsKoreanUi()
{
    // 지역 형식이 아니라 "Windows 표시 언어"를 본다 (시스템 대화상자의 언어와 맞추기 위해)
    static const bool korean = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_KOREAN;
    return korean;
}

const wchar_t* Str(StrId id)
{
    const Entry& entry = kStrings[static_cast<size_t>(id)];
    return IsKoreanUi() ? entry.ko : entry.en;
}
