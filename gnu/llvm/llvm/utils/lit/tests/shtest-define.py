# We're using DEFINE/REDEFINE to help us write tests for DEFINE/REDEFINE.

# RUN: echo '-- Available Tests --' > %t.tests.actual.txt

# DEFINE: %{my-inputs} = %{inputs}/shtest-define

# DEFINE: %{test} =
# DEFINE: %{lit-pre} =
# DEFINE: %{lit-args} =
# DEFINE: %{fc-args} =
# DEFINE: %{run-test} =                                                        \
# DEFINE:   %{lit-pre} %{lit} -va  %{lit-args} %{my-inputs}/%{test} 2>&1 |     \
# DEFINE:     FileCheck -match-full-lines %{fc-args} %{my-inputs}/%{test}      \
# DEFINE:               -dump-input-filter=all -vv -color
# DEFINE: %{record-test} =                                                     \
# DEFINE:   echo '  shtest-define :: %{test}' >> %t.tests.actual.txt
# DEFINE: %{run-and-record-test} = %{run-test} && %{record-test}

# REDEFINE: %{lit-pre} = not
#
# REDEFINE: %{test} = errors/assignment/before-name.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/assignment/between-name-equals.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/assignment/braces-empty.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/assignment/braces-with-dot.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/assignment/braces-with-equals.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/assignment/braces-with-newline.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/assignment/braces-with-number.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/assignment/braces-with-ws.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/assignment/empty.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/assignment/no-equals.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/assignment/no-name.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/assignment/ws-only.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/continuation/empty.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/continuation/end-in-double-backslash.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/continuation/unterminated-define-bad-redefine.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/continuation/unterminated-define-continuation.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/continuation/unterminated-define-redefine.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/continuation/unterminated-define-run.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/continuation/unterminated-define.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/continuation/unterminated-redefine-bad-define.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/continuation/unterminated-redefine-continuation.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/continuation/unterminated-redefine-define.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/continuation/unterminated-redefine-run.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/continuation/unterminated-redefine.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/continuation/unterminated-run-define.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/continuation/unterminated-run-redefine.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/continuation/ws-only.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/defined-check/define-already-by-config.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/defined-check/define-already-by-test.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/defined-check/define-inside-pattern.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/defined-check/define-multiple-exact.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/defined-check/define-multiple-once-exact.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/defined-check/define-prefixes-pattern.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/defined-check/define-suffixes-pattern.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/defined-check/redefine-inside-pattern.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/defined-check/redefine-multiple-exact.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/defined-check/redefine-multiple-once-exact.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/defined-check/redefine-none.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/defined-check/redefine-prefixes-pattern.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/defined-check/redefine-suffixes-pattern.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/location-range.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{test} = errors/no-run.txt
# RUN: %{run-and-record-test}
#
# REDEFINE: %{lit-pre} =

# REDEFINE: %{test} = examples/param-subst.txt
# RUN: %{run-and-record-test}

# REDEFINE: %{test} = expansion-order.txt
# RUN: %{run-and-record-test}

# REDEFINE: %{test} = line-number-substitutions.txt
# RUN: %{run-and-record-test}

# REDEFINE: %{test} = name-chars.txt
# RUN: %{run-and-record-test}

# REDEFINE: %{test} = recursiveExpansionLimit.txt
#
# REDEFINE: %{fc-args} = -check-prefix=CHECK-NON-RECUR
# RUN: %{run-test}
#
# REDEFINE: %{lit-args} = -Drecur=2
# REDEFINE: %{fc-args} = -check-prefix=CHECK-RECUR
# RUN: %{run-test}
#
# RUN: %{record-test}
# REDEFINE: %{lit-args} =
# REDEFINE: %{fc-args} =

# Check that per-test changes to substitutions don't affect other tests in the
# same LIT invocation.
#
# RUN: %{lit} -va %{my-inputs}/shared-substs-*.txt 2>&1 |                      \
# RUN:   FileCheck -check-prefix=SHARED-SUBSTS -match-full-lines %s
#
# SHARED-SUBSTS:# | shared-substs-0.txt
# SHARED-SUBSTS:# | GLOBAL: World
# SHARED-SUBSTS:# | LOCAL0: LOCAL0:Hello LOCAL0:World
# SHARED-SUBSTS:# | LOCAL0: subst
#
# SHARED-SUBSTS:# | shared-substs-1.txt
# SHARED-SUBSTS:# | GLOBAL: World
# SHARED-SUBSTS:# | LOCAL1: LOCAL1:Hello LOCAL1:World
# SHARED-SUBSTS:# | LOCAL1: subst
#
# REDEFINE: %{test} = shared-substs-0.txt
# RUN: %{record-test}
# REDEFINE: %{test} = shared-substs-1.txt
# RUN: %{record-test}

# REDEFINE: %{test} = value-equals.txt
# RUN: %{run-and-record-test}

# REDEFINE: %{test} = value-escaped.txt
# RUN: %{run-and-record-test}

# REDEFINE: %{fc-args} = -strict-whitespace
# REDEFINE: %{test} = ws-and-continuations.txt
# RUN: %{run-and-record-test}
# REDEFINE: %{fc-args} =

# Make sure we didn't forget to run something.
#
# RUN: %{lit} --show-tests %{my-inputs} > %t.tests.expected.txt
# RUN: diff -u -w %t.tests.expected.txt %t.tests.actual.txt
