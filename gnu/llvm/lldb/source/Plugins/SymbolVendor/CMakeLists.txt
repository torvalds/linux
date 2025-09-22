add_subdirectory(ELF)

if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
  add_subdirectory(MacOSX)
endif()

add_subdirectory(PECOFF)
add_subdirectory(wasm)
