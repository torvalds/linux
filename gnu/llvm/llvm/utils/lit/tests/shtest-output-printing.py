# Check the various features of the ShTest format.
#
# RUN: not %{lit} -v %{inputs}/shtest-output-printing > %t.out
# RUN: FileCheck --input-file %t.out --match-full-lines %s
#
# END.

#       CHECK: -- Testing: {{.*}}
#       CHECK: FAIL: shtest-output-printing :: basic.txt {{.*}}
#  CHECK-NEXT: ***{{\**}} TEST 'shtest-output-printing :: basic.txt' FAILED ***{{\**}}
#  CHECK-NEXT: Exit Code: 1
# CHECK-EMPTY:
#  CHECK-NEXT: Command Output (stdout):
#  CHECK-NEXT: --
#  CHECK-NEXT: # RUN: at line 1
#  CHECK-NEXT: true
#  CHECK-NEXT: # executed command: true
#  CHECK-NEXT: # RUN: at line 2
#  CHECK-NEXT: echo hi
#  CHECK-NEXT: # executed command: echo hi
#  CHECK-NEXT: # .---command stdout------------
#  CHECK-NEXT: # | hi
#  CHECK-NEXT: # `-----------------------------
#  CHECK-NEXT: # RUN: at line 3
#  CHECK-NEXT: not not wc missing-file &> [[FILE:.*]] || true
#  CHECK-NEXT: # executed command: not not wc missing-file
#  CHECK-NEXT: # .---redirected output from '[[FILE]]'
#  CHECK-NEXT: # | wc: {{cannot open missing-file|missing-file.* No such file or directory}}
#  CHECK-NEXT: # `-----------------------------
#  CHECK-NEXT: # note: command had no output on stdout or stderr
#  CHECK-NEXT: # error: command failed with exit status: 1
#  CHECK-NEXT: # executed command: true
#  CHECK-NEXT: # RUN: at line 4
#  CHECK-NEXT: not {{.*}}python{{.*}} {{.*}}write-a-lot.py &> [[FILE:.*]]
#  CHECK-NEXT: # executed command: not {{.*}}python{{.*}} {{.*}}write-a-lot.py{{.*}}
#  CHECK-NEXT: # .---redirected output from '[[FILE]]'
#  CHECK-NEXT: # | All work and no play makes Jack a dull boy.
#  CHECK-NEXT: # | All work and no play makes Jack a dull boy.
#  CHECK-NEXT: # | All work and no play makes Jack a dull boy.
#       CHECK: # | ...
#  CHECK-NEXT: # `---data was truncated--------
#  CHECK-NEXT: # note: command had no output on stdout or stderr
#  CHECK-NEXT: # error: command failed with exit status: 1
# CHECK-EMPTY:
#  CHECK-NEXT:--
