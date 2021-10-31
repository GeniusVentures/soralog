if(DEFINED POLLY_COMPILER_CLANG_CMAKE)
    return()
else()
    set(POLLY_COMPILER_CLANG_CMAKE 1)
endif()

# don't override if tool chain set externally, i.e. xcode, IOS, Android where to find these
if(NOT DEFINED CMAKE_C_COMPILER)
find_program(CMAKE_C_COMPILER clang clang-9 clang-8)
if(NOT CMAKE_C_COMPILER)
    fatal_error("clang not found")
endif()
set(
    CMAKE_C_COMPILER
    "${CMAKE_C_COMPILER}"
    CACHE
    STRING
    "C compiler"
    FORCE
)

endif(NOT DEFINED CMAKE_C_COMPILER)

if(NOT DEFINED CMAKE_CXX_COMPILER)
find_program(CMAKE_CXX_COMPILER clang++ clang++-9 clang++-9)

if(NOT CMAKE_CXX_COMPILER)
    fatal_error("clang++ not found")
endif()

message("CXX_COMPILER: "  "${CMAKE_CXX_COMPILER}") 

set(
    CMAKE_CXX_COMPILER
    "${CMAKE_CXX_COMPILER}"
    CACHE
    STRING
    "C++ compiler"
    FORCE
)

endif(NOT DEFINED CMAKE_CXX_COMPILER)



