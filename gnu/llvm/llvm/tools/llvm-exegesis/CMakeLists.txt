# Has side effect of defining LLVM_EXEGESIS_TARGETS
add_subdirectory(lib)

set(LLVM_LINK_COMPONENTS
  CodeGenTypes
  MC
  MCParser
  Support
  TargetParser
  )

foreach(t ${LLVM_EXEGESIS_TARGETS})
  string(STRIP ${t} t)
  list(APPEND LLVM_LINK_COMPONENTS "${t}AsmParser")
  list(APPEND LLVM_LINK_COMPONENTS "${t}CodeGen")
  list(APPEND LLVM_LINK_COMPONENTS "${t}Desc")
  list(APPEND LLVM_LINK_COMPONENTS "${t}Disassembler")
  list(APPEND LLVM_LINK_COMPONENTS "${t}Info")
endforeach()

add_llvm_tool(llvm-exegesis
  DISABLE_LLVM_LINK_LLVM_DYLIB
  llvm-exegesis.cpp

  DEPENDS
  intrinsics_gen
  )

# Link all enabled exegesis targets
set(libs)
foreach(t ${LLVM_EXEGESIS_TARGETS})
  string(STRIP ${t} t)
  list(APPEND libs "LLVMExegesis${t}")
endforeach()

target_link_libraries(llvm-exegesis PRIVATE
  LLVMExegesis
  ${libs}
  )
