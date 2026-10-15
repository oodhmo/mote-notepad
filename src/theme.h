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
};

const ThemeColors& CurrentTheme();

// UI에 사용할 글꼴 이름 ("Segoe UI Variable Text"가 없으면 "Segoe UI")
const wchar_t* UiFontFace();

// 툴바 아이콘 글꼴 (Windows 11: Segoe Fluent Icons, Windows 10: Segoe MDL2 Assets)
const wchar_t* IconFontFace();

// 96 DPI 기준 픽셀 값을 현재 DPI로 변환한다
inline int Scale(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }

// 포인트 크기(10분의 1 단위)로 UI 글꼴을 만든다. 호출자가 DeleteObject 해야 한다.
HFONT CreateUiFont(int pointTenths, UINT dpi, int weight = FW_NORMAL, const wchar_t* face = nullptr);

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
