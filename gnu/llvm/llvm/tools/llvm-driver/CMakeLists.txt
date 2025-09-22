get_property(LLVM_COMMON_DEPENDS GLOBAL PROPERTY LLVM_DRIVER_DEPS)
get_property(LLVM_DRIVER_OBJLIBS GLOBAL PROPERTY LLVM_DRIVER_OBJLIBS)

get_property(LLVM_DRIVER_TOOLS GLOBAL PROPERTY LLVM_DRIVER_TOOLS)

list(SORT LLVM_DRIVER_TOOLS)
list(REVERSE LLVM_DRIVER_TOOLS)
foreach(tool ${LLVM_DRIVER_TOOLS})
  string(REPLACE "-" "_" tool_entry ${tool})
  get_property(tool_aliases GLOBAL PROPERTY LLVM_DRIVER_TOOL_ALIASES_${tool})
  foreach(alias ${tool_aliases})
    set_property(GLOBAL APPEND PROPERTY LLVM_DRIVER_TOOL_SYMLINKS ${alias})
    string(REPLACE "llvm-" "" alias ${alias})
    set(def_decl "${def_decl}LLVM_DRIVER_TOOL(\"${alias}\", ${tool_entry})\n")
  endforeach()
  get_property(hidden_tool_aliases GLOBAL PROPERTY LLVM_DRIVER_HIDDEN_TOOL_ALIASES_${tool})
  foreach(alias ${hidden_tool_aliases})
    string(REPLACE "llvm-" "" alias ${alias})
    set(def_decl "${def_decl}LLVM_DRIVER_TOOL(\"${alias}\", ${tool_entry})\n")
  endforeach()
endforeach()

file(WRITE
          "${CMAKE_CURRENT_BINARY_DIR}/LLVMDriverTools.def"
          "${def_decl}#undef LLVM_DRIVER_TOOL\n")

target_include_directories(llvm-driver PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_sources(llvm-driver PRIVATE llvm-driver.cpp)

set_target_properties(llvm-driver PROPERTIES OUTPUT_NAME llvm)

target_link_libraries(llvm-driver PUBLIC ${LLVM_DRIVER_OBJLIBS})
target_link_libraries(llvm-driver PUBLIC LLVMDebuginfod)

if(APPLE)
  # dsymutil uses some CoreFoundation stuff on Darwin...
  target_link_libraries(llvm-driver PRIVATE "-framework CoreFoundation")
endif(APPLE)

macro(generate_driver_tool_targets)
  get_property(LLVM_DRIVER_TOOL_SYMLINKS GLOBAL PROPERTY LLVM_DRIVER_TOOL_SYMLINKS)
  foreach(name ${LLVM_DRIVER_TOOL_SYMLINKS})
    add_llvm_tool_symlink(${name} llvm-driver ALWAYS_GENERATE)
    # Always generate install targets
    llvm_install_symlink(LLVM ${name} llvm-driver ALWAYS_GENERATE)
  endforeach()
endmacro()
