# FIXME: As we plan to execute llvm-symbolizer binary from compiler-rt
# libraries, it has to be compiled for all supported targets (x86_64, i386 etc).
# This means that we need LLVM libraries to be compiled for these
# targets as well. Currently, there is no support for such a build strategy.

set(LLVM_TARGET_DEFINITIONS Opts.td)
tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(SymbolizerOptsTableGen)

set(LLVM_LINK_COMPONENTS
  Demangle
  Object
  Option
  Support
  Symbolize
  )

add_llvm_tool(llvm-symbolizer
  llvm-symbolizer.cpp

  DEPENDS
  SymbolizerOptsTableGen
  GENERATE_DRIVER
  )

if(NOT LLVM_TOOL_LLVM_DRIVER_BUILD)
  target_link_libraries(llvm-symbolizer PRIVATE LLVMDebuginfod)
endif()

add_llvm_tool_symlink(llvm-addr2line llvm-symbolizer)

if(LLVM_INSTALL_BINUTILS_SYMLINKS)
  add_llvm_tool_symlink(addr2line llvm-symbolizer)
endif()
