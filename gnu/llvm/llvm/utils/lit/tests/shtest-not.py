# Check the not command

# RUN: not %{lit} -a -v %{inputs}/shtest-not \
# RUN: | FileCheck -match-full-lines %s
#
# END.

# Make sure not and env commands are included in printed commands.

# CHECK: -- Testing: 17 tests{{.*}}

# CHECK: FAIL: shtest-not :: exclamation-args-nested-none.txt {{.*}}
# CHECK: ! ! !
# CHECK: # executed command: ! ! !
# CHECK: # | Error: '!' requires a subcommand
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-not :: exclamation-args-none.txt {{.*}}
# CHECK: !
# CHECK: # executed command: !
# CHECK: # | Error: '!' requires a subcommand
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-not :: exclamation-calls-external.txt {{.*}}

# CHECK: ! [[PYTHON:.*]] fail.py
# CHECK: # executed command: ! [[PYTHON_BARE:.*]] fail.py
# CHECK: ! ! [[PYTHON]] pass.py
# CHECK: # executed command: ! ! [[PYTHON_BARE]] pass.py
# CHECK: ! ! ! [[PYTHON]] fail.py
# CHECK: # executed command: ! ! ! [[PYTHON_BARE]] fail.py
# CHECK: ! ! ! ! [[PYTHON]] pass.py
# CHECK: # executed command: ! ! ! ! [[PYTHON_BARE]] pass.py

# CHECK: ! [[PYTHON]] pass.py
# CHECK: # executed command: ! [[PYTHON_BARE]] pass.py
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-not :: not-args-last-is-crash.txt {{.*}}
# CHECK: not --crash
# CHECK: # executed command: not --crash
# CHECK: # | Error: 'not' requires a subcommand
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-not :: not-args-nested-none.txt {{.*}}
# CHECK: not not not
# CHECK: # executed command: not not not
# CHECK: # | Error: 'not' requires a subcommand
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-not :: not-args-none.txt {{.*}}
# CHECK: not
# CHECK: # executed command: not
# CHECK: # | Error: 'not' requires a subcommand
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-not :: not-calls-cd.txt {{.*}}
# CHECK: not not cd foobar
# CHECK: # executed command: not not cd foobar
# CHECK: not --crash cd foobar
# CHECK: # executed command: not --crash cd foobar
# CHECK: # | Error: 'not --crash' cannot call 'cd'
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-not :: not-calls-colon.txt {{.*}}
# CHECK: not not : foobar
# CHECK: # executed command: not not : foobar
# CHECK: not --crash :
# CHECK: # executed command: not --crash :
# CHECK: # | Error: 'not --crash' cannot call ':'
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-not :: not-calls-diff-with-crash.txt {{.*}}
# CHECK: not --crash diff -u {{.*}}
# CHECK: # executed command: not --crash diff -u {{.*}}
# CHECK-NOT: # executed command: {{.*}}
# CHECK-NOT: {{[Ee]rror}}
# CHECK: # error: command failed with exit status: {{.*}}
# CHECK-NOT: # executed command: {{.*}}
# CHECK-NOT: {{[Ee]rror}}

# CHECK: FAIL: shtest-not :: not-calls-diff.txt {{.*}}
# CHECK: not diff {{.*}}
# CHECK: # executed command: not diff {{.*}}
# CHECK: not not not diff {{.*}}
# CHECK: # executed command: not not not diff {{.*}}
# CHECK: not not not not not diff {{.*}}
# CHECK: # executed command: not not not not not diff {{.*}}
# CHECK: diff {{.*}}
# CHECK: # executed command: diff {{.*}}
# CHECK: not not diff {{.*}}
# CHECK: # executed command: not not diff {{.*}}
# CHECK: not not not not diff {{.*}}
# CHECK: # executed command: not not not not diff {{.*}}
# CHECK: not diff {{.*}}
# CHECK: # executed command: not diff {{.*}}
# CHECK-NOT: # executed command: {{.*}}

# CHECK: FAIL: shtest-not :: not-calls-echo.txt {{.*}}
# CHECK: not not echo hello world
# CHECK: # executed command: not not echo hello world
# CHECK: not --crash echo hello world
# CHECK: # executed command: not --crash echo hello world
# CHECK: # | Error: 'not --crash' cannot call 'echo'
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-not :: not-calls-env-builtin.txt {{.*}}
# CHECK: not --crash env -u FOO BAR=3 rm {{.*}}.no-such-file
# CHECK: # executed command: not --crash env -u FOO BAR=3 rm {{.+}}.no-such-file{{.*}}
# CHECK: # | Error: 'env' cannot call 'rm'
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-not :: not-calls-export.txt {{.*}}
# CHECK: not not export FOO=1
# CHECK: # executed command: not not export FOO=1
# CHECK: not --crash export BAZ=3
# CHECK: # executed command: not --crash export BAZ=3
# CHECK: # | Error: 'not --crash' cannot call 'export'
# CHECK: # error: command failed with exit status: {{.*}}


