option(CLANG_INSTALL_SCANVIEW "Install the scan-view tool" ON)

set(BinFiles
      scan-view)

set(ShareFiles
      ScanView.py
      Reporter.py
      startfile.py
      bugcatcher.ico)

if(CLANG_INSTALL_SCANVIEW)
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
            COMPONENT scan-view)
  endforeach()

  foreach(ShareFile ${ShareFiles})
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/share/scan-view/${ShareFile}
                       COMMAND ${CMAKE_COMMAND} -E make_directory
                         ${CMAKE_BINARY_DIR}/share/scan-view
                       COMMAND ${CMAKE_COMMAND} -E copy
                         ${CMAKE_CURRENT_SOURCE_DIR}/share/${ShareFile}
                         ${CMAKE_BINARY_DIR}/share/scan-view/
                       DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/share/${ShareFile})
    list(APPEND Depends ${CMAKE_BINARY_DIR}/share/scan-view/${ShareFile})
    install(FILES share/${ShareFile}
            DESTINATION "${CMAKE_INSTALL_DATADIR}/scan-view"
            COMPONENT scan-view)
  endforeach()

  add_custom_target(scan-view ALL DEPENDS ${Depends})
  set_target_properties(scan-view PROPERTIES FOLDER "Misc")

  if(NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets("install-scan-view"
                             DEPENDS scan-view
                             COMPONENT scan-view)
  endif()
endif()
