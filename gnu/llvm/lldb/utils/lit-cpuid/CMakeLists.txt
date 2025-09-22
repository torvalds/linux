add_lldb_executable(lit-cpuid
  lit-cpuid.cpp

  LINK_COMPONENTS
    Support
    TargetParser
  )

set_target_properties(lit-cpuid PROPERTIES FOLDER "LLDB/Utils")
