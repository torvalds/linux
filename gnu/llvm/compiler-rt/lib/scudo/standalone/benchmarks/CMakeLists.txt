# To build these benchmarks, build the target "ScudoBenchmarks.$ARCH", where
# $ARCH is the name of the target architecture. For example,
# ScudoBenchmarks.x86_64 for 64-bit x86. The benchmark executable is then
# available under projects/compiler-rt/lib/scudo/standalone/benchmarks/ in the
# build directory.

include(AddLLVM)

set(SCUDO_BENCHMARK_CFLAGS -I${COMPILER_RT_SOURCE_DIR}/lib/scudo/standalone)
if(ANDROID)
  list(APPEND SCUDO_BENCHMARK_CFLAGS -fno-emulated-tls)
endif()
string(REPLACE ";" " " SCUDO_BENCHMARK_CFLAGS " ${SCUDO_BENCHMARK_CFLAGS}")

foreach(arch ${SCUDO_STANDALONE_SUPPORTED_ARCH})
  add_benchmark(ScudoBenchmarks.${arch}
                malloc_benchmark.cpp
                $<TARGET_OBJECTS:RTScudoStandalone.${arch}>)
  set_property(TARGET ScudoBenchmarks.${arch} APPEND_STRING PROPERTY
               COMPILE_FLAGS "${SCUDO_BENCHMARK_CFLAGS}")

  if (COMPILER_RT_HAS_GWP_ASAN)
    add_benchmark(
      ScudoBenchmarksWithGwpAsan.${arch} malloc_benchmark.cpp
      $<TARGET_OBJECTS:RTScudoStandalone.${arch}>
      $<TARGET_OBJECTS:RTGwpAsan.${arch}>
      $<TARGET_OBJECTS:RTGwpAsanBacktraceLibc.${arch}>
      $<TARGET_OBJECTS:RTGwpAsanSegvHandler.${arch}>)
    set_property(
      TARGET ScudoBenchmarksWithGwpAsan.${arch} APPEND_STRING PROPERTY
      COMPILE_FLAGS "${SCUDO_BENCHMARK_CFLAGS} -DGWP_ASAN_HOOKS")
  endif()
endforeach()
