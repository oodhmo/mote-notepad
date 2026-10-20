#pragma once

// 화면에 보이는 문구. Windows 표시 언어가 한국어면 한국어, 그 외에는 영어로 보여 준다.
// (메뉴는 resources/app.rc에 언어별로 따로 있다)
enum class StrId {
    // 상단 바
    MenuFile,
    MenuEdit,
    MenuHelp,
    TipBold,
    TipItalic,
    TipUnderline,
    TipTextColor,
    TipHideToolbar,
    TipShowToolbar,
    CustomColor,
    AssignColorShortcut,  // %d: 1~3 (Ctrl+F1~F3)
    RecentColors,
    // 메뉴에서 바꿔 쓰는 항목
    RecentEmpty,
    RecentClear,
    MenuHideToolbar,
    MenuShowToolbar,
    // 찾기 바
    FindCue,
    ReplaceCue,
    ReplaceButton,
    ReplaceAllButton,
    TipFindPrev,
    TipFindNext,
    TipCloseFind,
    TipToggleReplace,
    TipReplace,
    TipReplaceAll,
    MatchCountFormat,  // %d: 전체 개수 (현재 번호를 모를 때)
    ReplacedFormat,    // %d: 바꾼 개수
    // 저장 확인창
    SaveChangesTitle,
    Save,
    DontSave,
    Cancel,
    SaveUtf8Title,
    SaveUtf8Message,   // %s: 원래 인코딩 이름
    SaveUtf8Button,
    // 파일
    Untitled,
    FilterDocuments,
    FilterRtf,
    FilterTxt,
    FilterAll,
    // 오류
    ErrorOpenFile,
    ErrorSaveFile,
    ErrorNewWindow,
    ErrorExePath,
    ErrorUnknown,
    ErrorFileTooLarge,
    ErrorCorruptFile,
    ErrorConvert,
    ErrorRichEdit,
    ErrorCreateWindow,
    // 도움말
    HelpTitle,
    HelpSummary,
    HelpSectionFile,
    HelpSectionEdit,
    HelpSectionFormat,
    HelpSectionFind,
    HelpSectionView,
    HelpNew,
    HelpNewWindow,
    HelpOpen,
    HelpSaveAs,
    HelpDropNote,
    HelpUndo,
    HelpRedo,
    HelpClipboard,
    HelpSelectAll,
    HelpBold,
    HelpItalic,
    HelpUnderline,
    HelpColor,
    HelpColorNote,
    HelpFind,
    HelpReplace,
    HelpFindNext,
    HelpFindPrevious,
    HelpCloseFind,
    HelpToolbar,
    AboutVersion,      // %s: 버전
    AboutDescription,
    Close,
    // 연결 프로그램 등록
    MenuRegisterOpenWith,
    MenuUnregisterOpenWith,
    RegisterTitle,
    RegisterMessage,
    RegisterButton,
    RegisteredTitle,
    RegisteredMessage,
    OpenSettingsButton,
    UnregisterTitle,
    UnregisterMessage,
    UnregisterButton,
    ErrorRegister,
    OpenWithTextType,
    OpenWithRichType,
    FirstRunRegisterTitle,
    FirstRunRegisterMessage,
    LaterButton,
    Count
};

// Windows 표시 언어가 한국어인지 (처음 한 번만 확인한다)
bool IsKoreanUi();

const wchar_t* Str(StrId id);
