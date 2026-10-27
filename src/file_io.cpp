#include "file_io.h"
#include "editor.h"
#include "strings.h"

#include <richedit.h>
#include <shobjidl.h>
#include <algorithm>
#include <cstring>

namespace {

constexpr ULONGLONG kMaxFileSize = 512ull * 1024 * 1024;  // 메모장 용도로 충분한 상한

// 큰 파일을 읽고 쓰는 동안 멈춘 것처럼 보이지 않도록 대기 커서를 띄운다 (범위를 벗어나면 원래 커서로)
struct WaitCursor {
    HCURSOR previous = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    ~WaitCursor() { SetCursor(previous); }
};

std::wstring SystemErrorText(DWORD code)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = (length && buffer) ? std::wstring(buffer, length) : std::wstring(Str(StrId::ErrorUnknown)) + L" (" + std::to_wstring(code) + L")";
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ')) text.pop_back();
    return text;
}

std::wstring Extension(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return {};
    std::wstring ext = path.substr(dot);
    CharLowerBuffW(ext.data(), static_cast<DWORD>(ext.size()));
    return ext;
}

bool ReadWholeFile(const std::wstring& path, std::string& bytes, std::wstring& error)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = SystemErrorText(GetLastError());
        return false;
    }

    bool ok = false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size)) {
        error = SystemErrorText(GetLastError());
    } else if (static_cast<ULONGLONG>(size.QuadPart) > kMaxFileSize) {
        error = Str(StrId::ErrorFileTooLarge);
    } else {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        size_t offset = 0;
        ok = true;
        while (offset < bytes.size()) {
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 64u * 1024 * 1024));
            DWORD read = 0;
            if (!ReadFile(file, bytes.data() + offset, chunk, &read, nullptr)) {
                error = SystemErrorText(GetLastError());
                ok = false;
                break;
            }
            if (read == 0) {  // 읽는 도중 파일이 줄어든 경우
                bytes.resize(offset);
                break;
            }
            offset += read;
        }
    }
    CloseHandle(file);
    return ok;
}

// 같은 폴더의 임시 파일에 먼저 쓰고 원본과 교체한다.
// 쓰기 도중 실패하거나 디스크가 가득 차도 기존 파일은 손상되지 않는다.
bool WriteWholeFile(const std::wstring& path, const std::string& bytes, std::wstring& error)
{
    const std::wstring temp = path + L".mote-tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = SystemErrorText(GetLastError());
        return false;
    }

    bool ok = true;
    size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 64u * 1024 * 1024));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr)) {
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    DWORD code = ok ? 0 : GetLastError();
    CloseHandle(file);

    if (ok) {
        // ReplaceFileW는 원본의 속성/권한을 유지한다. 새 파일이거나 실패하면 이동으로 대체한다.
        ok = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES &&
             ReplaceFileW(path.c_str(), temp.c_str(), nullptr,
                          REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS, nullptr, nullptr);
        if (!ok) ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!ok) code = GetLastError();
    }
    if (!ok) {
        DeleteFileW(temp.c_str());
        error = SystemErrorText(code);
    }
    return ok;
}

// UTF-8이 아닌 옛 텍스트 파일에 쓸 코드 페이지 (한국어 Windows는 CP949).
// "Unicode UTF-8 지원(베타)"를 켜면 GetACP()가 UTF-8(65001)을 돌려주므로, 그때는 시스템 언어의 원래 코드 페이지를 쓴다.
UINT LegacyCodePage()
{
    static const UINT codePage = [] {
        const UINT acp = GetACP();
        if (acp != CP_UTF8) return acp;
        UINT localeCodePage = 0;
        GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                        reinterpret_cast<LPWSTR>(&localeCodePage), sizeof(localeCodePage) / sizeof(wchar_t));
        return (localeCodePage != 0 && localeCodePage != CP_UTF8) ? localeCodePage : 1252u;
    }();
    return codePage;
}

