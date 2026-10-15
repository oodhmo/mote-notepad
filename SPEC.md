## 프로젝트 목표

Windows 11에서 사용할 수 있는 초경량 데스크톱 텍스트 에디터를 만든다.

기본 UX는 Windows 메모장(Notepad)처럼 최대한 단순해야 하며, 일반 메모장과의 가장 큰 차이는 사용자가 선택한 텍스트에 직접 글자색을 지정할 수 있다는 점이다.

불필요한 워드프로세서 기능은 넣지 않는다.

핵심 목표:
- 매우 빠른 실행 속도
- 작은 실행 파일 크기
- 낮은 메모리 사용량
- Windows 11에 어울리는 깔끔한 UI
- 선택 영역 글자색 변경
- 맞춤법 검사 기능 없음
- 별도 런타임 설치 없이 실행 가능하도록 배포

---

## 기술 스택

### 권장 기술

- Language: C++20
- Platform: Native Win32 API
- Text Editor Control: Windows Rich Edit Control
  - `Msftedit.dll`
  - `RICHEDIT50W`
- Build System: CMake
- Compiler: MSVC
- UI Framework:
  - Win32 API만 사용
  - Qt, Electron, Tauri, WinUI 3, .NET, WebView 사용 금지
- Character Encoding:
  - 내부 처리: UTF-16
  - plain text import/export: UTF-8
- Rich text format:
  - `.rtf`

### 기술 선택 이유

프로그램을 최대한 작고 빠르게 만들기 위해 별도 UI 프레임워크를 사용하지 않는다.

Windows에 기본 포함된 Rich Edit Control을 사용하여 다음 기능을 구현한다.

- rich text rendering
- selection formatting
- font color
- bold / italic / underline
- undo / redo
- clipboard
- text selection
- RTF streaming

외부 라이브러리는 가능한 사용하지 않는다.

---

## 핵심 기능

### 1. 텍스트 편집

필수 기능:

- 텍스트 입력
- 텍스트 선택
- 복사
- 붙여넣기
- 잘라내기
- 실행 취소
- 다시 실행
- 전체 선택

단축키:

- Ctrl+C
- Ctrl+V
- Ctrl+X
- Ctrl+Z
- Ctrl+Y
- Ctrl+A

---

### 2. 글자색 변경

가장 중요한 기능이다.

사용자가 텍스트를 선택한 후 툴바의 색상 버튼을 누르면 선택한 영역의 글자색이 변경되어야 한다.

구현 방식:

- Rich Edit의 `EM_SETCHARFORMAT`
- `CHARFORMAT2`
- `CFE_AUTOCOLOR`
- `crTextColor`

지원 색상은 최소 다음 프리셋을 제공한다.

- 기본 검정
- 빨강
- 주황
- 파랑
- 초록
- 보라
- 회색

추가로 Windows 기본 Color Picker를 열 수 있는 Custom Color 버튼을 제공한다.

색상이 지정되지 않은 새 텍스트는 기본 텍스트 색상을 따른다.

---

### 3. 기본 서식

다음 세 개만 지원한다.

- Bold
- Italic
- Underline

단축키:

- Ctrl+B
- Ctrl+I
- Ctrl+U

Font family 변경 기능은 MVP에서 제외한다.

Font size 변경 기능도 MVP에서는 제외한다.

기본 폰트:

- Segoe UI
- 11pt 또는 Windows 기본 UI에 자연스러운 크기

---

### 4. 파일

지원 파일 형식:

#### RTF
기본 저장 형식.

`.rtf`

다음 정보가 유지되어야 한다.

- 텍스트
- 글자색
- bold
- italic
- underline

#### TXT
`.txt` 읽기와 저장도 지원한다.

TXT 저장 시 rich text 서식은 제거하고 plain text만 저장한다.

TXT 읽기 시 UTF-8을 기본으로 처리한다.

메뉴:

File
- New
- Open
- Save
- Save As
- Exit

단축키:

- Ctrl+N
- Ctrl+O
- Ctrl+S
- Ctrl+Shift+S

---

### 5. 저장되지 않은 변경 감지

문서 수정 상태를 추적한다.

수정된 상태에서 다음 동작을 수행하면 확인창을 띄운다.

- 새 파일
- 다른 파일 열기
- 프로그램 종료

문구 예:

"변경 내용을 저장하시겠습니까?"

선택:

- Save
- Don't Save
- Cancel

---

### 6. 찾기 / 바꾸기

필수:

- Find
- Find Next
- Replace
- Replace All

단축키:

- Ctrl+F
- Ctrl+H
- F3

별도 복잡한 검색 옵션은 필요 없다.

지원하지 않아도 되는 기능:

- Regex
- Case-sensitive 옵션
- Whole word 옵션

가능하면 단순 substring 검색만 구현한다.

---

### 7. UI

목표:

Windows 11 기본 메모장처럼 최대한 단순하고 깔끔하게 만든다.

