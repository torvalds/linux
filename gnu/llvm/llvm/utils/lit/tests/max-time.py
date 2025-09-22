# UNSUPPORTED: system-windows

# Test overall lit timeout (--max-time).
#
# RUN: %{lit} %{inputs}/max-time --max-time=5 2>&1  |  FileCheck %s

# CHECK: reached timeout, skipping remaining tests
# CHECK: Skipped: 1
# CHECK: Passed : 1
