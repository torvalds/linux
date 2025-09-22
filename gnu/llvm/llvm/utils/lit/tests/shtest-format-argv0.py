# Check that we route argv[0] as it was written, instead of the resolved
# path. This is important for some tools, in particular '[' which at least on OS
# X only recognizes that it is in '['-mode when its argv[0] is exactly
# '['. Otherwise it will refuse to accept the trailing closing bracket.
#
# This test is not supported on AIX since `[` is only available as a shell builtin
# and is not installed under PATH by default.
# UNSUPPORTED: system-aix
#
# RUN: %{lit} -v %{inputs}/shtest-format-argv0 | FileCheck %s

# CHECK: -- Testing:
# CHECK: PASS: shtest-format-argv0 :: argv0.txt
