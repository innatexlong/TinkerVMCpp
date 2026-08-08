cmake_minimum_required(VERSION 3.30)
project(customVM LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_COMPILER_LAUNCHER "")
set(CMAKE_C_COMPILER_LAUNCHER "")

# ---- 开启模块支持 ----
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "cxxmodules")
else()
    message(FATAL_ERROR "CMake >= 3.28 required for C++20 modules")
endif()

if(USE_ANTLR4)
    # ===================================================================
    # 1. 手动编译 ANTLR4 运行时（替代 add_subdirectory）
    # ===================================================================
    # 收集所有 .cpp 源文件（排除 tests 目录）
    add_definitions(-DANTLR4CPP_STATIC)
    file(GLOB_RECURSE ANTLR4_RUNTIME_SRCS
        "${CMAKE_SOURCE_DIR}/vendor/antlr4/src/*.cpp"
    )
    # 排除测试文件（如果存在 tests 子目录）
    list(FILTER ANTLR4_RUNTIME_SRCS EXCLUDE REGEX "/tests/")

    # 创建静态库
    add_library(antlr4_runtime_manual STATIC ${ANTLR4_RUNTIME_SRCS})
    target_compile_definitions(antlr4_runtime_manual PRIVATE ANTLR4CPP_STATIC)
    target_include_directories(antlr4_runtime_manual PUBLIC "${CMAKE_SOURCE_DIR}/vendor/antlr4/src")
    target_compile_options(antlr4_runtime_manual PRIVATE
        -Wno-old-style-cast -Wno-sign-conversion -Wno-conversion
        -Wno-unused-parameter -Wno-shadow -Wno-undef
        -w
    )

    ## 添加运行时头文件路径（公开，供生成的解析器使用）
    #target_include_directories(antlr4_runtime_manual PUBLIC
    #    "${CMAKE_SOURCE_DIR}/vendor/antlr4/src"
    #)
    #
    ## 为 ANTLR4 运行时放宽编译警告（避免干扰主项目严格选项）
    #target_compile_options(antlr4_runtime_manual PRIVATE
    #    -Wno-old-style-cast
    #    -Wno-sign-conversion
    #    -Wno-conversion
    #    -Wno-unused-parameter
    #    -Wno-shadow
    #    -Wno-undef
    #)

    # ===================================================================
    # 2. 生成解析器代码（使用 ANTLR4 工具）
    # ===================================================================
    # ---- ANTLR 工具 jar 路径 ----
    set(ANTLR_JAR "${CMAKE_SOURCE_DIR}/vendor/antlr4/antlr-4.13.2-complete.jar")
    if(NOT EXISTS ${ANTLR_JAR})
        message(FATAL_ERROR "请将 antlr-4.13.2-complete.jar 放入 vendor/antlr4/")
    endif()

    # ---- 语法文件目录和生成目录 ----
    file(GLOB GRAMMAR_FILES grammar/*.g4)
    if(NOT GRAMMAR_FILES)
        message(FATAL_ERROR "未找到 grammar/*.g4 文件，请先创建语法文件")
    endif()

    set(GENERATED_DIR ${CMAKE_SOURCE_DIR}/src/generated)

    # ---- 根据语法文件名 Vm.g4 显式列出生成的文件 ----
    set(GENERATED_CPPS
        ${GENERATED_DIR}/VmLexer.cpp
        ${GENERATED_DIR}/VmParser.cpp
    )
    set(GENERATED_HEADERS
        ${GENERATED_DIR}/VmLexer.h
        ${GENERATED_DIR}/VmParser.h
        ${GENERATED_DIR}/VmVisitor.h
        ${GENERATED_DIR}/VmBaseVisitor.h
    )

    set(JAVA_EXECUTABLE "C:/Users/Administrator/AppData/Roaming/.minecraft/runtime/java-runtime-delta/bin/java.exe")
    if(NOT EXISTS ${JAVA_EXECUTABLE})
        message(FATAL_ERROR "找不到 Java 可执行文件: ${JAVA_EXECUTABLE}")
    endif()
    # ---- 自定义命令：运行 ANTLR4 生成 C++ 代码 ----
    add_custom_command(
        OUTPUT ${GENERATED_CPPS} ${GENERATED_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
        COMMAND ${JAVA_EXECUTABLE} -jar ${ANTLR_JAR} -Dlanguage=Cpp -o ${GENERATED_DIR} -visitor -no-listener ${GRAMMAR_FILES}
        DEPENDS ${GRAMMAR_FILES} ${ANTLR_JAR}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating ANTLR4 parser code for Custom VM"
    )

    add_custom_target(antlr_generate DEPENDS ${GENERATED_CPPS})

    # ===================================================================
    # 3. 将生成的解析器代码编译为独立静态库
    # ===================================================================
    add_library(antlr_generated STATIC ${GENERATED_CPPS})

    target_compile_options(antlr_generated PRIVATE
        -w
        -fno-modules-ts
    )

    target_include_directories(antlr_generated PRIVATE
        ${GENERATED_DIR}                            # 生成的头文件
        ${CMAKE_SOURCE_DIR}/vendor/antlr4/src       # ANTLR4 运行时头文件（供解析器源码引用）
    )

    add_dependencies(antlr_generated antlr_generate)

    # ===================================================================
    # 4. 主程序 vm（支持 C++26 模块）
    # ===================================================================
    # ---- 收集普通源文件和模块接口 ----
    file(GLOB CPP_SOURCES src/*.cpp)          # 普通 .cpp（不含 .cppm）
    file(GLOB MODULE_INTERFACES src/*.cppm)   # 模块接口

    # ---- 创建可执行文件 ----
    add_executable(vm ${CPP_SOURCES})

    # ---- 将模块接口加入 CXX_MODULES 文件集 ----
    target_sources(vm
        PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS src
        FILES ${MODULE_INTERFACES}
    )

    # ---- 包含目录（系统库路径） ----
    target_include_directories(vm SYSTEM PRIVATE
        vendor/data/include
        vendor/fmt/include
        vendor/conchashmap/include
        ${GENERATED_DIR}                            # 生成的解析器头文件
        ${CMAKE_SOURCE_DIR}/vendor/antlr4/src       # ANTLR4 运行时头文件（用于主程序 #include）
    )
else()
    # ---- 收集普通源文件和模块接口 ----
    file(GLOB CPP_SOURCES src/*.cpp)          # 普通 .cpp（不含 .cppm）
    file(GLOB MODULE_INTERFACES src/*.cppm)   # 模块接口

    # ---- 创建可执行文件 ----
    add_executable(vm ${CPP_SOURCES})

    # ---- 将模块接口加入 CXX_MODULES 文件集 ----
    target_sources(vm
        PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS src
        FILES ${MODULE_INTERFACES}
            src/io_utils.cppm
    )

    # ---- 包含目录（系统库路径） ----
    target_include_directories(vm SYSTEM PRIVATE
        vendor/data/include
        vendor/fmt/include
        vendor/conchashmap/include
    )
endif()

#target_precompile_headers(vm PRIVATE
#    "vendor/antlr4/src/antlr4-runtime.h"
#    "src/generated/VmLexer.h"
#    "src/generated/VmParser.h"
#    "src/generated/VmBaseVisitor.h"
#)

target_compile_definitions(vm PRIVATE FMT_HEADER_ONLY)

# ---- 编译选项（严格模式，完全保留） ----
target_compile_options(vm PRIVATE
    -std=c++26
    -O3
    -m64
    -ffast-math
    -fmodules-ts
    -fdiagnostics-color=always
    -Wall -Wextra
    -Wno-attributes
    -Wdeprecated-declarations
    -Werror=return-type
    -Werror=uninitialized
    -Werror=maybe-uninitialized
    -Werror=missing-field-initializers
    -Werror=parentheses
    -Werror=switch
    -Werror=switch-default
    -Werror=switch-enum
    -Werror=sign-compare
    -Werror=sign-conversion
    -Werror=conversion
    -Werror=cast-align=strict
    -Werror=cast-qual
    -Werror=float-equal
    -Werror=double-promotion
    -Werror=format=2
    -Werror=format-overflow=2
    -Werror=format-truncation=2
    -Werror=sizeof-pointer-memaccess
    -Werror=stringop-truncation
    -Werror=implicit-fallthrough
    -Werror=shadow
    -Werror=logical-op
    -Werror=unused-variable
    -Werror=unused-function
    -Werror=unused-value
    -Werror=unused-parameter
    -Wunused-label
    -Werror=duplicated-cond
    -Werror=duplicated-branches
    -Werror=undef
    -Wnull-dereference
    -Wtrampolines
    -Wctor-dtor-privacy
    -Wsign-promo
    -Wpedantic
    -Werror=overloaded-virtual
    -Werror=non-virtual-dtor
    -Werror=suggest-override
    -Werror=old-style-cast
    -Werror=useless-cast
    -Werror=noexcept
    -Werror=alloc-zero
    -fopenmp
    -DDEBUG
)

# ---- 链接库搜索路径 ----
target_link_directories(vm PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor/data/lib
    ${CMAKE_SOURCE_DIR}/vendor/fmt/lib
)

if (USE_ANTLR4)
    # ---- 链接库（顺序重要） ----
    target_link_libraries(vm PRIVATE
        antlr_generated          # 生成的解析器代码
        antlr4_runtime_manual    # 手动编译的 ANTLR4 运行时（替代 antlr4-runtime）
        data
        stdc++exp
    )

    # ---- 确保生成解析器在编译主程序前完成 ----
    add_dependencies(vm antlr_generate)
else()
    # ---- 链接库（顺序重要） ----
    target_link_libraries(vm PRIVATE
        data
        stdc++exp
    )
endif()