# Check the pushd and popd commands

# RUN: not %{lit} -a -v %{inputs}/shtest-pushd-popd \
# RUN: | FileCheck -match-full-lines %s
#
# END.

# CHECK: -- Testing: 4 tests{{.*}}

# CHECK: FAIL: shtest-pushd-popd :: popd-args.txt ({{[^)]*}})
# CHECK: popd invalid
# CHECK: # | 'popd' does not support arguments

# CHECK: FAIL: shtest-pushd-popd :: popd-no-stack.txt ({{[^)]*}})
# CHECK: popd
# CHECK: # | popd: directory stack empty

# CHECK: FAIL: shtest-pushd-popd :: pushd-too-many-args.txt ({{[^)]*}})
# CHECK: pushd a b
# CHECK: # | 'pushd' supports only one argument

# CHECK: Total Discovered Tests: 4
# CHECK: Passed:  1 {{\([0-9]*\.[0-9]*%\)}}
# CHECK: Failed:  3 {{\([0-9]*\.[0-9]*%\)}}
# CHECK-NOT: {{.}}
