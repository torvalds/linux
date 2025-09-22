# Check that -a/-v/-vv makes the line number of the failing RUN command clear.


# RUN: not %{lit} -a %{inputs}/shtest-run-at-line | %{filter-lit} | FileCheck %s
# RUN: not %{lit} -v %{inputs}/shtest-run-at-line | %{filter-lit} | FileCheck %s
# RUN: not %{lit} -vv %{inputs}/shtest-run-at-line | %{filter-lit} | FileCheck %s
# END.


# CHECK: Testing: 8 tests


# In the case of the external shell, we check for only RUN lines in stderr in
# case some shell implementations format "set -x" output differently.

# CHECK-LABEL: FAIL: shtest-run-at-line :: external-shell/basic.txt

#       CHECK: Command Output (stderr)
#  CHECK-NEXT: --
#  CHECK-NEXT: {{^}}RUN: at line 4: true{{$}}
#  CHECK-NEXT: true
#  CHECK-NEXT: {{^}}RUN: at line 5: false{{$}}
#  CHECK-NEXT: false
# CHECK-EMPTY:
#  CHECK-NEXT: --

# CHECK-LABEL: FAIL: shtest-run-at-line :: external-shell/empty-run-line.txt

#       CHECK: Command Output (stderr)
#  CHECK-NEXT: --
#  CHECK-NEXT: {{^}}RUN: at line 2 has no command after substitutions{{$}}
#  CHECK-NEXT: {{^}}RUN: at line 3: false{{$}}
#  CHECK-NEXT: false
# CHECK-EMPTY:
#  CHECK-NEXT: --

# CHECK-LABEL: FAIL: shtest-run-at-line :: external-shell/line-continuation.txt

# The execution trace from an external sh-like shell might print the commands
# from a pipeline in any order, so this time just check that lit suppresses the
# trace of the echo command for each 'RUN: at line N: cmd-line'.

#       CHECK: Command Output (stderr)
#  CHECK-NEXT: --
#  CHECK-NEXT: {{^}}RUN: at line 4: echo 'foo bar' | FileCheck
#   CHECK-NOT: RUN
#       CHECK: {{^}}RUN: at line 6: echo 'foo baz' | FileCheck
#   CHECK-NOT: RUN
#       CHECK: --

# CHECK-LABEL: FAIL: shtest-run-at-line :: external-shell/run-line-with-newline.txt

#      CHECK: Command Output (stderr)
# CHECK-NEXT: --
# CHECK-NEXT: {{^}}RUN: at line 1: echo abc |
# CHECK-NEXT: FileCheck {{.*}} &&
# CHECK-NEXT: false
#  CHECK-NOT: RUN


# CHECK-LABEL: FAIL: shtest-run-at-line :: internal-shell/basic.txt

# CHECK:      Command Output (stdout)
# CHECK-NEXT: --
# CHECK-NEXT: # RUN: at line 1
# CHECK-NEXT: true
# CHECK-NEXT: # executed command: true
# CHECK-NEXT: # RUN: at line 2
# CHECK-NEXT: false
# CHECK-NEXT: # executed command: false
# CHECK-NOT:  RUN

# CHECK-LABEL: FAIL: shtest-run-at-line :: internal-shell/empty-run-line.txt

#      CHECK: Command Output (stdout)
# CHECK-NEXT: --
# CHECK-NEXT: # RUN: at line 2 has no command after substitutions
# CHECK-NEXT: # RUN: at line 3
# CHECK-NEXT: false
# CHECK-NEXT: # executed command: false
#  CHECK-NOT: RUN

# CHECK-LABEL: FAIL: shtest-run-at-line :: internal-shell/line-continuation.txt

# CHECK:      Command Output (stdout)
# CHECK-NEXT: --
# CHECK-NEXT: # RUN: at line 1
# CHECK-NEXT: : first line continued to second line
# CHECK-NEXT: # executed command: : first line continued to second line
# CHECK-NEXT: # RUN: at line 3
# CHECK-NEXT: echo 'foo bar' | FileCheck {{.*}}
# CHECK-NEXT: # executed command: echo 'foo bar'
# CHECK-NEXT: # executed command: FileCheck {{.*}}
# CHECK-NEXT: # RUN: at line 5
# CHECK-NEXT: echo 'foo baz' | FileCheck {{.*}}
# CHECK-NEXT: # executed command: echo 'foo baz'
# CHECK-NEXT: # executed command: FileCheck {{.*}}
# CHECK-NOT:  RUN

# CHECK-LABEL: FAIL: shtest-run-at-line :: internal-shell/run-line-with-newline.txt

#      CHECK: Command Output (stdout)
# CHECK-NEXT: --
# CHECK-NEXT: # RUN: at line 1
# CHECK-NEXT: echo abc |
# CHECK-NEXT: FileCheck {{.*}} &&
# CHECK-NEXT: false
# CHECK-NEXT: # executed command: echo abc
# CHECK-NEXT: # executed command: FileCheck {{.*}}
# CHECK-NEXT: # executed command: false
#  CHECK-NOT: RUN