std::wstring DecodeText(const std::string& bytes, TextEncoding& encoding)
{
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    const size_t size = bytes.size();

    // UTF-16 BOM
    if (size >= 2 && ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF))) {
        const bool bigEndian = data[0] == 0xFE;
        encoding = bigEndian ? TextEncoding::Utf16Be : TextEncoding::Utf16Le;
        std::wstring text((size - 2) / 2, L'\0');
        for (size_t i = 0; i < text.size(); ++i) {
            const unsigned char a = data[2 + i * 2], b = data[3 + i * 2];
            text[i] = static_cast<wchar_t>(bigEndian ? (a << 8) | b : (b << 8) | a);
        }
        return text;
    }

    // UTF-8 BOM은 건너뛴다
    const bool bom = size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF;
    const size_t start = bom ? 3 : 0;
    encoding = bom ? TextEncoding::Utf8Bom : TextEncoding::Utf8;
    const char* src = bytes.data() + start;
    const int srcLen = static_cast<int>(size - start);
    if (srcLen == 0) return {};

    // UTF-8을 기본으로 시도하고, 잘못된 바이트가 있으면 시스템 코드 페이지(예: CP949)로 읽는다
    UINT codePage = CP_UTF8;
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, srcLen, nullptr, 0);
    if (length == 0) {
        codePage = LegacyCodePage();
        encoding = TextEncoding::Ansi;
        length = MultiByteToWideChar(codePage, 0, src, srcLen, nullptr, 0);
    }
    std::wstring text(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(codePage, codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, src, srcLen, text.data(), length);
    return text;
}

// 첫 줄바꿈의 종류. 줄바꿈이 섞여 있으면 저장할 때 이 종류로 통일된다 (메모장과 같은 동작).
LineEnding DetectLineEnding(const std::wstring& text)
{
    const size_t pos = text.find_first_of(L"\r\n");
    if (pos == std::wstring::npos) return LineEnding::CrLf;
    if (text[pos] == L'\n') return LineEnding::Lf;
    return (pos + 1 < text.size() && text[pos + 1] == L'\n') ? LineEnding::CrLf : LineEnding::Cr;
}

// RichEdit가 내보낸 텍스트(문단 끝 = CRLF)의 줄바꿈을 바꾼다
std::wstring ConvertLineEndings(const wchar_t* text, size_t count, LineEnding lineEnding)
{
    std::wstring out(text, count);
    if (lineEnding == LineEnding::CrLf) return out;
    const wchar_t replacement = lineEnding == LineEnding::Lf ? L'\n' : L'\r';
    size_t write = 0;
    for (size_t read = 0; read < out.size(); ++read) {
        if (out[read] == L'\r' && read + 1 < out.size() && out[read + 1] == L'\n') {
            out[write++] = replacement;
            ++read;
        } else {
            out[write++] = out[read];
        }
    }
    out.resize(write);
    return out;
}

// 인코딩에 맞춰 바이트로 바꾼다. ANSI로 표현할 수 없는 글자가 있으면 false (다른 인코딩은 항상 성공).
bool EncodeText(const std::wstring& text, TextEncoding encoding, std::string& out)
{
    out.clear();
    const int count = static_cast<int>(text.size());
    switch (encoding) {
    case TextEncoding::Utf16Le:
    case TextEncoding::Utf16Be: {
        const bool bigEndian = encoding == TextEncoding::Utf16Be;
        out.reserve(2 + text.size() * 2);
        out += bigEndian ? "\xFE\xFF" : "\xFF\xFE";
        for (const wchar_t ch : text) {
            const char low = static_cast<char>(ch & 0xFF);
            const char high = static_cast<char>(ch >> 8);
            out += bigEndian ? high : low;
            out += bigEndian ? low : high;
        }
        return true;
    }
    case TextEncoding::Ansi: {
        if (count == 0) return true;
        // 비슷한 글자로 몰래 바꾸지 않도록(WC_NO_BEST_FIT_CHARS) 하고, 대체 문자가 쓰였으면 실패로 본다
        BOOL usedDefault = FALSE;
        const int length = WideCharToMultiByte(LegacyCodePage(), WC_NO_BEST_FIT_CHARS, text.data(), count, nullptr, 0, nullptr, &usedDefault);
        if (length == 0 || usedDefault) return false;
        out.resize(static_cast<size_t>(length));
        WideCharToMultiByte(LegacyCodePage(), WC_NO_BEST_FIT_CHARS, text.data(), count, out.data(), length, nullptr, nullptr);
        return true;
    }
    case TextEncoding::Utf8Bom:
    case TextEncoding::Utf8:
    default: {
        if (encoding == TextEncoding::Utf8Bom) out = "\xEF\xBB\xBF";
        if (count == 0) return true;
        const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), count, nullptr, 0, nullptr, nullptr);
        const size_t prefix = out.size();
        out.resize(prefix + static_cast<size_t>(length));
        WideCharToMultiByte(CP_UTF8, 0, text.data(), count, out.data() + prefix, length, nullptr, nullptr);
        return true;
    }
    }
}

