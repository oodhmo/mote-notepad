#pragma once
#include <windows.h>

// 화면 전체에서 공유하는 색 토큰
struct ThemeColors {
    COLORREF windowBg;       // 창/에디터 배경
    COLORREF secondaryBg;    // 팝업 등 보조 배경
    COLORREF text;           // 본문 기본 글자색
    COLORREF secondaryText;  // 보조 글자색
    COLORREF border;         // 구분선
    COLORREF hover;          // 버튼 hover 배경
    COLORREF pressed;        // 버튼 눌림/활성 배경
};

const ThemeColors& CurrentTheme();

// UI에 사용할 글꼴 이름 ("Segoe UI Variable Text"가 없으면 "Segoe UI")
const wchar_t* UiFontFace();

// 96 DPI 기준 픽셀 값을 현재 DPI로 변환한다
inline int Scale(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }

// 포인트 크기(10분의 1 단위)로 UI 글꼴을 만든다. 호출자가 DeleteObject 해야 한다.
HFONT CreateUiFont(int pointTenths, UINT dpi, int weight = FW_NORMAL, const wchar_t* face = nullptr);
