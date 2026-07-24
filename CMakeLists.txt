# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

cmake_minimum_required(VERSION 3.14)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# 【追加修正1】強制的に Pico のコンパイラを指定（Macコンパイラの誤検知を防ぐ）
if(NOT BUILD_TESTS)
    set(CMAKE_C_COMPILER arm-none-eabi-gcc)
    set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
    set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
endif()

option(BUILD_TESTS "Build host unit tests instead of Pico UF2" OFF)

if(NOT BUILD_TESTS)
    set(PICO_BOARD pico2 CACHE STRING "Board type")
    include(pico_sdk_import.cmake OPTIONAL)
endif()

project(pico_basic C CXX ASM)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 【追加修正2】Pico SDK で C++ の例外処理 (try/catch/throw) を有効にする
set(PICO_CXX_ENABLE_EXCEPTIONS 1)

if(BUILD_TESTS)
    message(STATUS "🚀 Building for HOST tests (Google Test will be downloaded)")
    
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_library(core_lib
        src/lexer.cpp
        src/parser.cpp
        src/memory_manager.cpp
        src/program_manager.cpp
        src/expression_parser.cpp
        src/statement_core.cpp
        src/statement_graphics_io.cpp
        src/hal_touch.cpp
        src/hal_sdcard.cpp
    )
    target_include_directories(core_lib PUBLIC src)

    add_executable(basic_tests
        tests/test_lexer.cpp
        tests/test_parser.cpp
        tests/test_input_end_stop.cpp
        tests/test_graphics.cpp
        tests/test_gpio_brightness.cpp
        tests/test_advanced_graphics.cpp
        tests/test_extended_core.cpp
        tests/test_control_flow_ext.cpp
        tests/test_psg_envelope.cpp
        tests/test_elseif_labels.cpp
        tests/test_operators_deffn.cpp
        tests/test_edit_debug_lang.cpp
        tests/test_file_ops.cpp
        tests/test_builtin_functions_ext.cpp
        tests/test_advanced_features.cpp
        tests/test_sound_logic.cpp
        tests/test_touch_play.cpp
        tests/test_2d_array_longvar_string.cpp
        tests/mock_hal_display.cpp
        src/hal_gpio.cpp
        src/hal_sound.cpp
    )
    target_include_directories(basic_tests PRIVATE src tests)
    target_link_libraries(basic_tests PRIVATE core_lib GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(basic_tests)

else()
    message(STATUS "🍓 Building for Pico RP2350 target")
    
    if (COMMAND pico_sdk_init)
        pico_sdk_init()
    else()
        message(WARNING "pico_sdk_import.cmake is not found. Please ensure Raspberry Pi Pico SDK is configured properly.")
    endif()

    # MicroSD (FatFS + SPI ドライバ)。ボード固有のピン定義は src/hw_config.c
    include(FetchContent)
    FetchContent_Declare(
        no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
        GIT_REPOSITORY https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico.git
        # タグは上流で付け替えられる可能性があるため、コミット SHA で固定する
        GIT_TAG        6c644cfc3ab03c161fee2dd7be4877e5b832fa71 # v3.6.2
        SOURCE_SUBDIR  src
    )
    # SD ドライバのメッセージは USE_PRINTF を定義せず、
    # hal_sdcard.cpp の put_out_error_message() 等で受け取って制御する。
    # （カード抜き差しの検出で想定内の失敗が出るため、そこだけ黙らせたい）
    # ドライバの詳細ログが必要なときは USE_PRINTF / USE_DBG_PRINTF を足す。

    FetchContent_MakeAvailable(no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)

    add_executable(pico_basic
        src/main.cpp
        src/hw_config.c
        src/hal_display.cpp
        src/hal_gpio.cpp
        src/hal_sound.cpp
        src/hal_touch.cpp
        src/hal_sdcard.cpp
        src/repl.cpp
        src/lexer.cpp
        src/parser.cpp
        src/memory_manager.cpp
        src/program_manager.cpp
        src/expression_parser.cpp
        src/statement_core.cpp
        src/statement_graphics_io.cpp
    )

    if (TARGET pico_stdlib)
        pico_generate_pio_header(pico_basic ${CMAKE_CURRENT_LIST_DIR}/src/pio/i2s_out.pio)
        target_include_directories(pico_basic PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

        target_link_libraries(pico_basic
            pico_stdlib
            hardware_spi
            hardware_i2c
            hardware_pwm     # LCD バックライト調光（サウンドではない）
            hardware_pio     # I2S サウンド出力
            hardware_dma
            hardware_irq
            hardware_clocks
            no-OS-FatFS-SD-SDIO-SPI-RPi-Pico  # MicroSD (FatFS)
        )
        
        pico_enable_stdio_usb(pico_basic 1)
        pico_enable_stdio_uart(pico_basic 1)
        
        pico_add_extra_outputs(pico_basic)
    endif()
endif()