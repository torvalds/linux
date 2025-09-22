# REQUIRES: lit-max-individual-test-time

###############################################################################
# Check tests can hit timeout when set
###############################################################################

# Check that the per test timeout is enforced when running GTest tests.
#
# RUN: not %{lit} -v %{inputs}/googletest-timeout \
# RUN:   --param gtest_filter=InfiniteLoopSubTest --timeout=1 > %t.cmd.out
# RUN: FileCheck --check-prefix=CHECK-INF < %t.cmd.out %s

# Check that the per test timeout is enforced when running GTest tests via
# the configuration file
#
# RUN: not %{lit} -v %{inputs}/googletest-timeout \
# RUN:  --param gtest_filter=InfiniteLoopSubTest  --param set_timeout=1 \
# RUN:  > %t.cfgset.out
# RUN: FileCheck --check-prefix=CHECK-INF < %t.cfgset.out %s

# CHECK-INF: -- Testing:
# CHECK-INF: TIMEOUT: googletest-timeout :: [[PATH:[Dd]ummy[Ss]ub[Dd]ir/]][[FILE:OneTest.py]]/0/2
# CHECK-INF-NEXT: ******************** TEST 'googletest-timeout :: [[PATH]][[FILE]]/0/2' FAILED ********************
# CHECK-INF-NEXT: Script(shard):
# CHECK-INF-NEXT: --
# CHECK-INF-NEXT: GTEST_OUTPUT=json:{{[^[:space:]]*}} GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS=2 GTEST_SHARD_INDEX=0 {{.*}}[[FILE]]
# CHECK-INF-NEXT: --
# CHECK-INF-EMPTY:
# CHECK-INF-EMPTY:
# CHECK-INF-NEXT: --
# CHECK-INF-NEXT: exit:
# CHECK-INF-NEXT: --
# CHECK-INF-NEXT: Reached timeout of 1 seconds
# CHECK-INF: Timed Out: 1

###############################################################################
# Check tests can complete with a timeout set
#
# `QuickSubTest` should execute quickly so we shouldn't wait anywhere near the
# 3600 second timeout.
###############################################################################

# RUN: %{lit} -v %{inputs}/googletest-timeout \
# RUN:   --param gtest_filter=QuickSubTest --timeout=3600 > %t.cmd.out
# RUN: FileCheck --check-prefix=CHECK-QUICK < %t.cmd.out %s

# CHECK-QUICK: PASS: googletest-timeout :: {{[Dd]ummy[Ss]ub[Dd]ir}}/OneTest.py/0/2 {{.*}}
# CHECK-QUICK: Passed: 1

# Test per test timeout via a config file and on the command line.
# The value set on the command line should override the config file.
# RUN: %{lit} -v %{inputs}/googletest-timeout --param gtest_filter=QuickSubTest \
# RUN:   --param set_timeout=1 --timeout=3600 \
# RUN:   > %t.cmdover.out 2> %t.cmdover.err
# RUN: FileCheck --check-prefix=CHECK-QUICK < %t.cmdover.out %s
# RUN: FileCheck --check-prefix=CHECK-CMDLINE-OVERRIDE-ERR < %t.cmdover.err %s

# CHECK-CMDLINE-OVERRIDE-ERR: Forcing timeout to be 3600 seconds
