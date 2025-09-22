# The headers of Table 24: C++ library headers [tab:headers.cpp]
# and the headers of Table 25: C++ headers for C library facilities [tab:headers.cpp.c]
set(LIBCXX_MODULE_STD_SOURCES
  std/algorithm.inc
  std/any.inc
  std/array.inc
  std/atomic.inc
  std/barrier.inc
  std/bit.inc
  std/bitset.inc
  std/cassert.inc
  std/cctype.inc
  std/cerrno.inc
  std/cfenv.inc
  std/cfloat.inc
  std/charconv.inc
  std/chrono.inc
  std/cinttypes.inc
  std/climits.inc
  std/clocale.inc
  std/cmath.inc
  std/codecvt.inc
  std/compare.inc
  std/complex.inc
  std/concepts.inc
  std/condition_variable.inc
  std/coroutine.inc
  std/csetjmp.inc
  std/csignal.inc
  std/cstdarg.inc
  std/cstddef.inc
  std/cstdint.inc
  std/cstdio.inc
  std/cstdlib.inc
  std/cstring.inc
  std/ctime.inc
  std/cuchar.inc
  std/cwchar.inc
  std/cwctype.inc
  std/deque.inc
  std/exception.inc
  std/execution.inc
  std/expected.inc
  std/filesystem.inc
  std/flat_map.inc
  std/flat_set.inc
  std/format.inc
  std/forward_list.inc
  std/fstream.inc
  std/functional.inc
  std/future.inc
  std/generator.inc
  std/hazard_pointer.inc
  std/initializer_list.inc
  std/iomanip.inc
  std/ios.inc
  std/iosfwd.inc
  std/iostream.inc
  std/istream.inc
  std/iterator.inc
  std/latch.inc
  std/limits.inc
  std/list.inc
  std/locale.inc
  std/map.inc
  std/mdspan.inc
  std/memory.inc
  std/memory_resource.inc
  std/mutex.inc
  std/new.inc
  std/numbers.inc
  std/numeric.inc
  std/optional.inc
  std/ostream.inc
  std/print.inc
  std/queue.inc
  std/random.inc
  std/ranges.inc
  std/ratio.inc
  std/rcu.inc
  std/regex.inc
  std/scoped_allocator.inc
  std/semaphore.inc
  std/set.inc
  std/shared_mutex.inc
  std/source_location.inc
  std/span.inc
  std/spanstream.inc
  std/sstream.inc
  std/stack.inc
  std/stacktrace.inc
  std/stdexcept.inc
  std/stdfloat.inc
  std/stop_token.inc
  std/streambuf.inc
  std/string.inc
  std/string_view.inc
  std/strstream.inc
  std/syncstream.inc
  std/system_error.inc
  std/text_encoding.inc
  std/thread.inc
  std/tuple.inc
  std/type_traits.inc
  std/typeindex.inc
  std/typeinfo.inc
  std/unordered_map.inc
  std/unordered_set.inc
  std/utility.inc
  std/valarray.inc
  std/variant.inc
  std/vector.inc
  std/version.inc
)

set(LIBCXX_MODULE_STD_COMPAT_SOURCES
  std.compat/cassert.inc
  std.compat/cctype.inc
  std.compat/cerrno.inc
  std.compat/cfenv.inc
  std.compat/cfloat.inc
  std.compat/cinttypes.inc
  std.compat/climits.inc
  std.compat/clocale.inc
  std.compat/cmath.inc
  std.compat/csetjmp.inc
  std.compat/csignal.inc
  std.compat/cstdarg.inc
  std.compat/cstddef.inc
  std.compat/cstdint.inc
  std.compat/cstdio.inc
  std.compat/cstdlib.inc
  std.compat/cstring.inc
  std.compat/ctime.inc
  std.compat/cuchar.inc
  std.compat/cwchar.inc
  std.compat/cwctype.inc
)

# TODO MODULES the CMakeLists.txt in the build directory is only temporary.
# This allows using as available in the build directory. Once build systems
# have proper support for the installed files this will be removed.
if ("${LIBCXX_GENERATED_INCLUDE_DIR}" STREQUAL "${LIBCXX_GENERATED_INCLUDE_TARGET_DIR}")
  # This typically happens when the target is not installed.
  set(LIBCXX_CONFIGURED_INCLUDE_DIRS "${LIBCXX_GENERATED_INCLUDE_DIR}")
else()
  # It's important that the arch directory be included first so that its header files
  # which interpose on the default include dir be included instead of the default ones.
  set(LIBCXX_CONFIGURED_INCLUDE_DIRS
    "${LIBCXX_GENERATED_INCLUDE_TARGET_DIR};${LIBCXX_GENERATED_INCLUDE_DIR}"
  )
endif()
configure_file(
  "CMakeLists.txt.in"
  "${LIBCXX_GENERATED_MODULE_DIR}/CMakeLists.txt"
  @ONLY
)

