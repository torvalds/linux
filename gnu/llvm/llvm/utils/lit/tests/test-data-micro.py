# Test features related to formats which support reporting additional test data.
# and multiple test results.

# RUN: %{lit} -v %{inputs}/test-data-micro | FileCheck %s

# CHECK: -- Testing:

# CHECK: PASS: test-data-micro :: micro-tests.ini
# CHECK-NEXT: *** TEST 'test-data-micro :: micro-tests.ini' RESULTS ***
# CHECK-NEXT: value0: 1
# CHECK-NEXT: value1: 2.3456
# CHECK-NEXT: ***
# CHECK-NEXT: *** MICRO-TEST: test0
# CHECK-NEXT: micro_value0: 4
# CHECK-NEXT: micro_value1: 1.3
# CHECK-NEXT: *** MICRO-TEST: test1
# CHECK-NEXT: micro_value0: 4
# CHECK-NEXT: micro_value1: 1.3
# CHECK-NEXT: *** MICRO-TEST: test2
# CHECK-NEXT: micro_value0: 4
# CHECK-NEXT: micro_value1: 1.3
