set(LLVM_LINK_COMPONENTS
  AllTargetsAsmParsers
  AllTargetsCodeGens
  AllTargetsDescs
  AllTargetsDisassemblers
  AllTargetsInfos
  BitReader
  Core
  MCDisassembler
  Object
  Support
  Target
  )

# We should only have llvm-c-test use libLLVM if libLLVM is built with the
# default list of components. Using libLLVM with custom components can result in
# build failures.

set (USE_LLVM_DYLIB FALSE)

if (TARGET LLVM)
  set (USE_LLVM_DYLIB TRUE)
  if (DEFINED LLVM_DYLIB_COMPONENTS)
    foreach(c in ${LLVM_LINK_COMPONENTS})
      list(FIND LLVM_DYLIB_COMPONENTS ${c} C_IDX)
      if (C_IDX EQUAL -1)
        set(USE_LLVM_DYLIB FALSE)
        break()
      endif()
    endforeach()
  endif()
endif()

if(USE_LLVM_DYLIB)
  set(LLVM_LINK_COMPONENTS)
endif()

if (LLVM_COMPILER_IS_GCC_COMPATIBLE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Wstrict-prototypes")
endif ()

add_llvm_tool(llvm-c-test
  attributes.c
  calc.c
  debuginfo.c
  diagnostic.c
  disassemble.c
  echo.cpp
  helpers.c
  include-all.c
  main.c
  module.c
  metadata.c
  object.c
  targets.c
  )

if(USE_LLVM_DYLIB)
  target_link_libraries(llvm-c-test LLVM)
endif()
