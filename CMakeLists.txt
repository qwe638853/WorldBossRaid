cmake_minimum_required(VERSION 3.10)
project(WorldBossRaid C)

# 設定 C 標準
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 編譯選項
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")

# 包含目錄
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/server
    ${CMAKE_SOURCE_DIR}/src/client
)

# ============================================
# Server 可執行檔
# ============================================
set(SERVER_SOURCES
    src/server/server.c
    src/server/logic/client_handler.c
    src/server/logic/gamestate.c
    src/server/logic/dice.c
    src/server/security/replay_protection.c
    src/common/tls.c
    src/common/log.c
)

add_executable(server ${SERVER_SOURCES})

# Server 依賴：OpenSSL 和 pthread
find_package(Threads REQUIRED)

# 嘗試使用 CMake 的 FindOpenSSL 模組
find_package(OpenSSL QUIET)

# 如果找不到，嘗試使用 pkg-config
if(NOT OpenSSL_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(OPENSSL openssl)
        if(OPENSSL_FOUND)
            message(STATUS "Found OpenSSL via pkg-config")
            set(OPENSSL_INCLUDE_DIR ${OPENSSL_INCLUDE_DIRS})
            set(OPENSSL_LIBRARIES ${OPENSSL_LIBRARIES})
        endif()
    endif()
endif()

# 如果還是找不到，嘗試手動尋找
if(NOT OpenSSL_FOUND AND NOT OPENSSL_FOUND)
    find_path(OPENSSL_INCLUDE_DIR
        NAMES openssl/ssl.h
        PATHS /usr/include /usr/local/include
    )
    
    find_library(OPENSSL_SSL_LIBRARY
        NAMES ssl libssl
        PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib
    )
    
    find_library(OPENSSL_CRYPTO_LIBRARY
        NAMES crypto libcrypto
        PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib
    )
    
    if(OPENSSL_INCLUDE_DIR AND OPENSSL_SSL_LIBRARY AND OPENSSL_CRYPTO_LIBRARY)
        message(STATUS "Found OpenSSL manually")
        set(OPENSSL_FOUND TRUE)
        set(OPENSSL_LIBRARIES ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})
    endif()
endif()

# 統一設定 OPENSSL_FOUND 變數（用於後續判斷）
if(OpenSSL_FOUND)
    set(OPENSSL_FOUND TRUE)
endif()

# 如果還是找不到，給出明確的錯誤訊息
# 檢查 OpenSSL_FOUND (CMake) 或 OPENSSL_FOUND (pkg-config/手動)
if(NOT OpenSSL_FOUND)
    message(FATAL_ERROR 
        "OpenSSL not found!\n"
        "Please install OpenSSL development package:\n"
        "  Ubuntu/Debian: sudo apt-get install libssl-dev\n"
        "  CentOS/RHEL:   sudo yum install openssl-devel\n"
        "  Fedora:        sudo dnf install openssl-devel"
    )
endif()

# 設定包含目錄和連結庫
if(OpenSSL_FOUND)
    # 使用現代 CMake 目標
    target_link_libraries(server
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
    )
else()
    # 使用傳統方式
    target_include_directories(server PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(server
        ${OPENSSL_LIBRARIES}
        Threads::Threads
    )
endif()

# ============================================
# Client 可執行檔
# ============================================
set(CLIENT_SOURCES
    src/client/client.c
    src/client/ui/client_ui.c
    src/client/ui/login.c
    src/client/ui/bonus.c
    src/client/ui/end.c
    src/common/tls.c
    src/common/log.c
)

add_executable(client ${CLIENT_SOURCES})

# Client 也需要 OpenSSL、pthread 以及 ncurses（因為使用 TLS + threads + TUI）
find_package(Curses REQUIRED)

if(OpenSSL_FOUND)
    # 使用現代 CMake 目標
    target_link_libraries(client
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        ${CURSES_LIBRARIES}
    )
else()
    # 使用傳統方式
    target_include_directories(client PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(client
        ${OPENSSL_LIBRARIES}
        Threads::Threads
        ${CURSES_LIBRARIES}
    )
endif()

# ============================================
# 安裝規則（可選）
# ============================================
# install(TARGETS server client
#     RUNTIME DESTINATION bin
# )

# ============================================
# 輸出訊息
# ============================================
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "OpenSSL found: ${OPENSSL_FOUND}")
message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")

