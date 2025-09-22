# Check GTEST_TOTAL_SHARDS and GTEST_SHARD_INDEX environment variabls are
# respected when using the googletest formatter.

# RUN: env GTEST_TOTAL_SHARDS=1 GTEST_SHARD_INDEX=0 \
# RUN: not %{lit} -v --order=random %{inputs}/googletest-format-respect-gtest-sharding-env-vars > %t.out
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
# CHECK-NEXT: GTEST_OUTPUT=json:{{[^[:space:]]*}} GTEST_SHUFFLE=1 GTEST_TOTAL_SHARDS=1 GTEST_SHARD_INDEX=0 GTEST_RANDOM_SEED=123 {{.*}}[[FILE]]
