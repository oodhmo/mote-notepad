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
    {L"Help", L"도움말"},
    {L"Bold (Ctrl+B)", L"굵게 (Ctrl+B)"},
    {L"Italic (Ctrl+I)", L"기울임꼴 (Ctrl+I)"},
    {L"Underline (Ctrl+U)", L"밑줄 (Ctrl+U)"},
    {L"Text color", L"글자색"},
    {L"Hide toolbar (Ctrl+\\)", L"툴바 숨기기 (Ctrl+\\)"},
    {L"Show toolbar (Ctrl+\\)", L"툴바 표시 (Ctrl+\\)"},
    {L"Custom color…", L"사용자 지정 색…"},
    {L"Assign to Ctrl+F%d", L"Ctrl+F%d에 지정"},
    {L"Recent colors", L"최근 사용한 색"},

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

    {L"Shortcuts and features", L"단축키 및 기능"},
    {L"mote is a lightweight notepad that lets you color selected text. Rich text (.rtf) keeps colors, bold, italic and underline; plain text (.txt) is saved in its original encoding.",
     L"mote는 선택한 글자에 색을 입힐 수 있는 가벼운 메모장입니다. 서식 있는 문서(.rtf)는 글자색과 굵게·기울임꼴·밑줄을 그대로 저장하고, 텍스트 문서(.txt)는 원래 인코딩으로 저장합니다."},
    {L"File", L"파일"},
    {L"Edit", L"편집"},
    {L"Format and color", L"서식과 글자색"},
    {L"Find and replace", L"찾기와 바꾸기"},
    {L"View and help", L"보기와 도움말"},
    {L"New", L"새로 만들기"},
    {L"New window", L"새 창"},
    {L"Open", L"열기"},
    {L"Save as", L"다른 이름으로 저장"},
    {L"You can also open a file by dragging it onto the window.", L"파일을 창에 끌어다 놓아도 열립니다."},
    {L"Undo", L"실행 취소"},
    {L"Redo", L"다시 실행"},
    {L"Cut / copy / paste", L"잘라내기 / 복사 / 붙여넣기"},
    {L"Select all", L"모두 선택"},
    {L"Bold", L"굵게"},
    {L"Italic", L"기울임꼴"},
    {L"Underline", L"밑줄"},
    {L"Apply this color", L"이 색 적용"},
    {L"To change these colors, right-click a color in the text color menu.", L"단축키 색은 글자색 메뉴에서 색을 우클릭해 바꿀 수 있습니다."},
    {L"Find", L"찾기"},
    {L"Replace", L"바꾸기"},
    {L"Find next", L"다음 찾기"},
    {L"Find previous", L"이전 찾기"},
    {L"Close the find bar", L"찾기 바 닫기"},
    {L"Hide / show toolbar", L"툴바 숨기기 / 표시"},
    {L"Version %s", L"버전 %s"},
    {L"A lightweight notepad that lets you color selected text.", L"선택한 글자에 색을 입힐 수 있는 가벼운 메모장입니다."},
    {L"Close", L"닫기"},

    {L"Add mote to \"Open with\"...", L"연결 프로그램에 mote 등록..."},
    {L"Remove mote from \"Open with\"", L"연결 프로그램에서 mote 제거"},
    {L"Add mote to \"Open with\"?", L"연결 프로그램에 mote를 등록할까요?"},
    {L"mote will appear in the \"Open with\" list for .txt and .rtf files and in Windows default apps settings. If you move mote.exe, the path is updated the next time you run it.",
     L".txt, .rtf 파일의 '연결 프로그램' 목록과 Windows 기본 앱 설정에 mote가 표시됩니다. mote.exe를 다른 폴더로 옮기면 다음 실행 때 경로가 자동으로 고쳐집니다."},
    {L"Add", L"등록"},
    {L"mote was added to \"Open with\"", L"연결 프로그램에 mote를 등록했습니다"},
    {L"To open .txt files with mote every time, choose mote in default apps settings, or right-click a file and choose Open with > mote > Always.",
     L".txt 파일을 항상 mote로 열려면 기본 앱 설정에서 mote를 고르거나, 파일을 우클릭해 연결 프로그램 > mote > 항상을 선택하세요."},
    {L"Open settings", L"설정 열기"},
    {L"Remove mote from \"Open with\"?", L"연결 프로그램에서 mote를 제거할까요?"},
    {L"mote will no longer appear in the \"Open with\" list or in default apps settings. Your files are not changed.",
     L"'연결 프로그램' 목록과 기본 앱 설정에서 mote가 사라집니다. 파일은 바뀌지 않습니다."},
    {L"Remove", L"제거"},
    {L"Can't add mote to \"Open with\".", L"연결 프로그램에 등록하지 못했습니다."},
    {L"mote Text Document", L"mote 텍스트 문서"},
    {L"mote Rich Text Document", L"mote 서식 있는 문서"},
    {L"Add mote to \"Open with\"", L"연결 프로그램에 mote 등록"},
    {L"Show mote in the \"Open with\" list when you right-click .txt and .rtf files? You can add or remove it later from the Help menu.",
     L".txt, .rtf 파일을 우클릭했을 때 '연결 프로그램' 목록에 mote가 나타나게 할까요? 나중에 도움말 메뉴에서 등록하거나 제거할 수 있습니다."},
    {L"Later", L"나중에"},
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
