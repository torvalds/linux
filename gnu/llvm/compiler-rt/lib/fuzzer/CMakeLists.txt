set(LIBFUZZER_SOURCES
  FuzzerCrossOver.cpp
  FuzzerDataFlowTrace.cpp
  FuzzerDriver.cpp
  FuzzerExtFunctionsDlsym.cpp
  FuzzerExtFunctionsWeak.cpp
  FuzzerExtFunctionsWindows.cpp
  FuzzerExtraCounters.cpp
  FuzzerExtraCountersDarwin.cpp
  FuzzerExtraCountersWindows.cpp
  FuzzerFork.cpp
  FuzzerIO.cpp
  FuzzerIOPosix.cpp
  FuzzerIOWindows.cpp
  FuzzerLoop.cpp
  FuzzerMerge.cpp
  FuzzerMutate.cpp
  FuzzerSHA1.cpp
  FuzzerTracePC.cpp
  FuzzerUtil.cpp
  FuzzerUtilDarwin.cpp
  FuzzerUtilFuchsia.cpp
  FuzzerUtilLinux.cpp
  FuzzerUtilPosix.cpp
  FuzzerUtilWindows.cpp)

set(LIBFUZZER_HEADERS
  FuzzerBuiltins.h
  FuzzerBuiltinsMsvc.h
  FuzzerCommand.h
  FuzzerCorpus.h
  FuzzerDataFlowTrace.h
  FuzzerDefs.h
  FuzzerDictionary.h
  FuzzerExtFunctions.def
  FuzzerExtFunctions.h
  FuzzerFlags.def
  FuzzerFork.h
  FuzzerIO.h
  FuzzerInterface.h
  FuzzerInternal.h
  FuzzerMerge.h
  FuzzerMutate.h
  FuzzerOptions.h
  FuzzerRandom.h
  FuzzerSHA1.h
  FuzzerTracePC.h
  FuzzerUtil.h
  FuzzerValueBitMap.h)

include_directories(../../include)

