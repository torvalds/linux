set(LLVM_LINK_COMPONENTS
  Object
  Option
  Support
  TargetParser
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)
tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(RcOptsTableGen)

set(LLVM_TARGET_DEFINITIONS WindresOpts.td)
tablegen(LLVM WindresOpts.inc -gen-opt-parser-defs)
add_public_tablegen_target(WindresOptsTableGen)

add_llvm_tool(llvm-rc
  llvm-rc.cpp
  ResourceFileWriter.cpp
  ResourceScriptCppFilter.cpp
  ResourceScriptParser.cpp
  ResourceScriptStmt.cpp
  ResourceScriptToken.cpp
  DEPENDS
  WindresOptsTableGen
  GENERATE_DRIVER
  )

add_llvm_tool_symlink(llvm-windres llvm-rc)

if(LLVM_INSTALL_BINUTILS_SYMLINKS)
  add_llvm_tool_symlink(windres llvm-rc)
endif()
