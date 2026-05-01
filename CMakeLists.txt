cmake_minimum_required(VERSION 3.15)
project(VoicebotGateway VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_PREFIX_PATH "/opt/homebrew" ${CMAKE_PREFIX_PATH})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_COVERAGE "Enable coverage reporting" OFF)
if(ENABLE_COVERAGE)
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()

# 1. 의존성 패키지 찾기
find_package(PkgConfig REQUIRED)

# PJSIP 찾기
set(PJPROJECT_ROOT "" CACHE PATH
    "Custom PJPROJECT install prefix containing lib/pkgconfig/libpjproject.pc")
set(PJPROJECT_PKGCONFIG_DIR "" CACHE PATH
    "Optional explicit pkg-config directory for libpjproject.pc")

set(_DEFAULT_LOCAL_PJPROJECT_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pjproject/install")
if(NOT PJPROJECT_ROOT AND EXISTS
   "${_DEFAULT_LOCAL_PJPROJECT_ROOT}/lib/pkgconfig/libpjproject.pc")
    set(PJPROJECT_ROOT "${_DEFAULT_LOCAL_PJPROJECT_ROOT}" CACHE PATH
        "Custom PJPROJECT install prefix containing lib/pkgconfig/libpjproject.pc" FORCE)
endif()

if(NOT PJPROJECT_PKGCONFIG_DIR AND PJPROJECT_ROOT)
    set(PJPROJECT_PKGCONFIG_DIR "${PJPROJECT_ROOT}/lib/pkgconfig" CACHE PATH
        "Optional explicit pkg-config directory for libpjproject.pc" FORCE)
endif()

if(PJPROJECT_PKGCONFIG_DIR)
    if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
        set(ENV{PKG_CONFIG_PATH}
            "${PJPROJECT_PKGCONFIG_DIR}:$ENV{PKG_CONFIG_PATH}")
    else()
        set(ENV{PKG_CONFIG_PATH} "${PJPROJECT_PKGCONFIG_DIR}")
    endif()

    if(PJPROJECT_ROOT)
        list(PREPEND CMAKE_PREFIX_PATH "${PJPROJECT_ROOT}")
        message(STATUS "Using custom PJPROJECT_ROOT: ${PJPROJECT_ROOT}")
    endif()
    message(STATUS "Using PJPROJECT pkg-config path: ${PJPROJECT_PKGCONFIG_DIR}")
endif()

pkg_check_modules(PJPROJECT REQUIRED libpjproject)

# Monitoring dependencies
find_package(prometheus-cpp REQUIRED)
find_package(opentelemetry-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)

# ── Unified OpenSSL Enforcement ──
# 모든 라이브러리가 동일한 시스템 OpenSSL을 사용하도록 강제하여 gRPC(BoringSSL)와의 충돌 방지
find_package(OpenSSL REQUIRED)
message(STATUS "Unified OpenSSL found: ${OPENSSL_VERSION} at ${OPENSSL_INCLUDE_DIR}")

# CURL 탐색 (OpenSSL 백엔드 권장)
find_package(CURL REQUIRED)
message(STATUS "CURL found: ${CURL_VERSION_STRING} at ${CURL_LIBRARIES}")

# Boost, gRPC, Protobuf 찾기
find_package(Boost REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

# uuid 찾기 (Linux/macOS 기본 라이브러리)
pkg_check_modules(UUID REQUIRED uuid)

find_library(ONNXRUNTIME_LIB 
 
    NAMES onnxruntime 
    PATHS /opt/homebrew/lib /usr/local/lib /usr/lib /opt/onnxruntime/lib
    REQUIRED)

# ONNX Runtime 헤더 탐색 추가
find_path(ONNXRUNTIME_INCLUDE_DIR 
    NAMES onnxruntime_cxx_api.h 
    PATHS /opt/homebrew/include/onnxruntime /usr/local/include/onnxruntime /usr/include/onnxruntime
    REQUIRED)

find_package(spdlog REQUIRED)
find_library(SPEEXDSP_LIB
    NAMES speexdsp
    PATHS /opt/homebrew/lib /usr/local/lib /usr/lib
    REQUIRED)

find_program(PROTOC_EXECUTABLE protoc)
find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/protos)
set(PROTO_SRC ${PROTO_DIR}/voicebot.proto)

set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

set(PROTO_HDRS ${PROTO_GEN_DIR}/voicebot.pb.h ${PROTO_GEN_DIR}/voicebot.grpc.pb.h)
set(PROTO_SRCS ${PROTO_GEN_DIR}/voicebot.pb.cc ${PROTO_GEN_DIR}/voicebot.grpc.pb.cc)

add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
    COMMAND ${PROTOC_EXECUTABLE}
    ARGS --grpc_out=${PROTO_GEN_DIR}
         --cpp_out=${PROTO_GEN_DIR}
         -I ${PROTO_DIR}
         --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
         ${PROTO_SRC}
    DEPENDS ${PROTO_SRC}
)

# 2. 정적 라이브러리 (테스트 및 메인 실행파일 공유)
add_library(vbgw_lib STATIC
    src/engine/VoicebotEndpoint.cpp
    src/engine/VoicebotAccount.cpp
    src/engine/VoicebotCall.cpp
    src/engine/VoicebotMediaPort.cpp
    src/engine/RoutingEngine.cpp
    src/engine/CapacityManager.cpp
    src/engine/AccountManager.cpp
    src/engine/SessionManager.cpp
    src/ai/VoicebotAiClient.cpp
    src/ai/SileroVad.cpp
    src/ai/SpeexDsp.cpp
    src/ivr/IvrManager.cpp
    src/api/HttpServer.cpp
    src/utils/CdrWebhookClient.cpp
    src/utils/vault_client.cpp
    ${PROTO_SRCS}
    src/monitoring/metrics_exporter.cpp
    src/monitoring/http_exporter.cpp
    src/monitoring/otel_tracer.cpp
)

