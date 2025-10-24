cmake_minimum_required(VERSION 3.16)
project(dbms_integration LANGUAGES CXX)

add_executable(storage_load main_storage_load.cpp)
target_compile_features(storage_load PRIVATE cxx_std_17)
target_include_directories(storage_load PRIVATE
  ${CMAKE_SOURCE_DIR}/Storage/include
  ${CMAKE_SOURCE_DIR}/Storage           # 仅用于访问 internal（示例里用到 CLOCK 实现）
)
target_link_libraries(storage_load PRIVATE DBMS::storage)

# 开些有用的告警
target_compile_options(storage_load PRIVATE -Wall -Wextra -Wpedantic)