화면 구성:

```text
┌─────────────────────────────────────┐
│ File  Edit  Format                  │
├─────────────────────────────────────┤
│ B  I  U  ●Text Color               │
├─────────────────────────────────────┤
│                                     │
│                                     │
│          Rich Edit Area             │
│                                     │
│                                     │
├─────────────────────────────────────┤
│ Ln 1, Col 1                         │
└─────────────────────────────────────┘
```

툴바에는 다음만 표시한다.

- Bold
- Italic
- Underline
- Text Color

가능하면 아이콘 기반으로 구현한다.

과도한 ribbon UI를 사용하지 않는다.

사이드바 없음.

문서 탭 없음.

페이지 단위 UI 없음.

눈금자 없음.

---

## Windows 11 스타일

가능하면 다음을 적용한다.

- Segoe UI
- Windows 11 기본 배경색
- 둥근 느낌의 버튼 또는 flat toolbar
- 다크 모드 대응

다크 모드에서는:

- editor background: dark
- default text: light
- toolbar/menu: dark

하지만 사용자가 직접 지정한 글자색은 그대로 유지한다.

Windows 시스템 테마를 감지하여 자동으로 Light / Dark를 변경한다.

MVP에서 다크 모드 구현이 복잡하면 후순위로 미뤄도 된다.

---

## 반드시 제외할 기능

이 프로그램의 목적은 워드프로세서가 아니다.

다음 기능은 절대 넣지 않는다.

- 맞춤법 검사
- 문법 검사
- 자동 교정
- AI 기능
- 클라우드 동기화
- 계정
- 로그인
- 표
- 이미지 삽입
- 페이지 레이아웃
- 머리글 / 바닥글
- 인쇄 미리보기
- 스타일 시스템
- 제목 스타일
- 목록 스타일
- Markdown
- 코드 문법 강조
- 플러그인 시스템
- WebView
- 자동 업데이트
- 텔레메트리
- 광고

특히 맞춤법 검사와 빨간 밑줄 기능은 구현하지 않는다.

---

## 성능 목표

가능하면 다음 목표를 지향한다.

Cold start:
- 0.5초 이하

실행 파일:
- 가능하면 5MB 이하
- 목표는 1~3MB 수준

Idle RAM:
- 가능하면 20MB 이하

외부 런타임:
- 없음

설치:
- 필요 없음

배포 형태:

```text
mote.exe
```

단일 실행 파일 또는 실행 파일 + 최소한의 리소스 파일만 제공한다.

Portable 실행이 기본이다.

---

## 에디터 구현 세부사항

Windows Rich Edit Control을 사용한다.

앱 시작 시:

```cpp
LoadLibraryW(L"Msftedit.dll");
```

Rich Edit 컨트롤:

```cpp
MSFTEDIT_CLASS
```

또는:

```cpp
L"RICHEDIT50W"
```

사용.

선택 영역 서식 변경:

```cpp
EM_SETCHARFORMAT
SCF_SELECTION
```

RTF 저장/불러오기:

```cpp
EM_STREAMIN
EM_STREAMOUT
```

사용을 우선 검토한다.

---

## MVP 완료 기준

아래 기능이 모두 동작하면 MVP 완료로 간주한다.

1. 앱 실행
2. 한글 정상 입력
3. 영어 정상 입력
4. 텍스트 선택
5. 선택한 텍스트 색 변경
6. Bold / Italic / Underline
7. RTF 저장
8. 저장된 RTF 다시 열었을 때 글자색 유지
9. TXT 열기
10. TXT 저장
11. 찾기
12. 바꾸기
13. 실행 취소
14. 저장되지 않은 변경 경고
15. Windows 11에서 정상 실행
16. 별도 런타임 설치 필요 없음
17. 맞춤법 빨간 줄이 절대 표시되지 않음

---

## 개발 우선순위

### Phase 1

- Win32 Window 생성
- Rich Edit Control 생성
- 텍스트 입력
- 파일 열기 / 저장
- RTF 지원

### Phase 2

- 글자색
- Bold
- Italic
- Underline
- 툴바

### Phase 3

- 찾기 / 바꾸기
- 수정 상태 감지
- 최근 파일 경로 기억

### Phase 4

- Windows 11 UI 개선
- Light / Dark Theme
- 아이콘 정리

---

## 코드 품질 요구사항

- 기능별로 함수 분리
- 불필요한 클래스 구조를 만들지 않는다.
- 과도한 추상화 금지
- 외부 dependency 최소화
- Windows API 에러를 적절히 처리한다.
- Unicode API의 `W` 버전을 사용한다.
- UI thread에서 모든 editor operation을 처리한다.
- 코드에 중요한 Win32 메시지 처리 이유를 주석으로 남긴다.

프로젝트 구조는 가능한 단순하게 유지한다.

예:

