#pragma once
#include <windows.h>

namespace Gdiplus {
class Graphics;
}

// 화면 전체에서 공유하는 색 토큰
struct ThemeColors {
    COLORREF windowBg;       // 창/에디터 배경
    COLORREF secondaryBg;    // 팝업 등 보조 배경
    COLORREF text;           // 본문 기본 글자색
    COLORREF secondaryText;  // 보조 글자색
    COLORREF border;         // 구분선
    COLORREF hover;          // 버튼 hover 배경
    COLORREF pressed;        // 버튼 눌림/활성 배경
    COLORREF primaryBg;      // 기본 버튼(확인창의 "저장") 배경
    COLORREF primaryHover;
    COLORREF primaryPressed;
    COLORREF primaryText;
    COLORREF notFound;       // 찾기 실패 표시 (은은한 빨강)
};

const ThemeColors& CurrentTheme();
bool IsDarkTheme();

// Windows 설정의 "앱 모드"(라이트/다크)를 다시 읽는다. 테마가 바뀌었으면 true.
bool UpdateThemeFromSystem();

// 다크 모드용 팝업 메뉴를 쓸 수 있게 준비하고 현재 테마를 메뉴에 반영한다.
// (공개 API가 없어 uxtheme의 서수 함수를 쓴다. 없는 환경에서는 아무것도 하지 않는다.)
void ApplyMenuTheme();

// 제목 표시줄을 현재 테마에 맞춘다 (Windows 11: 본문과 같은 색, Windows 10: 다크 모드 여부만)
void ApplyTitleBarTheme(HWND window);

// 스크롤바/툴팁 같은 시스템 컨트롤을 현재 테마(라이트/다크)로 그리게 한다
void ApplyControlTheme(HWND control);

// UI에 사용할 글꼴 이름 ("Segoe UI Variable Text"가 없으면 "Segoe UI")
const wchar_t* UiFontFace();

// 툴바 아이콘 글꼴 (Windows 11: Segoe Fluent Icons, Windows 10: Segoe MDL2 Assets)
const wchar_t* IconFontFace();

// 96 DPI 기준 픽셀 값을 현재 DPI로 변환한다
inline int Scale(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }

// 포인트 크기(10분의 1 단위)로 UI 글꼴을 만든다. 호출자가 DeleteObject 해야 한다.
HFONT CreateUiFont(int pointTenths, UINT dpi, int weight = FW_NORMAL, const wchar_t* face = nullptr);

// 글자 크기를 96 DPI 기준 픽셀(CSS의 font-size px과 같은 의미)로 지정해 UI 글꼴을 만든다.
// 화면 배율에 맞춰 Scale만 적용하므로 기기와 상관없이 같은 크기로 보인다. 호출자가 DeleteObject 해야 한다.
HFONT CreateUiFontPx(int pixelSize, UINT dpi, int weight = FW_NORMAL, const wchar_t* face = nullptr);

// 한글 문장 위주의 UI에 쓸 글꼴 ("맑은 고딕", 없으면 UiFontFace).
// Segoe UI에는 한글이 없어 Windows가 글자마다 다른 글꼴로 대체하는데, 그 크기 비율이 기기마다 달라
// 같은 px이어도 기기별로 크기가 들쭉날쭉해진다. 한 글꼴로 고정하면 어디서나 같은 비율로 보인다.
const wchar_t* HangulUiFontFace();

// GDI의 RoundRect는 안티앨리어싱이 없어 모서리가 거칠어 GDI+로 그린다
void FillRoundRect(Gdiplus::Graphics& graphics, const RECT& rect, COLORREF color, int radius);

inline void FillSolid(HDC dc, const RECT& rect, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

// 메모리 비트맵에 그린 뒤 한 번에 복사해 깜빡임을 막는다
struct BufferedPaint {
    PAINTSTRUCT ps{};
    HDC target = nullptr;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    HWND hwnd;
    RECT client{};

    explicit BufferedPaint(HWND window) : hwnd(window)
    {
        target = BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &client);
        dc = CreateCompatibleDC(target);
        bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
        oldBitmap = SelectObject(dc, bitmap);
    }
    ~BufferedPaint()
    {
        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(hwnd, &ps);
    }
    BufferedPaint(const BufferedPaint&) = delete;
    BufferedPaint& operator=(const BufferedPaint&) = delete;
};
