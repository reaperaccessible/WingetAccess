cmake_minimum_required(VERSION 3.22)
project(WingetAccess VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- wxWidgets (static, via vcpkg x64-windows-static) -------------------------
# Pass at configure time:
#   -DCMAKE_TOOLCHAIN_FILE=C:/Claude/vcpkg/scripts/buildsystems/vcpkg.cmake
#   -DVCPKG_TARGET_TRIPLET=x64-windows-static
set(wxWidgets_USE_STATIC ON)
find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

# Match wxWidgets' static-CRT build (x64-windows-static uses /MT).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(WingetAccess WIN32
    Source/App.cpp
    Source/MainWindow.cpp
    Source/WingetRunner.cpp
    Source/WingetParser.cpp
    Source/Announce.cpp
    Source/WingetAccess.rc     # manifest (asInvoker/DPI) + version info
)

target_include_directories(WingetAccess PRIVATE Source)

target_link_libraries(WingetAccess PRIVATE
    ${wxWidgets_LIBRARIES}
    user32
    oleaut32          # SysAllocString / SysFreeString (UIA notification text)
    uiautomationcore  # UiaRaiseNotificationEvent (screen-reader announcements)
)

# The manifest is embedded through WingetAccess.rc (RT_MANIFEST); suppress the
# linker's own manifest to avoid a duplicate.
set_target_properties(WingetAccess PROPERTIES
    LINK_FLAGS "/MANIFEST:NO"
    OUTPUT_NAME "WingetAccess"
)

# UTF-8 source + execution charset for the accented FR string literals.
if(MSVC)
    target_compile_options(WingetAccess PRIVATE "/utf-8")
endif()

# --- Optional headless parser test (console) ----------------------------------
option(BUILD_PARSETEST "Build the winget-output parser test harness" OFF)
if(BUILD_PARSETEST)
    add_executable(parsetest
        tools/parsetest.cpp
        Source/WingetParser.cpp
    )
    target_include_directories(parsetest PRIVATE Source)
    target_link_libraries(parsetest PRIVATE ${wxWidgets_LIBRARIES})
    if(MSVC)
        target_compile_options(parsetest PRIVATE "/utf-8")
    endif()
endif()
