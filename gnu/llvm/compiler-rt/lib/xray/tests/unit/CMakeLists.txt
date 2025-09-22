set(TEST_SOURCES
  allocator_test.cpp
  buffer_queue_test.cpp
  function_call_trie_test.cpp
  profile_collector_test.cpp
  segmented_array_test.cpp
  test_helpers.cpp
  xray_unit_test_main.cpp
  )

if (NOT COMPILER_RT_STANDALONE_BUILD OR COMPILER_RT_HAS_LLVMTESTINGSUPPORT)
  list(APPEND TEST_SOURCES
    fdr_controller_test.cpp
    fdr_log_writer_test.cpp
    )
endif()

add_xray_unittest(XRayTest SOURCES ${TEST_SOURCES})
