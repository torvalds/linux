# Check GoogleTest shard test crashes are handled.

# RUN: not %{lit} -v %{inputs}/googletest-crash | FileCheck %s

# CHECK: -- Testing:
# CHECK: FAIL: googletest-crash :: [[PATH:[Dd]ummy[Ss]ub[Dd]ir/]][[FILE:OneTest\.py]]/0
# CHECK: *** TEST 'googletest-crash :: [[PATH]][[FILE]]/0{{.*}} FAILED ***
# CHECK-NEXT: Script(shard):
# CHECK-NEXT: --
# CHECK-NEXT: GTEST_OUTPUT=json:[[JSON:[^[:space:]]*\.json]] GTEST_SHUFFLE=0 GTEST_TOTAL_SHARDS={{[1-6]}} GTEST_SHARD_INDEX=0 {{.*}}[[FILE]]
# CHECK-NEXT: --
# CHECK-EMPTY:
# CHECK-NEXT: [----------] 4 test from FirstTest
# CHECK-NEXT: [ RUN      ] FirstTest.subTestA
# CHECK-NEXT: [       OK ] FirstTest.subTestA (18 ms)
# CHECK-NEXT: [ RUN      ] FirstTest.subTestB
# CHECK-NEXT: I am about to crash
# CHECK-EMPTY:
# CHECK-NEXT: --
# CHECK-NEXT: exit:
# CHECK-NEXT: --
# CHECK-NEXT: shard JSON output does not exist: [[JSON]]
# CHECK-NEXT: ***
# CHECK: Failed Tests (1):
# CHECK-NEXT:   googletest-crash :: [[PATH]][[FILE]]/0/{{[1-6]}}
# CHECK: Failed{{ *}}: 1
