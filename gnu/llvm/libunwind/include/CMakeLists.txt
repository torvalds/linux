set(files
    __libunwind_config.h
    libunwind.h
    libunwind.modulemap
    mach-o/compact_unwind_encoding.h
    unwind_arm_ehabi.h
    unwind_itanium.h
    unwind.h
    )

add_library(unwind-headers INTERFACE)
target_include_directories(unwind-headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if(LIBUNWIND_INSTALL_HEADERS)
  foreach(file ${files})
    get_filename_component(dir ${file} DIRECTORY)
    install(FILES ${file}
      DESTINATION "${LIBUNWIND_INSTALL_INCLUDE_DIR}/${dir}"
      COMPONENT unwind-headers
      PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
    )
  endforeach()

  if(NOT CMAKE_CONFIGURATION_TYPES)
    add_custom_target(install-unwind-headers
      DEPENDS unwind-headers
      COMMAND "${CMAKE_COMMAND}"
              -DCMAKE_INSTALL_COMPONENT=unwind-headers
              -P "${LIBUNWIND_BINARY_DIR}/cmake_install.cmake")
    add_custom_target(install-unwind-headers-stripped DEPENDS install-unwind-headers)
  endif()
endif()
