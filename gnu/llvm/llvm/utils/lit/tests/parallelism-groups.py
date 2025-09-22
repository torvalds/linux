# Check that we do not crash if a parallelism group is set to None. Permits
# usage of the following pattern.
#
# [lit.common.cfg]
#   lit_config.parallelism_groups['my_group'] = None
#   if <condition>:
#     lit_config.parallelism_groups['my_group'] = 3
#
# [project/lit.cfg]
#   config.parallelism_group = 'my_group'
#

# RUN: %{lit} -j2 %{inputs}/parallelism-groups | FileCheck %s

# CHECK:     -- Testing: 2 tests, 2 workers --
# CHECK-DAG: PASS: parallelism-groups :: test1.txt
# CHECK-DAG: PASS: parallelism-groups :: test2.txt
# CHECK:     Passed: 2
