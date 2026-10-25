cmake_minimum_required(VERSION 3.20.0)

# NCS 3.4.1 needs Seeed's external board definitions for this XIAO.
# Board roots must be added before find_package(Zephyr).
list(APPEND BOARD_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/platform-seeedboards/zephyr/nrf"
)

# NCS 3.4.1's nrf_security caches these strings without Kconfig quotes.
# On reconfigure, Zephyr mistakes them for command-line Kconfig overrides.
# Discard those malformed entries; Kconfig reloads the configured values.
# Preserve correctly quoted overrides supplied explicitly by the user.
foreach(symbol IN ITEMS
    CONFIG_MBEDTLS_CONFIG_FILE
    CONFIG_TF_PSA_CRYPTO_CONFIG_FILE
    CONFIG_TF_PSA_CRYPTO_USER_CONFIG_FILE
)
    if(DEFINED CACHE{${symbol}} AND NOT "$CACHE{${symbol}}" MATCHES "^\".*\"$")
        unset(${symbol} CACHE)
    endif()
endforeach()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xiao_wireless_benchmark)

target_include_directories(app PRIVATE include)
target_sources(app PRIVATE
    src/main.c
    src/esb_transport.c
    src/link_state_machine.c
    src/benchmark.c
    src/status_telemetry.c
)
# Treat application warnings as errors without imposing this on the Nordic SDK.
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
