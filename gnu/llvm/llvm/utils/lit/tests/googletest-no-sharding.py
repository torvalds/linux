# Check the various features of the GoogleTest format.

# RUN: not %{lit} -v --no-gtest-sharding --order=random %{inputs}/googletest-no-sharding > %t.out
# FIXME: Temporarily dump test output so we can debug failing tests on
# buildbots.
# RUN: cat %t.out
# RUN: FileCheck < %t.out %s
#
# END.

# CHECK: -- Testing:
# CHECK: FAIL: googletest-no-sharding :: [[PATH:[Dd]ummy[Ss]ub[Dd]ir/]][[FILE:OneTest\.py]]
# CHECK: *** TEST 'googletest-no-sharding :: [[PATH]][[FILE]]' FAILED ***
# CHECK-NEXT: Script(shard):
# CHECK-NEXT: --
# CHECK-NEXT: GTEST_OUTPUT=json:{{[^[:space:]]*}} GTEST_SHUFFLE=1 GTEST_RANDOM_SEED=123 {{.*}}[[FILE]]
# CHECK-NEXT: --
# CHECK-EMPTY:
# CHECK-NEXT: Script:
# CHECK-NEXT: --
# CHECK-NEXT: [[FILE]] --gtest_filter=FirstTest.subTestB
# CHECK-NEXT: --
# CHECK-NEXT: I am subTest B output
# CHECK-EMPTY:
# CHECK-NEXT: I am subTest B, I FAIL
# CHECK-NEXT: And I have two lines of output
# CHECK-EMPTY:
# CHECK: Script:
# CHECK-NEXT: --
# CHECK-NEXT: [[FILE]] --gtest_filter=FirstTest.subTestD
# CHECK-NEXT: --
# CHECK-NEXT: unresolved test result
# CHECK: ***
# CHECK: ***
# CHECK: Unresolved Tests (1):
# CHECK-NEXT:   googletest-no-sharding :: FirstTest/subTestD
# CHECK: ***
# CHECK-NEXT: Failed Tests (1):
# CHECK-NEXT:   googletest-no-sharding :: FirstTest/subTestB
# CHECK: Skipped{{ *}}: 1
# CHECK: Passed{{ *}}: 3
# CHECK: Unresolved{{ *}}: 1
# CHECK: Failed{{ *}}: 1
