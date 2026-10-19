#pragma once
#include <windows.h>
#include <string>

enum class FileFormat { Rtf, Text };

// TXT 파일의 원래 인코딩과 줄바꿈. 열 때 알아내 두었다가 저장할 때 그대로 쓴다.
enum class TextEncoding { Utf8, Utf8Bom, Utf16Le, Utf16Be, Ansi };  // Ansi: 시스템 언어의 옛 코드 페이지 (한국어는 CP949)
enum class LineEnding { CrLf, Lf, Cr };

struct TextFileInfo {
    TextEncoding encoding = TextEncoding::Utf8;  // 새 문서는 BOM 없는 UTF-8 + CRLF
    LineEnding lineEnding = LineEnding::CrLf;
};

enum class SaveResult {
    Ok,
    Failed,        // error에 사용자용 메시지
    NeedsUnicode,  // 원래 인코딩(ANSI)으로 표현할 수 없는 글자가 있어 저장하지 않았다
};

// 사용자에게 보여 줄 인코딩 이름 (예: "ANSI (CP949)")
std::wstring EncodingName(TextEncoding encoding);

// 경로의 확장자로 저장 형식을 정한다 (.txt면 Text, 그 외는 Rtf)
FileFormat FormatFromPath(const std::wstring& path);

// 파일 열기 대화상자. startFolder가 있으면 그 폴더에서 시작한다. 취소하면 false.
bool ShowOpenDialog(HWND owner, const std::wstring& startFolder, std::wstring& path);

// 다른 이름으로 저장 대화상자. currentPath가 있으면 그 위치/이름에서 시작한다. 취소하면 false.
bool ShowSaveDialog(HWND owner, const std::wstring& currentPath, FileFormat currentFormat, std::wstring& path);

// 파일을 에디터로 읽는다. 실패하면 error에 사용자용 메시지를 채우고 에디터는 건드리지 않는다.
// TXT로 열면 textInfo에 원래 인코딩/줄바꿈을 채운다 (RTF면 기본값).
bool LoadFileIntoEditor(HWND editor, const std::wstring& path, FileFormat& format, TextFileInfo& textInfo,
                        std::wstring& error);

// 에디터 내용을 파일로 저장한다. 임시 파일에 먼저 쓰고 교체하므로 실패해도 원본은 보존된다.
// TXT는 textInfo의 인코딩/줄바꿈으로 저장한다.
SaveResult SaveEditorToFile(HWND editor, const std::wstring& path, FileFormat format, const TextFileInfo& textInfo,
                            std::wstring& error);
