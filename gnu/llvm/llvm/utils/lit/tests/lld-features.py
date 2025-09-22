## Show that each of the LLD variants detected by use_lld comes with its own
## feature.

# RUN: %{lit} %{inputs}/lld-features 2>&1 | FileCheck %s -DDIR=%p

# CHECK: Passed: 4
