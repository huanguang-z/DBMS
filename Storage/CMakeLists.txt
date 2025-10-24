cmake_minimum_required(VERSION 3.16)
project(dbms_storage CXX)

# 可选：启用 LRU-K 替换器
option(DBMS_STORAGE_ENABLE_LRUK "Enable LRU-k replacer" OFF)

# 可选：Sanitizer
option(DBMS_STORAGE_ASAN  "Enable AddressSanitizer" OFF)
option(DBMS_STORAGE_UBSAN "Enable UBSanitizer"      OFF)

add_library(dbms_storage STATIC
  # ---- io ----
  src/io/disk_manager_posix.cc

  # ---- page ----
  src/page/slotted_page.cc

  # ---- buffer ----
  src/buffer/buffer_pool_manager.cc
  src/buffer/page_guard.cc
  src/buffer/page_table.cc
  src/buffer/clock_replacer.cc
  $<$<BOOL:${DBMS_STORAGE_ENABLE_LRUK}>:src/buffer/lruk_replacer.cc>

  # ---- space ----
  src/space/free_space_manager.cc

  # ---- segment ----
  src/segment/segment_manager.cc

  # ---- record ----
  src/record/schema.cc
  src/record/tuple.cc

  # ---- table ----
  src/table/table_heap.cc
  src/table/table_iterator.cc
)

# 公开公共头；并把 Storage 根目录作为 PRIVATE include，供内部源码 include "internal/..."
target_include_directories(dbms_storage
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

# C++17 & 常用告警
target_compile_features(dbms_storage PUBLIC cxx_std_17)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(dbms_storage PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Sanitizer（可选）
if (DBMS_STORAGE_ASAN)
  target_compile_options(dbms_storage PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(dbms_storage PRIVATE -fsanitize=address)
endif()
if (DBMS_STORAGE_UBSAN)
  target_compile_options(dbms_storage PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
  target_link_options(dbms_storage PRIVATE -fsanitize=undefined)
endif()

# 开启 LRU-K 时，向依赖者传递宏（main_storage_load 可见 #ifdef DBMS_STORAGE_ENABLE_LRUK）
if (DBMS_STORAGE_ENABLE_LRUK)
  target_compile_definitions(dbms_storage PUBLIC DBMS_STORAGE_ENABLE_LRUK=1)
endif()

# 统一别名
add_library(DBMS::storage ALIAS dbms_storage)
