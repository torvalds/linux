# Check that we can inject preamble commands at the beginning of a ShTest.
#
# For one case, check the execution trace as these preamble commands have
# "preamble command" instead of the usual "{{RUN}}: at line N".

# RUN: %{lit} %{inputs}/shtest-inject/test-empty.txt --show-all | FileCheck --check-prefix=CHECK-TEST1 %s
#
#       CHECK-TEST1: Command Output (stdout):
#  CHECK-TEST1-NEXT: --
#  CHECK-TEST1-NEXT: # preamble command line
#  CHECK-TEST1-NEXT: echo "THIS WAS"
#  CHECK-TEST1-NEXT: # executed command: echo 'THIS WAS'
#  CHECK-TEST1-NEXT: # .---command stdout{{-*}}
#  CHECK-TEST1-NEXT: # | THIS WAS
#  CHECK-TEST1-NEXT: # `---{{-*}}
#  CHECK-TEST1-NEXT: # preamble command line
#  CHECK-TEST1-NEXT: echo
#  CHECK-TEST1-NEXT: "INJECTED"
#  CHECK-TEST1-NEXT: # executed command: echo INJECTED
#  CHECK-TEST1-NEXT: # .---command stdout{{-*}}
#  CHECK-TEST1-NEXT: # | INJECTED
#  CHECK-TEST1-NEXT: # `---{{-*}}
# CHECK-TEST1-EMPTY:
#  CHECK-TEST1-NEXT: --
#
# CHECK-TEST1: Passed: 1

# RUN: %{lit} %{inputs}/shtest-inject/test-one.txt --show-all | FileCheck --check-prefix=CHECK-TEST2 %s
#
# CHECK-TEST2: THIS WAS
# CHECK-TEST2: INJECTED
# CHECK-TEST2: IN THE FILE
#
# CHECK-TEST2: Passed: 1

# RUN: %{lit} %{inputs}/shtest-inject/test-many.txt --show-all | FileCheck --check-prefix=CHECK-TEST3 %s
#
# CHECK-TEST3: THIS WAS
# CHECK-TEST3: INJECTED
# CHECK-TEST3: IN THE FILE
# CHECK-TEST3: IF IT WORKS
# CHECK-TEST3: AS EXPECTED
#
# CHECK-TEST3: Passed: 1
