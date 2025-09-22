# Check that --ignore-fail produces exit status 0 despite various kinds of
# test failures but doesn't otherwise suppress those failures.

# RUN: not %{lit} %{inputs}/ignore-fail | FileCheck %s
# RUN: %{lit} --ignore-fail %{inputs}/ignore-fail | FileCheck %s

# END.

# CHECK-DAG: FAIL: ignore-fail :: fail.txt
# CHECK-DAG: UNRESOLVED: ignore-fail :: unresolved.txt
# CHECK-DAG: XFAIL: ignore-fail :: xfail.txt
# CHECK-DAG: XPASS: ignore-fail :: xpass.txt

#      CHECK: Testing Time:
# CHECK: Total Discovered Tests:
# CHECK-NEXT:   Expectedly Failed : 1 {{\([0-9]*\.[0-9]*%\)}}
# CHECK-NEXT:   Unresolved : 1 {{\([0-9]*\.[0-9]*%\)}}
# CHECK-NEXT:   Failed : 1 {{\([0-9]*\.[0-9]*%\)}}
# CHECK-NEXT:   Unexpectedly Passed: 1 {{\([0-9]*\.[0-9]*%\)}}
#  CHECK-NOT: {{.}}
