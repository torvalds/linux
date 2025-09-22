# Check the env command

# RUN: not %{lit} -a -v %{inputs}/shtest-env \
# RUN: | FileCheck -match-full-lines %s
#
# END.

# Make sure env commands are included in printed commands.

# CHECK: -- Testing: 16 tests{{.*}}

# CHECK: FAIL: shtest-env :: env-args-last-is-assign.txt ({{[^)]*}})
# CHECK: env FOO=1
# CHECK: # executed command: env FOO=1
# CHECK: # | Error: 'env' requires a subcommand
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-env :: env-args-last-is-u-arg.txt ({{[^)]*}})
# CHECK: env -u FOO
# CHECK: # executed command: env -u FOO
# CHECK: # | Error: 'env' requires a subcommand
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-env :: env-args-last-is-u.txt ({{[^)]*}})
# CHECK: env -u
# CHECK: # executed command: env -u
# CHECK: # | Error: 'env' requires a subcommand
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-env :: env-args-nested-none.txt ({{[^)]*}})
# CHECK: env env env
# CHECK: # executed command: env env env
# CHECK: # | Error: 'env' requires a subcommand
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-env :: env-args-none.txt ({{[^)]*}})
# CHECK: env
# CHECK: # executed command: env
# CHECK: # | Error: 'env' requires a subcommand
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-env :: env-calls-cd.txt ({{[^)]*}})
# CHECK: env -u FOO BAR=3 cd foobar
# CHECK: # executed command: env -u FOO BAR=3 cd foobar
# CHECK: # | Error: 'env' cannot call 'cd'
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-env :: env-calls-colon.txt ({{[^)]*}})
# CHECK: env -u FOO BAR=3 :
# CHECK: # executed command: env -u FOO BAR=3 :
# CHECK: # | Error: 'env' cannot call ':'
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-env :: env-calls-echo.txt ({{[^)]*}})
# CHECK: env -u FOO BAR=3 echo hello world
# CHECK: # executed command: env -u FOO BAR=3 echo hello world
# CHECK: # | Error: 'env' cannot call 'echo'
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: PASS: shtest-env :: env-calls-env.txt ({{[^)]*}})
# CHECK: env env [[PYTHON:.+]] print_environment.py | {{.*}}
# CHECK: # executed command: env env [[PYTHON_BARE:.+]] print_environment.py
# CHECK: env FOO=2 env BAR=1 [[PYTHON]] print_environment.py | {{.*}}
# CHECK: # executed command: env FOO=2 env BAR=1 [[PYTHON_BARE]] print_environment.py
# CHECK: env -u FOO env -u BAR [[PYTHON]] print_environment.py | {{.*}}
# CHECK: # executed command: env -u FOO env -u BAR [[PYTHON_BARE]] print_environment.py
# CHECK: env -u FOO BAR=1 env -u BAR FOO=2 [[PYTHON]] print_environment.py | {{.*}}
# CHECK: # executed command: env -u FOO BAR=1 env -u BAR FOO=2 [[PYTHON_BARE]] print_environment.py
# CHECK: env -u FOO BAR=1 env -u BAR FOO=2 env BAZ=3 [[PYTHON]] print_environment.py | {{.*}}
# CHECK: # executed command: env -u FOO BAR=1 env -u BAR FOO=2 env BAZ=3 [[PYTHON_BARE]] print_environment.py
# CHECK-NOT: {{^[^#]}}
# CHECK: --

# CHECK: FAIL: shtest-env :: env-calls-export.txt ({{[^)]*}})
# CHECK: env -u FOO BAR=3 export BAZ=3
# CHECK: # executed command: env -u FOO BAR=3 export BAZ=3
# CHECK: # | Error: 'env' cannot call 'export'
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-env :: env-calls-mkdir.txt ({{[^)]*}})
# CHECK: env -u FOO BAR=3 mkdir foobar
# CHECK: # executed command: env -u FOO BAR=3 mkdir foobar
# CHECK: # | Error: 'env' cannot call 'mkdir'
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-env :: env-calls-not-builtin.txt ({{[^)]*}})
# CHECK: env -u FOO BAR=3 not rm {{.+}}.no-such-file
# CHECK: # executed command: env -u FOO BAR=3 not rm {{.+}}.no-such-file{{.*}}
# CHECK: # | Error: 'env' cannot call 'rm'
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: FAIL: shtest-env :: env-calls-rm.txt ({{[^)]*}})
# CHECK: env -u FOO BAR=3 rm foobar
# CHECK: # executed command: env -u FOO BAR=3 rm foobar
# CHECK: # | Error: 'env' cannot call 'rm'
# CHECK: # error: command failed with exit status: {{.*}}

# CHECK: PASS: shtest-env :: env-u.txt ({{[^)]*}})
# CHECK: [[PYTHON]] print_environment.py | {{.*}}
# CHECK: env -u FOO [[PYTHON]] print_environment.py | {{.*}}
# CHECK: # executed command: env -u FOO [[PYTHON_BARE]] print_environment.py
# CHECK: env -u FOO -u BAR [[PYTHON]] print_environment.py | {{.*}}
# CHECK: # executed command: env -u FOO -u BAR [[PYTHON_BARE]] print_environment.py
# CHECK-NOT: {{^[^#]}}
# CHECK: --

# CHECK: PASS: shtest-env :: env.txt ({{[^)]*}})
# CHECK: env A_FOO=999 [[PYTHON]] print_environment.py | {{.*}}
# CHECK: # executed command: env A_FOO=999 [[PYTHON_BARE]] print_environment.py
# CHECK: env A_FOO=1 B_BAR=2 C_OOF=3 [[PYTHON]] print_environment.py | {{.*}}
# CHECK: # executed command: env A_FOO=1 B_BAR=2 C_OOF=3 [[PYTHON_BARE]] print_environment.py
# CHECK-NOT: {{^[^#]}}
# CHECK: --

# CHECK: PASS: shtest-env :: mixed.txt ({{[^)]*}})
# CHECK: env A_FOO=999 -u FOO [[PYTHON]] print_environment.py | {{.*}}
# CHECK: # executed command: env A_FOO=999 -u FOO [[PYTHON_BARE]] print_environment.py
# CHECK: env A_FOO=1 -u FOO B_BAR=2 -u BAR C_OOF=3 [[PYTHON]] print_environment.py | {{.*}}
# CHECK: # executed command: env A_FOO=1 -u FOO B_BAR=2 -u BAR C_OOF=3 [[PYTHON_BARE]] print_environment.py
# CHECK-NOT: {{^[^#]}}
# CHECK: --

# CHECK: Total Discovered Tests: 16
# CHECK: Passed:  4 {{\([0-9]*\.[0-9]*%\)}}
# CHECK: Failed: 12 {{\([0-9]*\.[0-9]*%\)}}
# CHECK-NOT: {{.}}
