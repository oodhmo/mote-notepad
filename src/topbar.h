#pragma once
#include <windows.h>

// 창 상단의 얇은 한 줄 바. 왼쪽에 File/Edit 메뉴 버튼을 둔다.
// menus: 하위 메뉴들을 가진 메뉴 리소스. 메뉴 명령은 owner로 전달된다.
HWND CreateTopBar(HWND owner, HINSTANCE instance, HMENU menus);

// 현재 DPI에서 상단 바의 높이
int TopBarHeight(UINT dpi);

// DPI가 바뀌면 글꼴과 배치를 다시 계산한다
void SetTopBarDpi(HWND bar, UINT dpi);

// index번째 메뉴 버튼의 팝업을 연다 (Alt+F, Alt+E)
void OpenTopBarMenu(HWND bar, int index);
