cmake_minimum_required(VERSION 3.20)
project(ChessClaw VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Aggressive but portable optimization. march=native is opt-in (CHESSCLAW_NATIVE=ON)
# because it breaks portability of binaries across machines.
option(CHESSCLAW_NATIVE "Compile with -march=native (non-portable, fastest on local CPU)" OFF)
option(CHESSCLAW_LTO "Enable link-time optimization" ON)

add_library(chessclaw_core
  src/types.cpp
  src/bitboard.cpp
  src/position.cpp
  src/movegen.cpp
  src/evaluate.cpp
  src/nnue.cpp
  src/tt.cpp
  src/search.cpp
  src/uci.cpp
  src/cli.cpp
  src/train.cpp
)
target_include_directories(chessclaw_core PUBLIC include)

if(MSVC)
  target_compile_options(chessclaw_core PUBLIC /O2 /EHsc)
else()
  target_compile_options(chessclaw_core PUBLIC -O3 -funroll-loops -Wall -Wextra)
  if(CHESSCLAW_NATIVE)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-march=native" HAS_MARCH_NATIVE)
    if(HAS_MARCH_NATIVE)
      target_compile_options(chessclaw_core PUBLIC -march=native)
    endif()
  endif()
  # SIMD: rely on compiler auto-vectorization + explicit paths guarded by __AVX2__ etc.
  # No manual -mavx2 forcing here so Termux/ARM/macOS-ARM builds stay portable.
endif()

if(CHESSCLAW_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
  if(_ipo_ok)
    set_property(TARGET chessclaw_core PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

find_package(Threads REQUIRED)
target_link_libraries(chessclaw_core PUBLIC Threads::Threads)

add_executable(chessclaw src/main.cpp)
target_link_libraries(chessclaw PRIVATE chessclaw_core)
if(CHESSCLAW_LTO AND _ipo_ok)
  set_property(TARGET chessclaw PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# ---------------- Tests / tools ----------------
enable_testing()
add_executable(chessclaw_perft tools/perft.cpp)
target_link_libraries(chessclaw_perft PRIVATE chessclaw_core)
add_test(NAME perft_startpos COMMAND chessclaw_perft)
