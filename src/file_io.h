#pragma once
#include <windows.h>
#include <string>

enum class FileFormat { Rtf, Text };

// 경로의 확장자로 저장 형식을 정한다 (.txt면 Text, 그 외는 Rtf)
FileFormat FormatFromPath(const std::wstring& path);

// 파일 열기 대화상자. 취소하면 false.
bool ShowOpenDialog(HWND owner, std::wstring& path);

// 다른 이름으로 저장 대화상자. currentPath가 있으면 그 위치/이름에서 시작한다. 취소하면 false.
bool ShowSaveDialog(HWND owner, const std::wstring& currentPath, FileFormat currentFormat, std::wstring& path);

// 파일을 에디터로 읽는다. 실패하면 error에 사용자용 메시지를 채우고 에디터는 건드리지 않는다.
bool LoadFileIntoEditor(HWND editor, const std::wstring& path, FileFormat& format, std::wstring& error);

// 에디터 내용을 파일로 저장한다. 임시 파일에 먼저 쓰고 교체하므로 실패해도 원본은 보존된다.
bool SaveEditorToFile(HWND editor, const std::wstring& path, FileFormat format, std::wstring& error);