add_executable(vbgw src/main.cpp)
target_link_libraries(vbgw PRIVATE vbgw_lib)

# 3. 헤더 디렉토리 포함
target_include_directories(vbgw_lib PUBLIC 
    ${PJPROJECT_INCLUDE_DIRS}
    ${UUID_INCLUDE_DIRS}
    src
    ${PROTO_GEN_DIR}
    ${ONNXRUNTIME_INCLUDE_DIR}
    ${SPEEXDSP_INCLUDE_DIR}
)

# OpenSSL 라이브러리 경로 추출 (macOS 등 비표준 경로 대비)
get_filename_component(OPENSSL_LIB_DIR ${OPENSSL_SSL_LIBRARY} DIRECTORY)

# 4. 컴파일 옵션 및 라이브러리 링크
target_compile_options(vbgw_lib PUBLIC ${PJPROJECT_CFLAGS_OTHER})
target_link_directories(vbgw_lib PUBLIC ${PJPROJECT_LIBRARY_DIRS} ${OPENSSL_LIB_DIR})

execute_process(COMMAND pkg-config --static --libs libpjproject
                OUTPUT_VARIABLE PJ_RAW_LDFLAGS
                OUTPUT_STRIP_TRAILING_WHITESPACE)
set_target_properties(vbgw_lib PROPERTIES LINK_FLAGS "${PJ_RAW_LDFLAGS}")
set_target_properties(vbgw PROPERTIES LINK_FLAGS "${PJ_RAW_LDFLAGS}")

target_link_libraries(vbgw_lib PUBLIC 
    ${PJPROJECT_LIBRARIES}
    ${UUID_LIBRARIES}
    Boost::headers
    gRPC::grpc++
    protobuf::libprotobuf
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    redis++_static
    hiredis
    hiredis_ssl
    ${ONNXRUNTIME_LIB}
    ${SPEEXDSP_LIB}
    spdlog::spdlog
    prometheus-cpp::core
    prometheus-cpp::pull
    opentelemetry-cpp::api
    opentelemetry-cpp::sdk
    opentelemetry-cpp::ostream_span_exporter
    nlohmann_json::nlohmann_json
)

# ------------------------------------------------------------------------------
# 5. Testing Framework (GoogleTest) [M-4]
# ------------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  yaml-cpp
  URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.zip
)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

# hiredis (SSL 지원 활성)
FetchContent_Declare(
  hiredis
  URL https://github.com/redis/hiredis/archive/refs/tags/v1.2.0.zip
)
set(ENABLE_SSL ON CACHE BOOL "" FORCE)
set(HIREDIS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(HIREDIS_MASTER_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(hiredis)
FetchContent_GetProperties(hiredis)

# redis-plus-plus가 #include <hiredis/hiredis.h>를 강하게 원함.
# hiredis 소스 루트에 hiredis/ 서브디렉토리가 없으므로 프록시를 만듦.
set(HIREDIS_PROXY_DIR ${CMAKE_BINARY_DIR}/hiredis_proxy)
file(MAKE_DIRECTORY ${HIREDIS_PROXY_DIR}/hiredis)
file(GLOB HIREDIS_HEADERS "${hiredis_SOURCE_DIR}/*.h")
foreach(header ${HIREDIS_HEADERS})
    get_filename_component(header_name ${header} NAME)
    configure_file(${header} ${HIREDIS_PROXY_DIR}/hiredis/${header_name} COPYONLY)
endforeach()

# redis-plus-plus (SSL 지원 활성)
FetchContent_Declare(
  redis-plus-plus
  URL https://github.com/sewenew/redis-plus-plus/archive/refs/tags/1.3.10.zip
)
set(REDIS_PLUS_PLUS_USE_TLS ON CACHE BOOL "" FORCE)
set(REDIS_PLUS_PLUS_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(REDIS_PLUS_PLUS_BUILD_SHARED OFF CACHE BOOL "" FORCE)

# HINT 제공 (가장 중요)
set(HIREDIS_HEADER ${HIREDIS_PROXY_DIR} CACHE PATH "Hiredis proxy path" FORCE)
# 전체 빌드에 프록시 경로 주입
include_directories(${HIREDIS_PROXY_DIR})

FetchContent_MakeAvailable(googletest yaml-cpp redis-plus-plus)

enable_testing()
add_executable(test_vbgw
    tests/test_ringbuffer.cpp
    tests/test_runtime_metrics.cpp
    tests/cpp/test_session_manager.cpp
)
set_target_properties(test_vbgw PROPERTIES LINK_FLAGS "${PJ_RAW_LDFLAGS}")
target_link_libraries(test_vbgw PRIVATE vbgw_lib GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(test_vbgw DISCOVERY_TIMEOUT 30)

add_executable(verify_webhook tests/verify_webhook.cpp)
target_link_libraries(verify_webhook PRIVATE vbgw_lib)

add_executable(verify_redis tests/verify_redis.cpp)
target_link_libraries(verify_redis PRIVATE vbgw_lib)

# 6. yaml-cpp 링크 추가
target_link_libraries(vbgw_lib PUBLIC yaml-cpp)
