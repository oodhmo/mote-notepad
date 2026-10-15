#pragma once
#include <windows.h>

// 찾기 바가 나타나거나 사라지거나 높이가 바뀌어 owner가 레이아웃을 다시 계산해야 할 때 보내는 메시지
constexpr UINT WM_MOTE_RELAYOUT = WM_APP + 2;

// 상단 바 아래에 붙는 인라인 찾기/바꾸기 바 (Windows 11 메모장 방식).
// 1행: 찾기 입력란, 이전/다음, 닫기 / 2행(바꾸기 모드): 바꾸기 입력란, Replace, Replace All
HWND CreateFindBar(HWND owner, HINSTANCE instance, HWND editor);

// 바를 연다. 에디터에 한 줄짜리 선택이 있으면 찾기 칸에 미리 채운다.
void ShowFindBar(bool replaceMode);
void HideFindBar();

bool IsFindBarVisible();

// 입력 포커스가 찾기 바의 입력란에 있는지
bool FindBarHasFocus();

// 현재 모드에서의 바 높이
int FindBarHeight(UINT dpi);

void SetFindBarDpi(UINT dpi);

// 본문 내용이나 선택이 바뀌었을 때 호출한다. 바가 열려 있으면 잠시 뒤 일치 항목 수를 다시 센다.
void NotifyFindBarDocumentChanged();

// F3 / Shift+F3: 찾기 칸의 내용으로 다음/이전 항목을 찾는다. 찾기 칸이 비어 있으면 바를 연다.
void FindNextFromBar(bool forward);
