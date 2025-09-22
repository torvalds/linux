install(PROGRAMS hmaptool DESTINATION "${CLANG_TOOLS_INSTALL_DIR}" COMPONENT hmaptool)
add_custom_target(hmaptool ALL DEPENDS "hmaptool")
set_target_properties(hmaptool PROPERTIES FOLDER "Clang/Utils")

if(NOT LLVM_ENABLE_IDE)
  add_llvm_install_targets("install-hmaptool"
                           DEPENDS hmaptool
                           COMPONENT hmaptool)
endif()
