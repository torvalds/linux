# This test exercises an external shell use case that, at least at one time,
# appeared in the following tests:
#
#   compiler-rt/test/fuzzer/fork-sigusr.test
#   compiler-rt/test/fuzzer/merge-sigusr.test
#   compiler-rt/test/fuzzer/sigint.test
#   compiler-rt/test/fuzzer/sigusr.test
#
# That is, a RUN line can be:
#
#   cmd & PID=$!
#
# It is important that '&' only puts 'cmd' in the background and not the
# debugging commands that lit inserts before 'cmd'.  Otherwise:
#
# - The debugging commands might execute later than they are supposed to.
# - A later 'kill $PID' can kill more than just 'cmd'.  We've seen it even
#   manage to terminate the shell running lit.
#
# The last FileCheck directive below checks that the debugging commands for the
# above RUN line are not killed and do execute at the right time.

# RUN: %{lit} -a %{inputs}/shtest-external-shell-kill | %{filter-lit} | FileCheck %s
# END.

#       CHECK: Command Output (stdout):
#  CHECK-NEXT: --
#  CHECK-NEXT: start
#  CHECK-NEXT: end
# CHECK-EMPTY:
#  CHECK-NEXT: --
#  CHECK-NEXT: Command Output (stderr):
#  CHECK-NEXT: --
#  CHECK-NEXT: RUN: at line 1: echo start
#  CHECK-NEXT: echo start
#  CHECK-NEXT: RUN: at line 2: sleep [[#]] & PID=$!
