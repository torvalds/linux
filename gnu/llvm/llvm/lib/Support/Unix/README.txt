llvm/lib/Support/Unix README
===========================

This directory provides implementations of the lib/System classes that
are common to two or more variants of UNIX. For example, the directory
structure underneath this directory could look like this:

Unix           - only code that is truly generic to all UNIX platforms
  Posix        - code that is specific to Posix variants of UNIX
  SUS          - code that is specific to the Single Unix Specification
  SysV         - code that is specific to System V variants of UNIX

As a rule, only those directories actually needing to be created should be
created. Also, further subdirectories could be created to reflect versions of
the various standards. For example, under SUS there could be v1, v2, and v3
subdirectories to reflect the three major versions of SUS.
