set(LLVM_LINK_COMPONENTS
  support
)

add_clang_executable(c-index-test
  c-index-test.c
  core_main.cpp
  )

if(NOT MSVC)
  set_property(
    SOURCE c-index-test.c
    PROPERTY COMPILE_FLAGS "-std=gnu89"
    )
endif()

if (LLVM_BUILD_STATIC)
  target_link_libraries(c-index-test
    PRIVATE
    libclang_static
    clangCodeGen
    clangIndex
  )
else()
  target_link_libraries(c-index-test
    PRIVATE
    libclang
    clangAST
    clangBasic
    clangCodeGen
    clangFrontend
    clangIndex
    clangSerialization
  )
endif()

set_target_properties(c-index-test
  PROPERTIES
  LINKER_LANGUAGE CXX)

# If libxml2 is available, make it available for c-index-test.
if (CLANG_HAVE_LIBXML)
  target_link_libraries(c-index-test PRIVATE LibXml2::LibXml2)
endif()

if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  if(INTERNAL_INSTALL_PREFIX)
    set(INSTALL_DESTINATION "${INTERNAL_INSTALL_PREFIX}/bin")
    set_property(TARGET c-index-test APPEND PROPERTY INSTALL_RPATH
       "@executable_path/../../lib")
  else()
    set(INSTALL_DESTINATION "${CMAKE_INSTALL_BINDIR}")
  endif()

  install(TARGETS c-index-test
    RUNTIME DESTINATION "${INSTALL_DESTINATION}"
    COMPONENT c-index-test)

  if (NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets(install-c-index-test
                             DEPENDS c-index-test
                             COMPONENT c-index-test)
  endif()
endif()
