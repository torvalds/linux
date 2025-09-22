option(CLANG_INSTALL_SCANBUILD "Install the scan-build tool" ON)

include(GNUInstallDirs)

if (WIN32 AND NOT CYGWIN)
  set(BinFiles
        scan-build
        scan-build.bat
	)
  set(LibexecFiles
        ccc-analyzer
        c++-analyzer
        ccc-analyzer.bat
        c++-analyzer.bat
	)
else()
  set(BinFiles
        scan-build
	)
  set(LibexecFiles
        ccc-analyzer
        c++-analyzer
	)
  if (APPLE)
    list(APPEND BinFiles
           set-xcode-analyzer
	   )
  endif()
endif()

set(ManPages
      scan-build.1)

set(ShareFiles
      scanview.css
      sorttable.js)


if(CLANG_INSTALL_SCANBUILD)
  foreach(BinFile ${BinFiles})
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/bin/${BinFile}
                       COMMAND ${CMAKE_COMMAND} -E make_directory
                         ${CMAKE_BINARY_DIR}/bin
                       COMMAND ${CMAKE_COMMAND} -E copy
                         ${CMAKE_CURRENT_SOURCE_DIR}/bin/${BinFile}
                         ${CMAKE_BINARY_DIR}/bin/
                       DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bin/${BinFile})
    list(APPEND Depends ${CMAKE_BINARY_DIR}/bin/${BinFile})
    install(PROGRAMS bin/${BinFile}
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT scan-build)
  endforeach()

  foreach(LibexecFile ${LibexecFiles})
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/libexec/${LibexecFile}
                       COMMAND ${CMAKE_COMMAND} -E make_directory
                         ${CMAKE_BINARY_DIR}/libexec
                       COMMAND ${CMAKE_COMMAND} -E copy
                         ${CMAKE_CURRENT_SOURCE_DIR}/libexec/${LibexecFile}
                         ${CMAKE_BINARY_DIR}/libexec/
                       DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/libexec/${LibexecFile})
    list(APPEND Depends ${CMAKE_BINARY_DIR}/libexec/${LibexecFile})
    install(PROGRAMS libexec/${LibexecFile}
            DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}"
            COMPONENT scan-build)
  endforeach()

  foreach(ManPage ${ManPages})
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_MANDIR}/man1/${ManPage}"
                       COMMAND ${CMAKE_COMMAND} -E make_directory
                         "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_MANDIR}/man1"
                       COMMAND ${CMAKE_COMMAND} -E copy
                         "${CMAKE_CURRENT_SOURCE_DIR}/man/${ManPage}"
                         "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_MANDIR}/man1/"
                       DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/man/${ManPage})
    list(APPEND Depends "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_MANDIR}/man1/${ManPage}")
    install(FILES man/${ManPage}
            DESTINATION "${CMAKE_INSTALL_MANDIR}/man1"
            COMPONENT scan-build)
  endforeach()

  foreach(ShareFile ${ShareFiles})
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/share/scan-build/${ShareFile}
                       COMMAND ${CMAKE_COMMAND} -E make_directory
                         ${CMAKE_BINARY_DIR}/share/scan-build
                       COMMAND ${CMAKE_COMMAND} -E copy
                         ${CMAKE_CURRENT_SOURCE_DIR}/share/scan-build/${ShareFile}
                         ${CMAKE_BINARY_DIR}/share/scan-build/
                       DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/share/scan-build/${ShareFile})
    list(APPEND Depends ${CMAKE_BINARY_DIR}/share/scan-build/${ShareFile})
    install(FILES share/scan-build/${ShareFile}
            DESTINATION "${CMAKE_INSTALL_DATADIR}/scan-build"
            COMPONENT scan-build)
  endforeach()

  add_custom_target(scan-build ALL DEPENDS ${Depends})
  set_target_properties(scan-build PROPERTIES FOLDER "Misc")

  if(NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets("install-scan-build"
                             DEPENDS scan-build
                             COMPONENT scan-build)
  endif()
endif()
