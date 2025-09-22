# Test features related to formats which support reporting additional test data.

# RUN: %{lit} -v %{inputs}/test-data > %t.out
# RUN: FileCheck < %t.out %s

# CHECK: -- Testing:

# CHECK: PASS: test-data :: metrics.ini
# CHECK-NEXT: *** TEST 'test-data :: metrics.ini' RESULTS ***
# CHECK-NEXT: value0: 1
# CHECK-NEXT: value1: 2.3456
# CHECK-NEXT: ***
