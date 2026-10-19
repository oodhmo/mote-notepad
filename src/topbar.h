#pragma once
#include <windows.h>
#include "editor.h"

// 색상 팝업에서 색을 골랐을 때 owner로 보내는 메시지 (lParam = COLORREF)
constexpr UINT WM_MOTE_SETCOLOR = WM_APP + 1;

// 창 상단의 얇은 한 줄 바.
// 왼쪽: File/Edit 메뉴 버튼, 오른쪽: B / I / U / 글자색 버튼.
// menus: 하위 메뉴들을 가진 메뉴 리소스. 메뉴·버튼 명령은 owner로 WM_COMMAND로 전달된다.
HWND CreateTopBar(HWND owner, HINSTANCE instance, HMENU menus);

// 현재 DPI에서 상단 바의 높이
int TopBarHeight(UINT dpi);

// DPI가 바뀌면 글꼴과 배치를 다시 계산한다
void SetTopBarDpi(HWND bar, UINT dpi);

// index번째 메뉴 버튼의 팝업을 연다 (Alt+F, Alt+E)
void OpenTopBarMenu(HWND bar, int index);

// 선택 영역 서식을 버튼 상태(B/I/U 활성, 색 표시)에 반영한다
void SetTopBarFormatState(HWND bar, const FormatState& state);

// Ctrl+F1~F3(slot 0~2)에 지정된 색. 색상 팝업에서 색을 우클릭해 바꿀 수 있다.
COLORREF ShortcutColor(int slot);

// 색상 팝업이 열려 있는지 (메인 창이 활성 모양을 유지하는 데 사용)
bool IsColorPopupOpen();

// 메뉴/색 팝업이 열려 있거나 버튼을 누르고 있는 중인지 (접힌 바를 숨기면 안 되는 상태)
bool IsTopBarBusy();

// 바가 접힌 상태인지 알려 준다 (오른쪽 끝 화살표의 방향과 툴팁이 바뀐다)
void SetTopBarCollapsed(HWND bar, bool collapsed);

// 바가 접혔을 때 창 오른쪽 위에 나타나는 펼치기 화살표 (메인 창의 자식, 클릭하면 IDM_VIEW_TOOLBAR)
HWND ExpandButtonWindow();
int ExpandButtonSize(UINT dpi);
int ExpandButtonMargin(UINT dpi);
int ExpandButtonTop(UINT dpi);
