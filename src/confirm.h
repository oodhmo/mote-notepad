#pragma once
#include <windows.h>
#include <string>

enum class SaveChoice { Save, DontSave, Cancel };

// "변경 내용을 저장하시겠습니까?" 확인창. TaskDialog 대신 mote 톤에 맞춰 직접 그리는 작은 모달 창이다.
// owner는 창이 떠 있는 동안 비활성화된다.
SaveChoice AskSaveChanges(HWND owner, const std::wstring& documentName);

// 같은 모양의 [취소] [okLabel] 확인창. okLabel을 누르면 true. cancelLabel을 주면 "취소" 대신 쓴다.
bool AskConfirm(HWND owner, const std::wstring& title, const std::wstring& message, const wchar_t* okLabel,
                const wchar_t* cancelLabel = nullptr);
