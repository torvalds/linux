add_kaleidoscope_chapter(Kaleidoscope-Ch2
  toy.cpp
  )

if(LLVM_COMPILER_IS_GCC_COMPATIBLE)
  target_compile_options(Kaleidoscope-Ch2 PRIVATE
    -Wno-unused-private-field
    )
endif()

if(MSVC)
  # ignore "warning LNK4199: /DELAYLOAD:shell32.dll ignored; no imports found from shell32.dll"
  target_link_libraries(Kaleidoscope-Ch2 PRIVATE "-ignore:4199")
endif()