struct MemoryReader {
    const char* data;
    size_t size;
    size_t pos;
};

// EM_STREAMIN 콜백: 메모리 버퍼에서 RichEdit로 데이터를 공급한다
DWORD CALLBACK ReadFromMemory(DWORD_PTR cookie, LPBYTE buffer, LONG requested, LONG* actual)
{
    auto* reader = reinterpret_cast<MemoryReader*>(cookie);
    const size_t count = std::min<size_t>(static_cast<size_t>(requested), reader->size - reader->pos);
    memcpy(buffer, reader->data + reader->pos, count);
    reader->pos += count;
    *actual = static_cast<LONG>(count);
    return 0;
}

// EM_STREAMOUT 콜백: RichEdit 출력을 문자열 버퍼에 모은다
DWORD CALLBACK WriteToString(DWORD_PTR cookie, LPBYTE buffer, LONG count, LONG* actual)
{
    reinterpret_cast<std::string*>(cookie)->append(reinterpret_cast<const char*>(buffer), static_cast<size_t>(count));
    *actual = count;
    return 0;
}

bool StreamIn(HWND editor, const void* data, size_t size, UINT format)
{
    MemoryReader reader = {static_cast<const char*>(data), size, 0};
    EDITSTREAM stream = {reinterpret_cast<DWORD_PTR>(&reader), 0, ReadFromMemory};
    SendMessageW(editor, EM_STREAMIN, format, reinterpret_cast<LPARAM>(&stream));
    return stream.dwError == 0;
}

bool StreamOut(HWND editor, UINT format, std::string& out)
{
    EDITSTREAM stream = {reinterpret_cast<DWORD_PTR>(&out), 0, WriteToString};
    SendMessageW(editor, EM_STREAMOUT, format, reinterpret_cast<LPARAM>(&stream));
    return stream.dwError == 0;
}

// 예전 버전이 본문 글꼴로 저장한 "Segoe UI Variable ..."을 RTF 글꼴 표에서 본문 글꼴로 바꾼다 (kEditorFontFace 참고).
// 불러온 뒤 TOM으로 글꼴 이름을 바꾸면 이름은 바뀌어도 확대 시 한글이 깨지는 것은 그대로라서, 읽기 전에 바꾼다.
// 본문에 같은 문구를 적었을 수 있으므로 글꼴 표({\fonttbl ...}) 안만 고친다.
void ReplaceLegacyFontNames(std::string& rtf)
{
    static const std::string kLegacyPrefix = "Segoe UI Variable";
    char editorFace[LF_FACESIZE] = {};
    WideCharToMultiByte(CP_ACP, 0, kEditorFontFace, -1, editorFace, sizeof(editorFace), nullptr, nullptr);

    const size_t tableStart = rtf.find("{\\fonttbl");
    if (tableStart == std::string::npos) return;
    // 글꼴 표의 끝: 여는 중괄호와 짝이 맞는 닫는 중괄호 (이스케이프된 \{ \}는 세지 않는다)
    size_t tableEnd = std::string::npos;
    int depth = 0;
    for (size_t i = tableStart; i < rtf.size(); ++i) {
        if (rtf[i] == '\\') { ++i; continue; }
        if (rtf[i] == '{') ++depth;
        else if (rtf[i] == '}' && --depth == 0) { tableEnd = i; break; }
    }
    if (tableEnd == std::string::npos) return;

    // 글꼴 이름은 세미콜론으로 끝난다: "Segoe UI Variable Text;" → "Segoe UI;"
    size_t pos = tableStart;
    while ((pos = rtf.find(kLegacyPrefix, pos)) != std::string::npos && pos < tableEnd) {
        const size_t nameEnd = rtf.find(';', pos);
        if (nameEnd == std::string::npos || nameEnd > tableEnd) break;
        rtf.replace(pos, nameEnd - pos, editorFace);
        tableEnd -= (nameEnd - pos) - strlen(editorFace);
        pos += strlen(editorFace);
    }
}

