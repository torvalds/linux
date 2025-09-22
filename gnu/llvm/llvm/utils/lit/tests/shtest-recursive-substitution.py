# Check that the config.recursiveExpansionLimit is picked up and will cause
# lit substitutions to be expanded recursively.

# RUN: %{lit} %{inputs}/shtest-recursive-substitution/substitutes-within-limit --show-all | FileCheck --check-prefix=CHECK-TEST1 %s
# CHECK-TEST1: PASS: substitutes-within-limit :: test.py
# CHECK-TEST1: echo STOP

# RUN: not %{lit} %{inputs}/shtest-recursive-substitution/does-not-substitute-within-limit --show-all | FileCheck --check-prefix=CHECK-TEST2 %s
# CHECK-TEST2: UNRESOLVED: does-not-substitute-within-limit :: test.py
# CHECK-TEST2: ValueError: Recursive substitution of

# RUN: %{lit} %{inputs}/shtest-recursive-substitution/does-not-substitute-no-limit --show-all | FileCheck --check-prefix=CHECK-TEST3 %s
# CHECK-TEST3: PASS: does-not-substitute-no-limit :: test.py
# CHECK-TEST3: echo %rec4

# RUN: not %{lit} %{inputs}/shtest-recursive-substitution/not-an-integer --show-all 2>&1 | FileCheck --check-prefix=CHECK-TEST4 %s
# CHECK-TEST4: recursiveExpansionLimit must be either None or an integer

# RUN: not %{lit} %{inputs}/shtest-recursive-substitution/negative-integer --show-all 2>&1 | FileCheck --check-prefix=CHECK-TEST5 %s
# CHECK-TEST5: recursiveExpansionLimit must be a non-negative integer

# RUN: %{lit} %{inputs}/shtest-recursive-substitution/set-to-none --show-all | FileCheck --check-prefix=CHECK-TEST6 %s
# CHECK-TEST6: PASS: set-to-none :: test.py

# RUN: %{lit} %{inputs}/shtest-recursive-substitution/escaping --show-all | FileCheck --check-prefix=CHECK-TEST7 %s
# CHECK-TEST7: PASS: escaping :: test.py
# CHECK-TEST7: echo %s %s %%s
