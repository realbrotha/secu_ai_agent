cmake_minimum_required(VERSION 3.14)
project(wu_agent LANGUAGES CXX VERSION 0.1.0)

# --- 언어 표준 (final_plan.md §2) ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- OS/arch 감지 (Phase 0 은 정보 출력만) ---
message(STATUS "wu-agent: system=${CMAKE_SYSTEM_NAME} arch=${CMAKE_SYSTEM_PROCESSOR} build=${CMAKE_BUILD_TYPE}")

# --- 소스 수집 (Phase 3: op 프레임워크 + native op + platform 백엔드) ---
# op 는 파일 드롭인 → glob(CONFIGURE_DEPENDS 로 새 파일 자동 인식).
file(GLOB_RECURSE OP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/op/*.cpp)
# platform/*.cpp 는 각 파일이 #ifdef 로 자기 OS 만 컴파일(비대상은 빈 TU) → 전부 포함.
file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp)
# core: 인터프리터 등
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp)
# llm: 엔진 (llama 미빌드 시 stub 만 컴파일)
file(GLOB LLM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/llm/*.cpp)

add_executable(wu_agent
  src/main.cpp
  ${OP_SOURCES}
  ${PLATFORM_SOURCES}
  ${CORE_SOURCES}
  ${LLM_SOURCES}
)

target_include_directories(wu_agent PRIVATE
  src
  third_party/nlohmann            # json.hpp
)

# --- replxx (REPL line editing, Win/Linux/Mac) ---
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/replxx/CMakeLists.txt)
  set(REPLXX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(REPLXX_BUILD_PACKAGE  OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/replxx EXCLUDE_FROM_ALL)
  target_link_libraries(wu_agent PRIVATE replxx)
  target_compile_definitions(wu_agent PRIVATE WU_HAVE_REPLXX=1 REPLXX_STATIC)
  message(STATUS "wu-agent: replxx 포함 (WU_HAVE_REPLXX)")
else()
  message(STATUS "wu-agent: replxx 미포함 (getline REPL)")
endif()

# --- llama.cpp (Phase 1): 벤더링돼 있으면 빌드/링크 + WU_HAVE_LLAMA ---
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp/CMakeLists.txt)
  set(LLAMA_CURL         OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_TOOLS  OFF CACHE BOOL "" FORCE)
  set(GGML_METAL         OFF CACHE BOOL "" FORCE)   # CPU-only 기본(§2)
  set(GGML_NATIVE        OFF CACHE BOOL "" FORCE)   # 재현/이식성
  set(BUILD_SHARED_LIBS  OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/llama.cpp EXCLUDE_FROM_ALL)
  target_link_libraries(wu_agent PRIVATE llama)
  target_compile_definitions(wu_agent PRIVATE WU_HAVE_LLAMA=1)
  message(STATUS "wu-agent: llama.cpp 포함 (WU_HAVE_LLAMA)")
else()
  message(STATUS "wu-agent: llama.cpp 미포함 (LLM stub)")
endif()

target_compile_definitions(wu_agent PRIVATE WU_AGENT_VERSION="${PROJECT_VERSION}")

# --- 플랫폼 매크로 + OS별 라이브러리 ---
if(WIN32)
  target_compile_definitions(wu_agent PRIVATE WU_OS_WINDOWS=1)
  target_link_libraries(wu_agent PRIVATE iphlpapi ws2_32)
elseif(APPLE)
  target_compile_definitions(wu_agent PRIVATE WU_OS_MACOS=1)   # libproc 은 libSystem 에 포함(추가 링크 불요)
elseif(UNIX)
  target_compile_definitions(wu_agent PRIVATE WU_OS_LINUX=1)
endif()

# 이후 phase 확장 가이드:
#  add_subdirectory(third_party/llama.cpp)          # Phase 1
#  target_link_libraries(wu_agent PRIVATE llama ggml CURL::libcurl ...)