bool GetDialogResultPath(IFileDialog* dialog, std::wstring& path)
{
    IShellItem* item = nullptr;
    if (FAILED(dialog->GetResult(&item)) || !item) return false;
    wchar_t* name = nullptr;
    const bool ok = SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)) && name;
    if (ok) path = name;
    CoTaskMemFree(name);
    item->Release();
    return ok;
}

}  // namespace

std::wstring EncodingName(TextEncoding encoding)
{
    switch (encoding) {
    case TextEncoding::Utf8Bom: return L"UTF-8 (BOM)";
    case TextEncoding::Utf16Le: return L"UTF-16 LE";
    case TextEncoding::Utf16Be: return L"UTF-16 BE";
    case TextEncoding::Ansi:    return L"ANSI (CP" + std::to_wstring(LegacyCodePage()) + L")";
    default:                    return L"UTF-8";
    }
}

FileFormat FormatFromPath(const std::wstring& path)
{
    return Extension(path) == L".txt" ? FileFormat::Text : FileFormat::Rtf;
}

bool ShowOpenDialog(HWND owner, const std::wstring& startFolder, std::wstring& path)
{
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return false;

    const COMDLG_FILTERSPEC filters[] = {
        {Str(StrId::FilterDocuments), L"*.rtf;*.txt"},
        {Str(StrId::FilterRtf), L"*.rtf"},
        {Str(StrId::FilterTxt), L"*.txt"},
        {Str(StrId::FilterAll), L"*.*"},
    };
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    dialog->SetFileTypes(ARRAYSIZE(filters), filters);
    if (!startFolder.empty()) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(startFolder.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    const bool ok = SUCCEEDED(dialog->Show(owner)) && GetDialogResultPath(dialog, path);
    dialog->Release();
    return ok;
}

bool ShowSaveDialog(HWND owner, const std::wstring& currentPath, FileFormat currentFormat, std::wstring& path)
{
    IFileSaveDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return false;

    const COMDLG_FILTERSPEC filters[] = {
        {Str(StrId::FilterRtf), L"*.rtf"},
        {Str(StrId::FilterTxt), L"*.txt"},
    };
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT);
    dialog->SetFileTypes(ARRAYSIZE(filters), filters);
    // 기본 확장자를 지정해 두면 사용자가 파일 형식을 바꿀 때 확장자도 따라 바뀐다
    const bool text = currentFormat == FileFormat::Text;
    dialog->SetFileTypeIndex(text ? 2 : 1);
    dialog->SetDefaultExtension(text ? L"txt" : L"rtf");

    if (currentPath.empty()) {
        dialog->SetFileName(Str(StrId::Untitled));
    } else {
        const size_t slash = currentPath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            IShellItem* folder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(currentPath.substr(0, slash).c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                dialog->SetFolder(folder);
                folder->Release();
            }
        }
        dialog->SetFileName(slash == std::wstring::npos ? currentPath.c_str() : currentPath.c_str() + slash + 1);
    }

    const bool ok = SUCCEEDED(dialog->Show(owner)) && GetDialogResultPath(dialog, path);
    dialog->Release();
    return ok;
}

