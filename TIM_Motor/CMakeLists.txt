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
set(CMAKE_PROJECT_NAME TIM_Motor)

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
    Core/Src/oled.c
    Core/Src/font.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
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
)

# ---------------------------------------------------------------------------
# 烧录相关（用户自己加的，CubeMX 不会覆盖这一段）
#
# 1) CubeMX 的 CMake 模板默认只产出 .elf，这里补一步 objcopy 顺手生成 .bin
# 2) 加一个名为 flash 的目标：在 CLion 的运行配置里选中它点运行，
#    就会先编译、再把 .bin 写进 Flash 并复位芯片
# ---------------------------------------------------------------------------
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.bin
    COMMENT "生成 ${CMAKE_PROJECT_NAME}.bin"
)

# STM32CubeProgrammer CLI 的位置。换电脑 / 换 CLT 版本时改这一行即可
set(STM32_PROGRAMMER_CLI
    "C:/ST/STM32CubeCLT_1.18.0/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe"
    CACHE FILEPATH "STM32CubeProgrammer CLI 可执行文件路径")

add_custom_target(flash
    COMMAND ${STM32_PROGRAMMER_CLI}
            -c port=SWD
            -w $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.bin 0x08000000
            -rst
    DEPENDS ${CMAKE_PROJECT_NAME}
    COMMENT "烧录 ${CMAKE_PROJECT_NAME}.bin 到 0x08000000 并复位"
    VERBATIM
)
