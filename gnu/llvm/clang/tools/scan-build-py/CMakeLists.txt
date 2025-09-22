set (BinFiles
     "analyze-build"
     "intercept-build"
     "scan-build")

set (LibExecs
     "analyze-c++"
     "analyze-cc"
     "intercept-c++"
     "intercept-cc")

set (LibScanbuild
     "__init__.py"
     "analyze.py"
     "arguments.py"
     "clang.py"
     "compilation.py"
     "intercept.py"
     "report.py"
     "shell.py")

set (LibScanbuildResources
     "scanview.css"
     "selectable.js"
     "sorttable.js")

# libear is compiled dynamically in build_libear using the specified cc
# compiler.
set (LibEar
     "__init__.py"
     "config.h.in"
     "ear.c")

foreach(BinFile ${BinFiles})
  if ("${BinFile}" STREQUAL "scan-build")
    # Need to rename scan-build to scan-build-py to prevent overwriting
    # scan-build Perl implementation.
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/bin/scan-build-py
                       COMMAND ${CMAKE_COMMAND} -E make_directory
                         ${CMAKE_BINARY_DIR}/bin
                       COMMAND ${CMAKE_COMMAND} -E copy
                         ${CMAKE_CURRENT_SOURCE_DIR}/bin/scan-build
                         ${CMAKE_BINARY_DIR}/bin/scan-build-py
                       DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bin/scan-build)
    install (PROGRAMS "bin/scan-build"
             DESTINATION "${CMAKE_INSTALL_BINDIR}"
             RENAME scan-build-py
             COMPONENT scan-build-py)
    list(APPEND Depends ${CMAKE_BINARY_DIR}/bin/scan-build-py)
  else()
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/bin/${BinFile}
                       COMMAND ${CMAKE_COMMAND} -E make_directory
                         ${CMAKE_BINARY_DIR}/bin
                       COMMAND ${CMAKE_COMMAND} -E copy
                         ${CMAKE_CURRENT_SOURCE_DIR}/bin/${BinFile}
                         ${CMAKE_BINARY_DIR}/bin/
                       DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bin/${BinFile})
    install(PROGRAMS bin/${BinFile}
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT scan-build-py)
    list(APPEND Depends ${CMAKE_BINARY_DIR}/bin/${BinFile})
  endif()
endforeach()

foreach(lib ${LibExecs})
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/libexec/${lib}
                     COMMAND ${CMAKE_COMMAND} -E make_directory
                       ${CMAKE_BINARY_DIR}/libexec
                     COMMAND ${CMAKE_COMMAND} -E copy
                       ${CMAKE_CURRENT_SOURCE_DIR}/libexec/${lib}
                       ${CMAKE_BINARY_DIR}/libexec/
                     DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/libexec/${lib})
  list(APPEND Depends ${CMAKE_BINARY_DIR}/libexec/${lib})
  install(PROGRAMS libexec/${lib}
          DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}"
          COMPONENT scan-build-py)
endforeach()

foreach(lib ${LibScanbuild})
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/lib/libscanbuild/${lib}
                     COMMAND ${CMAKE_COMMAND} -E make_directory
                       ${CMAKE_BINARY_DIR}/lib
                     COMMAND ${CMAKE_COMMAND} -E make_directory
                       ${CMAKE_BINARY_DIR}/lib/libscanbuild
                     COMMAND ${CMAKE_COMMAND} -E copy
                       ${CMAKE_CURRENT_SOURCE_DIR}/lib/libscanbuild/${lib}
                       ${CMAKE_BINARY_DIR}/lib/libscanbuild/
                     DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/libscanbuild/${lib})
  list(APPEND Depends ${CMAKE_BINARY_DIR}/lib/libscanbuild/${lib})
  install(FILES lib/libscanbuild/${lib}
          DESTINATION lib/libscanbuild
          COMPONENT scan-build-py)
endforeach()

foreach(resource ${LibScanbuildResources})
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/lib/libscanbuild/resources/${resource}
                     COMMAND ${CMAKE_COMMAND} -E make_directory
                       ${CMAKE_BINARY_DIR}/lib
                     COMMAND ${CMAKE_COMMAND} -E make_directory
                       ${CMAKE_BINARY_DIR}/lib/libscanbuild
                     COMMAND ${CMAKE_COMMAND} -E make_directory
                       ${CMAKE_BINARY_DIR}/lib/libscanbuild/resources
                     COMMAND ${CMAKE_COMMAND} -E copy
                       ${CMAKE_CURRENT_SOURCE_DIR}/lib/libscanbuild/resources/${resource}
                       ${CMAKE_BINARY_DIR}/lib/libscanbuild/resources
                     DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/libscanbuild/resources/${resource})
  list(APPEND Depends ${CMAKE_BINARY_DIR}/lib/libscanbuild/resources/${resource})
  install(FILES lib/libscanbuild/resources/${resource}
          DESTINATION lib/libscanbuild/resources
          COMPONENT scan-build-py)
endforeach()

foreach(lib ${LibEar})
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/lib/libear/${lib}
                     COMMAND ${CMAKE_COMMAND} -E make_directory
                       ${CMAKE_BINARY_DIR}/lib
                     COMMAND ${CMAKE_COMMAND} -E make_directory
                       ${CMAKE_BINARY_DIR}/lib/libear
                     COMMAND ${CMAKE_COMMAND} -E copy
                       ${CMAKE_CURRENT_SOURCE_DIR}/lib/libear/${lib}
                       ${CMAKE_BINARY_DIR}/lib/libear/
                     DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/libear/${lib})
  list(APPEND Depends ${CMAKE_BINARY_DIR}/lib/libear/${lib})
  install(FILES lib/libear/${lib}
          DESTINATION lib/libear
          COMPONENT scan-build-py)
endforeach()

add_custom_target(scan-build-py ALL DEPENDS ${Depends})
add_llvm_install_targets("install-scan-build-py"
                         DEPENDS scan-build-py
                         COMPONENT scan-build-py)
