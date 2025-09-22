// RUN: %clang --target=aarch64-none-gnu -mlr-for-calls-only -### %s 2> %t
// RUN: FileCheck --check-prefix=CHECK < %t %s
// CHECK: "-target-feature" "+reserve-lr-for-ra"