# CHECK: PASS: shtest-not :: not-calls-external.txt {{.*}}

# CHECK: not [[PYTHON]] fail.py
# CHECK: # executed command: not [[PYTHON_BARE]] fail.py
# CHECK: not not [[PYTHON]] pass.py
# CHECK: # executed command: not not [[PYTHON_BARE]] pass.py
# CHECK: not not not [[PYTHON]] fail.py
# CHECK: # executed command: not not not [[PYTHON_BARE]] fail.py
# CHECK: not not not not [[PYTHON]] pass.py
# CHECK: # executed command: not not not not [[PYTHON_BARE]] pass.py

# CHECK: not not --crash [[PYTHON]] pass.py
# CHECK: # executed command: not not --crash [[PYTHON_BARE]] pass.py
# CHECK: not not --crash [[PYTHON]] fail.py
# CHECK: # executed command: not not --crash [[PYTHON_BARE]] fail.py
# CHECK: not not --crash not [[PYTHON]] pass.py
# CHECK: # executed command: not not --crash not [[PYTHON_BARE]] pass.py
# CHECK: not not --crash not [[PYTHON]] fail.py
# CHECK: # executed command: not not --crash not [[PYTHON_BARE]] fail.py

# CHECK: env not [[PYTHON]] fail.py | {{.*}}
# CHECK: # executed command: env not [[PYTHON_BARE]] fail.py
# CHECK: not env [[PYTHON]] fail.py | {{.*}}
# CHECK: # executed command: not env [[PYTHON_BARE]] fail.py
# CHECK: env FOO=1 not [[PYTHON]] fail.py | {{.*}}
# CHECK: # executed command: env FOO=1 not [[PYTHON_BARE]] fail.py
# CHECK: not env FOO=1 BAR=1 [[PYTHON]] fail.py | {{.*}}
# CHECK: # executed command: not env FOO=1 BAR=1 [[PYTHON_BARE]] fail.py
# CHECK: env FOO=1 BAR=1 not env -u FOO BAR=2 [[PYTHON]] fail.py | {{.*}}
# CHECK: # executed command: env FOO=1 BAR=1 not env -u FOO BAR=2 [[PYTHON_BARE]] fail.py
# CHECK: not env FOO=1 BAR=1 not env -u FOO -u BAR [[PYTHON]] pass.py | {{.*}}
# CHECK: # executed command: not env FOO=1 BAR=1 not env -u FOO -u BAR [[PYTHON_BARE]] pass.py
# CHECK: not not env FOO=1 env FOO=2 BAR=1 [[PYTHON]] pass.py | {{.*}}
# CHECK: # executed command: not not env FOO=1 env FOO=2 BAR=1 [[PYTHON_BARE]] pass.py
# CHECK: env FOO=1 -u BAR env -u FOO BAR=1 not not [[PYTHON]] pass.py | {{.*}}
# CHECK: # executed command: env FOO=1 -u BAR env -u FOO BAR=1 not not [[PYTHON_BARE]] pass.py

# CHECK: not env FOO=1 BAR=1 env FOO=2 BAR=2 not --crash [[PYTHON]] pass.py | {{.*}}
# CHECK: # executed command: not env FOO=1 BAR=1 env FOO=2 BAR=2 not --crash [[PYTHON_BARE]] pass.py
# CHECK: not env FOO=1 BAR=1 not --crash not [[PYTHON]] pass.py | {{.*}}
# CHECK: # executed command: not env FOO=1 BAR=1 not --crash not [[PYTHON_BARE]] pass.py
# CHECK: not not --crash env -u BAR not env -u FOO BAR=1 [[PYTHON]] pass.py | {{.*}}
# CHECK: # executed command: not not --crash env -u BAR not env -u FOO BAR=1 [[PYTHON_BARE]] pass.py


# CHECK: FAIL: shtest-not :: not-calls-fail2.txt {{.*}}
# CHECK-NEXT: {{.*}} TEST 'shtest-not :: not-calls-fail2.txt' FAILED {{.*}}
# CHECK-NEXT: Exit Code: 1

# CHECK: FAIL: shtest-not :: not-calls-mkdir.txt {{.*}}
# CHECK: not mkdir {{.*}}
# CHECK: # executed command: not mkdir {{.*}}
# CHECK: not --crash mkdir foobar
# CHECK: # executed command: not --crash mkdir foobar
# CHECK: # | Error: 'not --crash' cannot call 'mkdir'
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-not :: not-calls-rm.txt {{.*}}
# CHECK: not rm {{.*}}
# CHECK: # executed command: not rm {{.*}}
# CHECK: not --crash rm foobar
# CHECK: # executed command: not --crash rm foobar
# CHECK: # | Error: 'not --crash' cannot call 'rm'
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: Total Discovered Tests: 17
# CHECK: Passed:  1 {{\([0-9]*\.[0-9]*%\)}}
# CHECK: Failed: 16 {{\([0-9]*\.[0-9]*%\)}}
# CHECK-NOT: {{.}}