set(LIBCXX_MODULE_STD_INCLUDE_SOURCES)
foreach(file ${LIBCXX_MODULE_STD_SOURCES})
  set(
    LIBCXX_MODULE_STD_INCLUDE_SOURCES
    "${LIBCXX_MODULE_STD_INCLUDE_SOURCES}#include \"${file}\"\n"
  )
endforeach()

configure_file(
  "std.cppm.in"
  "${LIBCXX_GENERATED_MODULE_DIR}/std.cppm"
  @ONLY
)

set(LIBCXX_MODULE_STD_COMPAT_INCLUDE_SOURCES)
foreach(file ${LIBCXX_MODULE_STD_COMPAT_SOURCES})
  set(
    LIBCXX_MODULE_STD_COMPAT_INCLUDE_SOURCES
    "${LIBCXX_MODULE_STD_COMPAT_INCLUDE_SOURCES}#include \"${file}\"\n"
  )
endforeach()

configure_file(
  "std.compat.cppm.in"
  "${LIBCXX_GENERATED_MODULE_DIR}/std.compat.cppm"
  @ONLY
)

set(_all_modules)
list(APPEND _all_modules "${LIBCXX_GENERATED_MODULE_DIR}/CMakeLists.txt")
list(APPEND _all_modules "${LIBCXX_GENERATED_MODULE_DIR}/std.cppm")
list(APPEND _all_modules "${LIBCXX_GENERATED_MODULE_DIR}/std.compat.cppm")
foreach(file ${LIBCXX_MODULE_STD_SOURCES} ${LIBCXX_MODULE_STD_COMPAT_SOURCES})
  set(src "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
  set(dst "${LIBCXX_GENERATED_MODULE_DIR}/${file}")
  add_custom_command(OUTPUT ${dst}
    DEPENDS ${src}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${src} ${dst}
    COMMENT "Copying CXX module ${file}")
  list(APPEND _all_modules "${dst}")
endforeach()

add_custom_target(generate-cxx-modules
  ALL DEPENDS
    ${_all_modules}
)
add_dependencies(cxx-test-depends generate-cxx-modules)

# Configure the modules manifest.
# Use the relative path between the installation and the module in the json
# file. This allows moving the entire installation to a different location.
if("${CMAKE_INSTALL_PREFIX}" STREQUAL "")
  set(BASE_DIRECTORY "/")
else()
  set(BASE_DIRECTORY ${CMAKE_INSTALL_PREFIX})
endif()
cmake_path(ABSOLUTE_PATH LIBCXX_INSTALL_LIBRARY_DIR
  BASE_DIRECTORY ${BASE_DIRECTORY}
  OUTPUT_VARIABLE ABS_LIBRARY_DIR)
cmake_path(ABSOLUTE_PATH LIBCXX_INSTALL_MODULES_DIR
  BASE_DIRECTORY ${BASE_DIRECTORY}
  OUTPUT_VARIABLE ABS_MODULES_DIR)
file(RELATIVE_PATH LIBCXX_MODULE_RELATIVE_PATH
  ${ABS_LIBRARY_DIR}
  ${ABS_MODULES_DIR})
configure_file(
  "modules.json.in"
  "${LIBCXX_LIBRARY_DIR}/libc++.modules.json"
  @ONLY
)

# Dummy library to make modules an installation component.
add_library(cxx-modules INTERFACE)
add_dependencies(cxx-modules generate-cxx-modules)

if (LIBCXX_INSTALL_MODULES)
  foreach(file ${LIBCXX_MODULE_STD_SOURCES} ${LIBCXX_MODULE_STD_COMPAT_SOURCES})
    get_filename_component(dir ${file} DIRECTORY)
    install(FILES ${file}
      DESTINATION "${LIBCXX_INSTALL_MODULES_DIR}/${dir}"
      COMPONENT cxx-modules
      PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
    )
  endforeach()

  # Install the generated module files.
  install(FILES
      "${LIBCXX_GENERATED_MODULE_DIR}/std.cppm"
      "${LIBCXX_GENERATED_MODULE_DIR}/std.compat.cppm"
    DESTINATION "${LIBCXX_INSTALL_MODULES_DIR}"
    COMPONENT cxx-modules
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
  )

  # Install the module manifest.
  install(FILES
      "${LIBCXX_LIBRARY_DIR}/libc++.modules.json"
    DESTINATION "${LIBCXX_INSTALL_LIBRARY_DIR}"
    COMPONENT cxx-modules
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
  )

  if (NOT CMAKE_CONFIGURATION_TYPES)
    add_custom_target(install-cxx-modules
                      DEPENDS cxx-modules
                      COMMAND "${CMAKE_COMMAND}"
                              -DCMAKE_INSTALL_COMPONENT=cxx-modules
                              -P "${CMAKE_BINARY_DIR}/cmake_install.cmake")
    # Stripping is a no-op for modules
    add_custom_target(install-cxx-modules-stripped DEPENDS install-cxx-modules)
  endif()
endif()
