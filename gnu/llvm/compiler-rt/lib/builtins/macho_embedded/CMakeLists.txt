file(GLOB filter_files ${CMAKE_CURRENT_SOURCE_DIR}/*.txt)
foreach(filter_file ${filter_files})
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${filter_file})
endforeach()
