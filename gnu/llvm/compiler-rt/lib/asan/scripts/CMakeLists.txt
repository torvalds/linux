if(ANDROID)
  add_compiler_rt_script(asan_device_setup)
  add_dependencies(asan asan_device_setup)
endif()
