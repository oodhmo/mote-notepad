#pragma once

// .txt/.rtf의 "연결 프로그램" 목록과 Windows 기본 앱 설정에 mote를 등록한다.
// 설치기가 없는 포터블 앱이므로 사용자가 도움말 메뉴에서 원할 때만 현재 사용자(HKCU) 레지스트리에 쓴다.

bool IsOpenWithRegistered();

// 현재 실행 파일 경로로 등록한다. 실패하면 false (일부만 쓴 값은 지운다).
bool RegisterOpenWith();

// 등록한 키와 값을 모두 지운다
void UnregisterOpenWith();

// 등록된 상태에서 실행 파일을 다른 폴더로 옮겼으면 새 경로로 다시 등록한다 (시작할 때 호출)
void RefreshOpenWithPath();
