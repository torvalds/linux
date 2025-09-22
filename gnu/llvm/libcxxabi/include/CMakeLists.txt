set(files
  __cxxabi_config.h
  cxxabi.h
  )

foreach(f ${files})
  set(src "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
  set(dst "${LIBCXXABI_GENERATED_INCLUDE_DIR}/${f}")
  add_custom_command(OUTPUT ${dst}
    DEPENDS ${src}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${src} ${dst}
    COMMENT "Copying CXXABI header ${f}")
  list(APPEND _all_includes "${dst}")
endforeach()

add_custom_target(generate-cxxabi-headers ALL DEPENDS ${_all_includes})

add_library(cxxabi-headers INTERFACE)
add_dependencies(cxxabi-headers generate-cxxabi-headers)
target_include_directories(cxxabi-headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

if (LIBCXXABI_INSTALL_HEADERS)
  foreach(file ${files})
    get_filename_component(dir ${file} DIRECTORY)
    install(FILES ${file}
      DESTINATION ${LIBCXXABI_INSTALL_INCLUDE_DIR}/${dir}
      COMPONENT cxxabi-headers
      PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
    )
  endforeach()

  add_custom_target(install-cxxabi-headers
                    DEPENDS cxxabi-headers
                    COMMAND "${CMAKE_COMMAND}"
                            -DCMAKE_INSTALL_COMPONENT=cxxabi-headers
                            -P "${CMAKE_BINARY_DIR}/cmake_install.cmake")
  # Stripping is a no-op for headers
  add_custom_target(install-cxxabi-headers-stripped DEPENDS install-cxxabi-headers)
endif()
