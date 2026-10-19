#pragma once
#include <windows.h>

// 도움말 > 단축키 및 기능 (F1). 확인창과 같은 톤의 작은 모달 창이다.
void ShowShortcutsHelp(HWND owner);

// 도움말 > mote 정보 (이름, 버전, GitHub 주소)
void ShowAbout(HWND owner);
