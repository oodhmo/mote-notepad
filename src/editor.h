#pragma once
#include <windows.h>

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
