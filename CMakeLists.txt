cmake_minimum_required(VERSION 3.25)
project(Gutenberg VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(GUTENBERG_SANITIZE "Enable address and undefined behavior sanitizers" OFF)

find_package(ZLIB REQUIRED)
add_library(gutenberg_core
    core/src/bytes.cpp
    core/src/archive.cpp
    core/src/resources.cpp
    core/src/dex.cpp
    core/src/runtime.cpp
    core/src/interpreter.cpp
    core/src/java.cpp
    core/src/package.cpp
    core/src/android.cpp
    core/src/android_views.cpp
    core/src/android_services.cpp
    core/src/jit.cpp
    core/src/jit_memory.cpp
    core/src/jit_x64.cpp
    core/src/jit_aarch64.cpp
)
target_include_directories(gutenberg_core PUBLIC core/include)
target_link_libraries(gutenberg_core PUBLIC ZLIB::ZLIB)
target_compile_options(gutenberg_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
if(GUTENBERG_SANITIZE)
    target_compile_options(gutenberg_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(gutenberg_core PUBLIC -fsanitize=address,undefined)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(OBJCXX)
    add_executable(Gutenberg MACOSX_BUNDLE
        ios/App/main.mm
        ios/App/GBAppDelegate.mm
        ios/Library/GBLibraryStore.mm
        ios/Library/GBLibraryController.mm
        ios/Runtime/GBSceneView.mm
        ios/Runtime/GBRuntimeController.mm
    )
    target_link_libraries(Gutenberg PRIVATE gutenberg_core "-framework UIKit" "-framework Foundation" "-framework CoreGraphics" "-framework UniformTypeIdentifiers")
    target_compile_options(Gutenberg PRIVATE -fobjc-arc -Wall -Wextra)
    set_target_properties(Gutenberg PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "app.gutenberg.ios"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_ATTRIBUTE_ENABLE_USER_SCRIPT_SANDBOXING "YES"
        XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE "NO"
    )
    if(GUTENBERG_TEST_APK)
        find_package(XCTest REQUIRED)
        xctest_add_bundle(GutenbergTests Gutenberg ios/Tests/GBApplicationTests.mm "${GUTENBERG_TEST_APK}")
        set_source_files_properties("${GUTENBERG_TEST_APK}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_compile_options(GutenbergTests PRIVATE -fobjc-arc)
        target_link_libraries(GutenbergTests PRIVATE "-framework UIKit")
        set_target_properties(GutenbergTests PROPERTIES
            XCODE_GENERATE_SCHEME YES
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "app.gutenberg.tests"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
            XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE "YES"
        )
    endif()
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(gutenberg tools/main.cpp)
    target_link_libraries(gutenberg PRIVATE gutenberg_core)
    include(CTest)
    if(BUILD_TESTING)
        add_executable(apk_tests tests/apk_tests.cpp)
        target_link_libraries(apk_tests PRIVATE gutenberg_core)
        add_executable(core_tests tests/core_tests.cpp tests/resource_tests.cpp tests/dex_tests.cpp tests/runtime_tests.cpp tests/java_tests.cpp tests/jit_tests.cpp)
        target_link_libraries(core_tests PRIVATE gutenberg_core)
        add_test(NAME core_tests COMMAND core_tests)
    endif()
endif()
