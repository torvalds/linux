# Check the GoogleTest format support command wrappers.

# RUN: %{lit} -v %{inputs}/googletest-cmd-wrapper | FileCheck %s

# CHECK: -- Testing:
# CHECK-NEXT: PASS: googletest-cmd-wrapper :: DummySubDir/OneTest.exe/0/1 (1 of 1)
# CHECK: Passed: 1