bool LoadFileIntoEditor(HWND editor, const std::wstring& path, FileFormat& format, TextFileInfo& textInfo,
                        std::wstring& error)
{
    const WaitCursor waitCursor;

    // 파일을 먼저 전부 읽어 두어, 읽기 실패 시 현재 문서를 건드리지 않는다
    std::string bytes;
    if (!ReadWholeFile(path, bytes, error)) return false;

    // .txt가 아니고 RTF 헤더로 시작하면 서식 있는 문서로 연다
    const bool isRtf = Extension(path) != L".txt" && bytes.size() >= 5 && memcmp(bytes.data(), "{\\rtf", 5) == 0;

    // 큰 문서를 읽는 동안 화면을 다시 그리지 않는다
    SendMessageW(editor, WM_SETREDRAW, FALSE, 0);
    bool ok;
    TextFileInfo info;
    if (isRtf) {
        ReplaceLegacyFontNames(bytes);
        ok = StreamIn(editor, bytes.data(), bytes.size(), SF_RTF);
        if (ok) NormalizeLoadedRichText(editor);
    } else {
        const std::wstring text = DecodeText(bytes, info.encoding);
        info.lineEnding = DetectLineEnding(text);
        ok = StreamIn(editor, text.data(), text.size() * sizeof(wchar_t), SF_TEXT | SF_UNICODE);
        if (ok) ApplyPlainTextFormatting(editor);
    }
    SendMessageW(editor, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(editor, nullptr, TRUE);

    if (!ok) {
        ResetEditorDocument(editor);
        error = Str(StrId::ErrorCorruptFile);
        return false;
    }

    CHARRANGE start = {0, 0};
    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&start));
    SendMessageW(editor, EM_SCROLLCARET, 0, 0);
    MarkEditorClean(editor);
    // 빈 .rtf(탐색기에서 새로 만든 파일 등)처럼 헤더가 없어도 .rtf는 RTF로 저장한다.
    // TXT로 두면 .rtf 파일에 UTF-8 텍스트가 저장되어 다른 프로그램에서 한글이 깨지고 글자색도 사라진다.
    format = (isRtf || Extension(path) == L".rtf") ? FileFormat::Rtf : FileFormat::Text;
    textInfo = info;
    return true;
}

SaveResult SaveEditorToFile(HWND editor, const std::wstring& path, FileFormat format, const TextFileInfo& textInfo,
                            std::wstring& error)
{
    const WaitCursor waitCursor;
    std::string bytes;
    bool ok;
    if (format == FileFormat::Text) {
        // 서식은 버리고 텍스트만 원래 인코딩/줄바꿈으로 저장한다
        std::string utf16;
        ok = StreamOut(editor, SF_TEXT | SF_UNICODE, utf16);
        if (ok) {
            const std::wstring text = ConvertLineEndings(reinterpret_cast<const wchar_t*>(utf16.data()),
                                                         utf16.size() / sizeof(wchar_t), textInfo.lineEnding);
            if (!EncodeText(text, textInfo.encoding, bytes)) return SaveResult::NeedsUnicode;
        }
    } else {
        // 테마 기본색으로 칠해진 글자는 파일에 "자동 색"으로 저장해 다른 테마/앱에서도 자연스럽게 보이게 한다
        const long themeColor = static_cast<long>(DefaultTextColor());
        ReplaceTextColor(editor, themeColor, kAutoColor);
        ok = StreamOut(editor, SF_RTF, bytes);
        ReplaceTextColor(editor, kAutoColor, themeColor);
    }
    if (!ok) {
        error = Str(StrId::ErrorConvert);
        return SaveResult::Failed;
    }
    if (!WriteWholeFile(path, bytes, error)) return SaveResult::Failed;

    SendMessageW(editor, EM_SETMODIFY, FALSE, 0);
    return SaveResult::Ok;
}
