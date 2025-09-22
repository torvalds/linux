# Link a shared library with COMPILER_RT_TEST_COMPILER.
# clang_link_shared(<output.so>
#                   OBJECTS <list of input objects>
#                   LINK_FLAGS <list of link flags>
#                   DEPS <list of dependencies>)
macro(clang_link_shared so_file)
  cmake_parse_arguments(SOURCE "" "" "OBJECTS;LINK_FLAGS;DEPS" ${ARGN})
  if(NOT COMPILER_RT_STANDALONE_BUILD)
    list(APPEND SOURCE_DEPS clang)
  endif()
  add_custom_command(
    OUTPUT ${so_file}
    COMMAND ${COMPILER_RT_TEST_COMPILER} -o "${so_file}" -shared
            ${SOURCE_LINK_FLAGS} ${SOURCE_OBJECTS}
    DEPENDS ${SOURCE_DEPS})
endmacro()
