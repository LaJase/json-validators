cmake_minimum_required(VERSION 3.14)
project(json-validator-cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Max optimisations pour le benchmark
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

include(FetchContent)

# ── nlohmann/json ──────────────────────────────────────────────────────────────
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ── nlohmann/json-schema-validator ────────────────────────────────────────────
FetchContent_Declare(
    json_schema_validator
    GIT_REPOSITORY https://github.com/pboettch/json-schema-validator.git
    GIT_TAG        2.3.0
    GIT_SHALLOW    TRUE
)
set(BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_GetProperties(json_schema_validator)
if(NOT json_schema_validator_POPULATED)
  FetchContent_Populate(json_schema_validator)
  add_subdirectory(
        ${json_schema_validator_SOURCE_DIR}
        ${json_schema_validator_BINARY_DIR}
    )
endif()

add_executable(validator validator.cpp)
target_link_libraries(validator PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)

# ── RapidJSON ──────────────────────────────────────────────────────────────────
FetchContent_Declare(
    rapidjson
    GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(rapidjson)
if(NOT rapidjson_POPULATED)
    FetchContent_Populate(rapidjson)
    set(RAPIDJSON_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(RAPIDJSON_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(RAPIDJSON_BUILD_DOC      OFF CACHE BOOL "" FORCE)
    add_subdirectory(${rapidjson_SOURCE_DIR} ${rapidjson_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# ── valijson ───────────────────────────────────────────────────────────────────
FetchContent_Declare(
    valijson
    GIT_REPOSITORY https://github.com/tristanpenman/valijson.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
set(valijson_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(valijson_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(valijson_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(valijson)

add_executable(validator_rj validator_rapidjson.cpp)
target_compile_definitions(validator_rj PRIVATE RAPIDJSON_HAS_STDSTRING=1)
target_include_directories(validator_rj PRIVATE
    ${rapidjson_SOURCE_DIR}/include
    ${valijson_SOURCE_DIR}/include
)
# Both rapidjson and valijson are header-only — no target_link_libraries needed
