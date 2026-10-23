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
    Audio/Frontend/audio_frontend.c
    VM3011/vm3011.c
    VOFA+/VOFA.c
    W25Q80/platform_w25q80_stm32_impl.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_rfft_init_q15.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_rfft_q15.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_init_q15.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_q15.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_radix4_q15.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_bitreversal.c
    Drivers/CMSIS/DSP/Source/TransformFunctions/arm_bitreversal2.c
    Drivers/CMSIS/DSP/Source/CommonTables/arm_common_tables.c
    Drivers/CMSIS/DSP/Source/CommonTables/arm_const_structs.c
    Drivers/CMSIS/DSP/Source/StatisticsFunctions/arm_absmax_q15.c
    Drivers/CMSIS/DSP/Source/BasicMathFunctions/arm_shift_q15.c
)

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

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

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
