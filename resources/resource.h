#pragma once

// 리소스
#define IDR_MAINMENU        101
#define IDR_ACCEL           102

// File 메뉴
#define IDM_FILE_NEW        1001
#define IDM_FILE_OPEN       1002
#define IDM_FILE_SAVE       1003
#define IDM_FILE_SAVEAS     1004
#define IDM_FILE_EXIT       1005
#define IDM_FILE_NEWWINDOW  1006

// File > Open Recent (최근 파일 8개: FIRST ~ FIRST+7)
#define IDM_FILE_RECENT_FIRST   1010
#define IDM_FILE_RECENT_CLEAR   1020
#define IDM_FILE_RECENT_EMPTY   1021

// Edit 메뉴
#define IDM_EDIT_UNDO       1101
#define IDM_EDIT_REDO       1102
#define IDM_EDIT_CUT        1103
#define IDM_EDIT_COPY       1104
#define IDM_EDIT_PASTE      1105
#define IDM_EDIT_SELECTALL  1106
#define IDM_EDIT_FIND       1107
#define IDM_EDIT_FINDNEXT   1108
#define IDM_EDIT_FINDPREV   1109
#define IDM_EDIT_REPLACE    1110

// 서식 (상단 바 버튼, Ctrl+B/I/U)
#define IDM_FORMAT_BOLD         1201
#define IDM_FORMAT_ITALIC       1202
#define IDM_FORMAT_UNDERLINE    1203
#define IDM_FORMAT_CUSTOMCOLOR  1204

// 상단 바 접기/펼치기 (Ctrl+\)
#define IDM_VIEW_TOOLBAR        1301

// 상단 바의 메뉴 버튼을 키보드로 여는 명령 (Alt+F / Alt+E)
#define IDM_OPEN_FILE_MENU  1901
#define IDM_OPEN_EDIT_MENU  1902