CHECK_CXX_SOURCE_COMPILES("
  static thread_local int blah;
  int main() {
  return 0;
  }
  " HAS_THREAD_LOCAL)

set(LIBFUZZER_CFLAGS ${COMPILER_RT_COMMON_CFLAGS})

if(OS_NAME MATCHES "Android|Linux|Fuchsia" AND
   COMPILER_RT_LIBCXX_PATH AND
   COMPILER_RT_LIBCXXABI_PATH)
  list(APPEND LIBFUZZER_CFLAGS -D_LIBCPP_ABI_VERSION=Fuzzer)
  append_list_if(COMPILER_RT_HAS_NOSTDINCXX_FLAG -nostdinc++ LIBFUZZER_CFLAGS)
elseif(TARGET cxx-headers OR HAVE_LIBCXX)
  # libFuzzer uses C++ standard library headers.
  list(APPEND LIBFUZZER_CFLAGS ${COMPILER_RT_CXX_CFLAGS})
  set(LIBFUZZER_DEPS cxx-headers)
endif()

append_list_if(COMPILER_RT_HAS_OMIT_FRAME_POINTER_FLAG -fno-omit-frame-pointer LIBFUZZER_CFLAGS)

if (CMAKE_CXX_FLAGS MATCHES "fsanitize-coverage")
  list(APPEND LIBFUZZER_CFLAGS -fsanitize-coverage=0)
endif()

if(MSVC)
  # Silence warnings by turning off exceptions in MSVC headers and avoid an
  # error by unnecessarily defining thread_local when it isn't even used on
  # Windows.
  list(APPEND LIBFUZZER_CFLAGS -D_HAS_EXCEPTIONS=0)
else()
  if(NOT HAS_THREAD_LOCAL)
    list(APPEND LIBFUZZER_CFLAGS -Dthread_local=__thread)
  endif()
endif()

add_compiler_rt_component(fuzzer)

add_compiler_rt_object_libraries(RTfuzzer
  OS ${FUZZER_SUPPORTED_OS}
  ARCHS ${FUZZER_SUPPORTED_ARCH}
  SOURCES ${LIBFUZZER_SOURCES}
  ADDITIONAL_HEADERS ${LIBFUZZER_HEADERS}
  CFLAGS ${LIBFUZZER_CFLAGS}
  DEPS ${LIBFUZZER_DEPS})

add_compiler_rt_object_libraries(RTfuzzer_main
  OS ${FUZZER_SUPPORTED_OS}
  ARCHS ${FUZZER_SUPPORTED_ARCH}
  SOURCES FuzzerMain.cpp
  CFLAGS ${LIBFUZZER_CFLAGS}
  DEPS ${LIBFUZZER_DEPS})

add_compiler_rt_object_libraries(RTfuzzer_interceptors
  OS ${FUZZER_SUPPORTED_OS}
  ARCHS ${FUZZER_SUPPORTED_ARCH}
  SOURCES FuzzerInterceptors.cpp
  CFLAGS ${LIBFUZZER_CFLAGS}
  DEPS ${LIBFUZZER_DEPS})

add_compiler_rt_runtime(clang_rt.fuzzer
  STATIC
  OS ${FUZZER_SUPPORTED_OS}
  ARCHS ${FUZZER_SUPPORTED_ARCH}
  OBJECT_LIBS RTfuzzer RTfuzzer_main
  CFLAGS ${LIBFUZZER_CFLAGS}
  PARENT_TARGET fuzzer)

add_compiler_rt_runtime(clang_rt.fuzzer_no_main
  STATIC
  OS ${FUZZER_SUPPORTED_OS}
  ARCHS ${FUZZER_SUPPORTED_ARCH}
  OBJECT_LIBS RTfuzzer
  CFLAGS ${LIBFUZZER_CFLAGS}
  PARENT_TARGET fuzzer)

add_compiler_rt_runtime(clang_rt.fuzzer_interceptors
  STATIC
  OS ${FUZZER_SUPPORTED_OS}
  ARCHS ${FUZZER_SUPPORTED_ARCH}
  OBJECT_LIBS RTfuzzer_interceptors
  CFLAGS ${LIBFUZZER_CFLAGS}
  PARENT_TARGET fuzzer)

if(OS_NAME MATCHES "Android|Linux|Fuchsia" AND
   COMPILER_RT_LIBCXX_PATH AND
   COMPILER_RT_LIBCXXABI_PATH)
  macro(partially_link_libcxx name dir arch)
    get_target_flags_for_arch(${arch} target_cflags)
    if(CMAKE_CXX_COMPILER_ID MATCHES Clang)
      get_compiler_rt_target(${arch} target)
      set(target_cflags --target=${target} ${target_cflags})
    endif()
    set(cxx_${arch}_merge_dir "${CMAKE_CURRENT_BINARY_DIR}/cxx_${arch}_merge.dir")
    file(MAKE_DIRECTORY ${cxx_${arch}_merge_dir})
    add_custom_command(TARGET clang_rt.${name}-${arch} POST_BUILD
      COMMAND ${CMAKE_CXX_COMPILER} ${target_cflags} -Wl,--whole-archive "$<TARGET_LINKER_FILE:clang_rt.${name}-${arch}>" -Wl,--no-whole-archive ${dir}/lib/libc++.a -r -o ${name}.o
      COMMAND ${CMAKE_OBJCOPY} --localize-hidden ${name}.o
      COMMAND ${CMAKE_COMMAND} -E remove "$<TARGET_LINKER_FILE:clang_rt.${name}-${arch}>"
      COMMAND ${CMAKE_AR} qcs "$<TARGET_LINKER_FILE:clang_rt.${name}-${arch}>" ${name}.o
      WORKING_DIRECTORY ${cxx_${arch}_merge_dir}
    )
  endmacro()

  foreach(arch ${FUZZER_SUPPORTED_ARCH})
    get_target_flags_for_arch(${arch} TARGET_CFLAGS)
    set(LIBCXX_${arch}_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/libcxx_fuzzer_${arch})
    add_custom_libcxx(libcxx_fuzzer_${arch} ${LIBCXX_${arch}_PREFIX}
      CFLAGS ${TARGET_CFLAGS}
      CMAKE_ARGS -DCMAKE_CXX_COMPILER_WORKS=ON
                 -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                 -DLIBCXXABI_ENABLE_EXCEPTIONS=OFF
                 -DLIBCXX_ABI_NAMESPACE=__Fuzzer
                 -DLIBCXX_ENABLE_EXCEPTIONS=OFF)
    target_compile_options(RTfuzzer.${arch} PRIVATE -isystem ${LIBCXX_${arch}_PREFIX}/include/c++/v1)
    add_dependencies(RTfuzzer.${arch} libcxx_fuzzer_${arch}-build)
    target_compile_options(RTfuzzer_main.${arch} PRIVATE -isystem ${LIBCXX_${arch}_PREFIX}/include/c++/v1)
    add_dependencies(RTfuzzer_main.${arch} libcxx_fuzzer_${arch}-build)
    target_compile_options(RTfuzzer_interceptors.${arch} PRIVATE -isystem ${LIBCXX_${arch}_PREFIX}/include/c++/v1)
    add_dependencies(RTfuzzer_interceptors.${arch} libcxx_fuzzer_${arch}-build)
    partially_link_libcxx(fuzzer_no_main ${LIBCXX_${arch}_PREFIX} ${arch})
    partially_link_libcxx(fuzzer_interceptors ${LIBCXX_${arch}_PREFIX} ${arch})
    partially_link_libcxx(fuzzer ${LIBCXX_${arch}_PREFIX} ${arch})
  endforeach()
endif()

if(COMPILER_RT_INCLUDE_TESTS)
  add_subdirectory(tests)
endif()
