set(TSAN_RTL_TEST_SOURCES
  tsan_bench.cpp
  tsan_mop.cpp
  tsan_mutex.cpp
  tsan_posix.cpp
  tsan_string.cpp
  tsan_test.cpp
  tsan_thread.cpp
  )

if(UNIX)
  list(APPEND TSAN_RTL_TEST_SOURCES
    tsan_test_util_posix.cpp
    )
endif()

set(TSAN_RTL_TEST_HEADERS
  tsan_test_util.h
  )

add_tsan_unittest(TsanRtlTest
  SOURCES ${TSAN_RTL_TEST_SOURCES}
  HEADERS ${TSAN_RTL_TEST_HEADERS})
