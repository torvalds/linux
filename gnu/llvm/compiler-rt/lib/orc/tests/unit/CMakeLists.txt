set(UNITTEST_SOURCES
  adt_test.cpp
  bitmask_enum_test.cpp
  c_api_test.cpp
  endian_test.cpp
  error_test.cpp
  executor_address_test.cpp
  executor_symbol_def_test.cpp
  extensible_rtti_test.cpp
  interval_map_test.cpp
  interval_set_test.cpp
  orc_unit_test_main.cpp
  wrapper_function_utils_test.cpp
  simple_packed_serialization_test.cpp
  string_pool_test.cpp
  )

if (COMPILER_RT_CAN_EXECUTE_TESTS)
  add_orc_unittest(OrcUnitTest SOURCES ${UNITTEST_SOURCES})
endif()
