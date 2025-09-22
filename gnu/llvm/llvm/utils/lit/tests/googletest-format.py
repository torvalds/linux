# Check the various features of the GoogleTest format.

# RUN: not %{lit} -v --order=random %{inputs}/googletest-format > %t.out
# FIXME: Temporarily dump test output so we can debug failing tests on
# buildbots.
# RUN: cat %t.out
# RUN: FileCheck < %t.out %s
#
# END.

# CHECK: -- Testing:
# CHECK: FAIL: googletest-format :: [[PATH:[Dd]ummy[Ss]ub[Dd]ir/]][[FILE:OneTest\.py]]/0
# CHECK: *** TEST 'googletest-format :: [[PATH]][[FILE]]/0{{.*}} FAILED ***
# CHECK-NEXT: Script(shard):
# CHECK-NEXT: --
# CHECK-NEXT: GTEST_OUTPUT=json:{{[^[:space:]]*}} GTEST_SHUFFLE=1 GTEST_TOTAL_SHARDS={{[1-6]}} GTEST_SHARD_INDEX=0 GTEST_RANDOM_SEED=123 {{.*}}[[FILE]]
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
# CHECK: Unresolved Tests (1):
# CHECK-NEXT:   googletest-format :: [[PATH]][[FILE]]/FirstTest/subTestD
# CHECK: ***
# CHECK-NEXT: Failed Tests (1):
# CHECK-NEXT:   googletest-format :: [[PATH]][[FILE]]/FirstTest/subTestB
# CHECK: Skipped{{ *}}: 1
# CHECK: Passed{{ *}}: 3
# CHECK: Unresolved{{ *}}: 1
# CHECK: Failed{{ *}}: 1
