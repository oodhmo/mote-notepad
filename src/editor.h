#pragma once
#include <windows.h>
#include <string>

// TOM의 자동 색(tomAutoColor)과 같은 값. ReplaceTextColor의 인자로 사용한다.
constexpr long kAutoColor = -9999997;

// RichEdit 컨트롤을 만들고 기본 설정을 적용한다.
// contextMenu: 우클릭 시 보여줄 팝업 메뉴 (명령은 parent로 전달된다)
HWND CreateEditor(HWND parent, HINSTANCE instance, HMENU contextMenu);

// DPI에 맞춰 본문 여백을 다시 계산한다
void SetEditorDpi(HWND editor, UINT dpi);

// 빈 새 문서로 초기화한다 (실행 취소 기록과 수정 상태도 초기화)
void ResetEditorDocument(HWND editor);

// 일반 텍스트를 불러온 직후: 문서 전체에 기본 글꼴/색/줄간격을 적용한다
void ApplyPlainTextFormatting(HWND editor);

// RTF를 불러온 직후: 자동 색을 테마 기본색으로 바꾸고 줄간격을 맞춘다
void NormalizeLoadedRichText(HWND editor);

// 문서 전체에서 글자색 from을 to로 바꾼다 (kAutoColor 사용 가능).
// 실행 취소 기록과 수정 상태에는 영향을 주지 않는다.
void ReplaceTextColor(HWND editor, long from, long to);

// 실행 취소 기록을 비우고 "수정되지 않음" 상태로 만든다
void MarkEditorClean(HWND editor);

// 현재 테마의 기본 글자색
COLORREF DefaultTextColor();

// 선택 영역(선택이 없으면 캐럿 위치의 입력 서식) 상태. 상단 바 표시용.
struct FormatState {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool colorMixed = false;  // 선택 영역에 여러 색이 섞여 있음
    COLORREF color = 0;
};

FormatState GetSelectionFormat(HWND editor);

// 선택 영역 전체가 해당 효과를 가지고 있으면 끄고, 아니면 켠다 (CFE_BOLD / CFE_ITALIC / CFE_UNDERLINE)
void ToggleSelectionEffect(HWND editor, DWORD effect);

// 선택 영역의 글자색을 바꾼다. 선택이 없으면 이후 입력할 글자의 색이 바뀐다.
void SetSelectionColor(HWND editor, COLORREF color);

// ---- 찾기 / 바꾸기 (대소문자를 무시하는 단순 부분 문자열 검색) ----

// 다음(forward) 또는 이전 일치 항목을 찾아 선택한다. 문서 끝(처음)에 닿으면 반대쪽부터 다시 찾는다.
// fromSelectionStart: 앞으로 찾을 때 현재 선택의 시작부터 찾는다 (입력하는 동안 같은 위치를 유지하는 검색용)
bool FindInEditor(HWND editor, const std::wstring& text, bool forward, bool fromSelectionStart = false);

// 현재 선택이 찾는 문자열이면 바꾼 뒤 다음 항목을 찾는다. 다음 항목이 있으면 true.
bool ReplaceCurrent(HWND editor, const std::wstring& find, const std::wstring& replacement);

// 문서 전체를 바꾸고 바꾼 개수를 돌려준다. 한 번의 실행 취소로 모두 되돌릴 수 있다.
int ReplaceAllInEditor(HWND editor, const std::wstring& find, const std::wstring& replacement);

// 문서 전체의 일치 항목 수. current는 현재 선택과 같은 항목의 순번(1부터, 없으면 0).
// 큰 문서에서 느려지지 않도록 limit개에서 세기를 멈춘다 (capped = true).
struct MatchCount {
    int current = 0;
    int total = 0;
    bool capped = false;
};

MatchCount CountMatches(HWND editor, const std::wstring& text, int limit);

// 선택된 텍스트 (줄바꿈이 없고 maxLength 이하일 때만, 아니면 빈 문자열)
std::wstring GetSingleLineSelection(HWND editor, size_t maxLength);