```text
mote/
├─ CMakeLists.txt
├─ src/
│  ├─ main.cpp
│  ├─ editor.cpp
│  ├─ editor.h
│  ├─ file_io.cpp
│  └─ file_io.h
└─ resources/
   └─ app.rc
```

파일이 지나치게 많아지지 않도록 한다.

---

## 제품 철학

이 프로그램은 Microsoft Word를 축소하는 프로그램이 아니다.

Windows Notepad에 딱 하나의 기능,

"선택한 글자의 색을 바꿀 수 있다"

를 추가한 프로그램에 가깝게 만든다.

새로운 기능을 추가할지 고민될 경우 다음 기준을 따른다.

"이 기능이 없으면 메모장으로서 불편한가?"

아니라면 추가하지 않는다.

## 디자인 요구사항

디자인은 기능만큼 중요하다.

전체적인 미감은 **Notion Desktop의 에디터 창처럼 미니멀하고 정돈된 느낌**을 목표로 한다.

단, Notion을 복제하거나 WebView/Electron을 사용하는 방식은 금지한다. 네이티브 Win32 UI만으로 유사한 분위기를 구현한다.

### 디자인 원칙

- 화면에 보이는 요소를 최소화한다.
- 과도한 테두리, 음영, 입체 버튼을 사용하지 않는다.
- 툴바와 편집 영역 사이에 강한 경계를 만들지 않는다.
- 충분한 여백과 padding을 사용한다.
- Windows 11의 현대적인 UI와 자연스럽게 어울려야 한다.
- 전통적인 Win32 프로그램처럼 보이지 않도록 한다.
- 오래된 WordPad / Windows 7 스타일 UI를 피한다.

### 기본 레이아웃

상단은 최대한 얇게 구성한다.

예:

```text
┌───────────────────────────────────────────┐
│ File   Edit                  B  I  U  ●   │
│───────────────────────────────────────────│
│                                           │
│   여백이 넉넉한 텍스트 편집 영역          │
│                                           │
│                                           │
│                                           │
└───────────────────────────────────────────┘
```

### 색상

Light mode 기본:

- Window background: `#FFFFFF`
- Secondary background: `#F7F7F5` 또는 유사한 매우 연한 회색
- Primary text: `#37352F`
- Secondary text: `#787774`
- Border/separator: `#E9E9E7`

강한 검정색(`#000000`)은 가급적 사용하지 않는다.

버튼 hover도 매우 은은하게 표현한다.

### Typography

기본 폰트:

- Segoe UI Variable
- fallback: Segoe UI

에디터 기본 텍스트 크기:

- 약 11~12pt

메뉴와 툴바는 에디터 본문보다 약간 작게 사용한다.

### Toolbar

툴바는 전통적인 Windows toolbar control처럼 보이지 않도록 한다.

버튼은:

- flat
- borderless
- transparent background
- hover 시에만 연한 배경 표시

필수 버튼만 표시:

- Bold
- Italic
- Underline
- Text Color

아이콘 간 간격을 넉넉하게 둔다.

### Text Color UI

Text Color 버튼은 복잡한 드롭다운 대신 작은 원형 color indicator 형태를 우선 고려한다.

클릭 시 작은 popup palette를 표시한다.

팔레트:

- Black
- Gray
- Red
- Orange
- Yellow
- Green
- Blue
- Purple

마지막에 Custom Color 항목을 제공한다.

### Editor

에디터 영역에는 기본적으로 테두리를 표시하지 않는다.

RichEdit 컨트롤의 기본 3D border를 제거한다.

본문 좌우 padding을 충분히 준다.

예:

- left/right: 24~32px
- top: 20~24px

줄 간격도 기본 RichEdit보다 약간 여유롭게 보이도록 가능한 범위 내에서 조정한다.

스크롤바는 Windows 기본 스타일을 사용하되 불필요하게 강조하지 않는다.

### Status Bar

MVP에서는 status bar를 제거해도 된다.

줄/열 정보가 꼭 필요하지 않다면 화면에 표시하지 않는다.

UI 단순성을 우선한다.

### Window Chrome

가능하면 Windows 11 스타일을 적용한다.

- rounded window corners
- dark mode title bar 지원
- native minimize / maximize / close buttons
- 시스템 테마 자동 감지

커스텀 타이틀바는 구현 복잡도가 크게 증가하면 사용하지 않는다.

### 금지 사항

다음 스타일은 피한다.

- Ribbon UI
- Windows 7 스타일 toolbar
- 회색 입체 버튼
- bevel
- gradient
- 과도한 border
- 큰 아이콘
- 여러 줄 toolbar
- sidebar
- document tabs
- floating panels
- Word 같은 복잡한 메뉴 구조

디자인 판단 기준은 다음 문장이다.

"Windows 메모장을 Notion 디자이너가 다시 디자인했다면?"

기능 추가보다 화면의 단순함과 균형을 우선한다.