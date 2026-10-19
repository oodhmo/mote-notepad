cmake_minimum_required(VERSION 3.21)

# 정적 CRT 선택(MSVC_RUNTIME_LIBRARY)을 쓰려면 CMP0091이 필요하다
cmake_policy(SET CMP0091 NEW)

project(mote LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
    message(FATAL_ERROR "mote는 MSVC로만 빌드합니다.")
endif()

add_executable(mote WIN32
    src/main.cpp
    src/editor.cpp
    src/file_io.cpp
    src/topbar.cpp
    src/theme.cpp
    src/findbar.cpp
    src/settings.cpp
    src/confirm.cpp
    src/help.cpp
    src/registration.cpp
    src/strings.cpp
    resources/app.rc
    resources/app.manifest
)

target_compile_definitions(mote PRIVATE
    UNICODE _UNICODE NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00
)

target_compile_options(mote PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /W4 /permissive- /Zc:__cplusplus>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/O1 /GL /Gy>
)

target_link_options(mote PRIVATE
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO>
)

# 별도 런타임 설치 없이 실행되도록 CRT를 정적으로 링크한다
set_property(TARGET mote PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 모두 Windows에 기본 포함된 시스템 DLL이다
target_link_libraries(mote PRIVATE
    comctl32 comdlg32 uxtheme dwmapi gdiplus ole32 shell32 shlwapi
)
