set (CMAKE_CXX_STANDARD 23)
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set (SANITIZING_FLAGS)
else ()
  set (SANITIZING_FLAGS -fno-sanitize-recover=all -fsanitize=undefined -fsanitize=address)
endif ()

# Ask for more warnings from the compiler. Keep compiler-specific flags out of
# the other toolchains: MSVC treats GCC warning options as invalid switches.
set (CMAKE_BASE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX /permissive- /DHAVE_WRAP32 /DHAVE_TCP_SENDER_MESSAGE")
else ()
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wpedantic -Wextra -Weffc++ -Werror -Wshadow -Wpointer-arith -Wcast-qual -Wformat=2 -Wno-unqualified-std-cast-call -Wno-non-virtual-dtor -DHAVE_WRAP32 -DHAVE_TCP_SENDER_MESSAGE")
endif ()

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set (MINNOW_OPTIMIZATION_FLAGS /O2 /DNDEBUG)
else ()
  set (MINNOW_OPTIMIZATION_FLAGS -O2 -DNDEBUG)
endif ()
