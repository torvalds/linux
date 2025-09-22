## Check that --skip-test-time-recording skips .lit_test_times.txt recording.

# RUN: %{lit-no-order-opt} --skip-test-time-recording %{inputs}/time-tests
# RUN: not ls %{inputs}/time-tests/.lit_test_times.txt

## Check that --time-tests generates a printed histogram.

# RUN: %{lit-no-order-opt} --time-tests %{inputs}/time-tests > %t.out
# RUN: FileCheck < %t.out %s
# RUN: rm %{inputs}/time-tests/.lit_test_times.txt

# CHECK:      Tests Times:
# CHECK-NEXT: --------------------------------------------------------------------------
# CHECK-NEXT: [    Range    ] :: [               Percentage               ] :: [Count]
# CHECK-NEXT: --------------------------------------------------------------------------
