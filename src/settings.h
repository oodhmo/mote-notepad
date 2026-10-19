#pragma once
#include <windows.h>
#include <array>
#include <string>
#include <vector>

// 사용자 설정은 모두 HKCU\Software\mote 아래에 저장한다 (포터블 실행: 설치 과정 없음)

bool LoadToolbarCollapsed();
void SaveToolbarCollapsed(bool collapsed);

// 처음 실행할 때 연결 프로그램 등록을 이미 물어봤는지 (답과 관계없이 한 번만 묻는다)
bool LoadOpenWithAsked();
void SaveOpenWithAsked();

// 마지막으로 닫은 창의 위치/크기/최대화 상태. 저장된 값이 없거나 올바르지 않으면 false.
bool LoadWindowPlacement(WINDOWPLACEMENT& placement);
void SaveWindowPlacement(const WINDOWPLACEMENT& placement);

// Ctrl+F1/F2/F3에 지정된 색 (색상 팝업의 팔레트 번호 0~7). 저장된 값이 없으면 빨강/파랑/초록.
constexpr int kColorShortcutCount = 3;
using ColorShortcuts = std::array<int, kColorShortcutCount>;
ColorShortcuts LoadColorShortcuts(int paletteCount);
void SaveColorShortcuts(const ColorShortcuts& shortcuts);

// 최근 파일 목록 (최신 순, 최대 kMaxRecentFiles개)
constexpr size_t kMaxRecentFiles = 8;

std::vector<std::wstring> LoadRecentFiles();

// path를 목록 맨 앞으로 옮긴다 (대소문자를 무시하고 같은 경로는 하나만 남긴다)
void AddRecentFile(const std::wstring& path);
void RemoveRecentFile(const std::wstring& path);
void ClearRecentFiles();
