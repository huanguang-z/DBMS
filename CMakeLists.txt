cmake_minimum_required(VERSION 3.16)
project(DBMS CXX)

# C++17 & compile_commands.json
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 默认构建类型
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# 子工程：Storage（会定义 dbms_storage 以及别名 DBMS::storage）
add_subdirectory(Storage)

# ===== 装载程序 =====
add_executable(main_storage_load Integration/main_storage_load.cpp)
target_link_libraries(main_storage_load PRIVATE DBMS::storage)

# main 里用了 Storage/internal/ 头文件，给它加 PRIVATE include
target_include_directories(main_storage_load PRIVATE ${CMAKE_SOURCE_DIR}/Storage)

target_compile_features(main_storage_load PRIVATE cxx_std_17)

# 如需更多子模块：
# add_subdirectory(Index)
# add_subdirectory(Operator)
# add_subdirectory(Recovery)
