cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME STM32WB55XX-HERA)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    BLEServices/Services.c
    BLEServices/svc_led.c
    Audio/App/audio_capture_ring_buff.c
    Audio/App/audio_stream.c
    Audio/App/audio_vad.c
    Audio/App/audio_app.c
    Audio/Frontend/logmel.c
    VM3011/vm3011.c
    VOFA+/VOFA.c
    W25Q80/platform_w25q80_stm32_impl.c
    W25Q80/w25_disk.c
    W25Q80/usb_msc.c
    W25Q80/shell_cli.c
    W25Q80/wav_recorder.c
    W25Q80/ymodem_tx.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_rfft_init_q15.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_rfft_q15.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_init_q15.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_q15.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_radix4_q15.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_bitreversal.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_bitreversal2.c
    Drivers/CMSIS/DSP/Source/CommonTables/arm_common_tables.c
    Drivers/CMSIS/DSP/Source/CommonTables/arm_const_structs.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_rfft_fast_f32.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_rfft_fast_init_f32.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_f32.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_init_f32.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_radix8_f32.c
    Drivers/CMSIS/DSP/Source/BasicMathFunctions/arm_offset_f32.c
    Drivers/CMSIS/DSP/Source/FastMathFunctions/arm_vlog_f32.c
    Drivers/CMSIS/DSP/Source/StatisticsFunctions/arm_absmax_q15.c
    Drivers/CMSIS/DSP/Source/BasicMathFunctions/arm_shift_q15.c
)

# ---- Native TFLM runtime (tflm_c C wrapper over TFLite-Micro C++) ----------
set(TFLM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/tensorflow)
file(GLOB TFLM_CORE_SOURCES
    ${TFLM_DIR}/tensorflow/lite/core/api/*.cc
    ${TFLM_DIR}/tensorflow/lite/core/c/*.cc
    ${TFLM_DIR}/tensorflow/lite/schema/*.cc
    ${TFLM_DIR}/tensorflow/lite/kernels/internal/*.cc
    ${TFLM_DIR}/tensorflow/lite/kernels/internal/reference/*.cc
    ${TFLM_DIR}/tensorflow/lite/kernels/kernel_util.cc
    ${TFLM_DIR}/tensorflow/lite/micro/*.cc
    ${TFLM_DIR}/tensorflow/lite/micro/arena_allocator/*.cc
    ${TFLM_DIR}/tensorflow/lite/micro/memory_planner/*.cc
    ${TFLM_DIR}/tensorflow/lite/micro/tflite_bridge/*.cc
    ${TFLM_DIR}/tensorflow/lite/micro/kernels/*.cc
    ${TFLM_DIR}/tensorflow/lite/micro/kernels/cmsis_nn/*.cc
)
# Test-only helpers and platform stubs never referenced by the app.
list(FILTER TFLM_CORE_SOURCES EXCLUDE REGEX
    "test_helper|mock_micro_graph|kernel_runner|system_setup")

add_library(tflm_runtime STATIC ${TFLM_CORE_SOURCES})
target_sources(tflm_runtime PRIVATE
    X-CUBE-AI/App/tflm_c.cc
    X-CUBE-AI/App/kws_opt_kernels.cc
    X-CUBE-AI/App/debug_log_imp.cc
)
# USE_PRINTF: route TFLM DebugLog through the app's printf (-> USART1).
target_compile_definitions(tflm_runtime PRIVATE USE_PRINTF)
target_link_libraries(tflm_runtime PUBLIC stm32cubemx)
# -Os: the hot arm_* CMSIS-NN kernels are compiled by the stm32cubemx
# target (already -O3); -O3 here would only bloat the glue layer (+92 KB,
# overflows the 512 KB budget) with no measurable gain.
set_source_files_properties(${TFLM_CORE_SOURCES}
    X-CUBE-AI/App/tflm_c.cc X-CUBE-AI/App/debug_log_imp.cc
    PROPERTIES COMPILE_OPTIONS "-Os")

# KWS 自定义热点内核（广播 ADD/MUL + LUT LOGISTIC）单独用 -O3，文件小、FLASH 增量 ~1KB
set_source_files_properties(X-CUBE-AI/App/kws_opt_kernels.cc
    PROPERTIES COMPILE_OPTIONS "-O3")

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    BLEServices
    Audio/App
    Audio/Frontend
    Audio/kws
    VM3011
    VOFA+
    W25Q80
    PDM2PCM
    Drivers/CMSIS/DSP/Include
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# FLASH 紧张：存储/CLI 模块用 -Os 覆盖 Debug 的 -Og
set_source_files_properties(
    W25Q80/w25_disk.c W25Q80/usb_msc.c
    W25Q80/shell_cli.c W25Q80/wav_recorder.c W25Q80/ymodem_tx.c
    W25Q80/platform_w25q80_stm32_impl.c
    PROPERTIES COMPILE_OPTIONS "-Os")

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    tflm_runtime

    # Add user defined libraries
    # PDM2PCM decimation filter (hard-float toolchain)
    ${CMAKE_CURRENT_SOURCE_DIR}/PDM2PCM/libPDMFilter_CM4_GCC_wc16_hardcompat.a
)

# Pull in float formatting for printf (nano.specs): KWS prints softmax %.1f
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "-Wl,-u,_printf_float")

# Generate binary and hex files from the ELF output
set(ELF_FILE ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.elf)
set(BIN_FILE ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin)
set(HEX_FILE ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex)
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${ELF_FILE} ${BIN_FILE}
    COMMAND ${CMAKE_OBJCOPY} -O ihex   ${ELF_FILE} ${HEX_FILE}
    COMMENT "Generating ${BIN_FILE} and ${HEX_FILE}"
    VERBATIM
)
