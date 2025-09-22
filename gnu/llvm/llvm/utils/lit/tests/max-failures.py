# UNSUPPORTED: system-windows
# FIXME: This test is flaky and hangs randomly on multi-core systems.
# See https://github.com/llvm/llvm-project/issues/56336 for more
# details.
# REQUIRES:  less-than-4-cpu-cores-in-parallel

# Check the behavior of --max-failures option.
#
# RUN: not %{lit}                  %{inputs}/max-failures >  %t.out 2>&1
# RUN: not %{lit} --max-failures=1 %{inputs}/max-failures >> %t.out 2>&1
# RUN: not %{lit} --max-failures=2 %{inputs}/max-failures >> %t.out 2>&1
# RUN: not %{lit} --max-failures=0 %{inputs}/max-failures 2>> %t.out
# RUN: FileCheck < %t.out %s
#

# CHECK-NOT: reached maximum number of test failures
# CHECK-NOT: Skipped
# CHECK: Failed: 3

# CHECK: reached maximum number of test failures, skipping remaining tests
# CHECK: Skipped: 2
# CHECK: Failed : 1

# CHECK: reached maximum number of test failures, skipping remaining tests
# CHECK: Skipped: 1
# CHECK: Failed : 2

# CHECK: error: argument --max-failures: requires positive integer, but found '0'
