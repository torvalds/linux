# RUN: not %{lit} -vv %{inputs}/shtest-keyword-parse-errors > %t.out
# RUN: FileCheck -input-file %t.out %s
#
# END.

# CHECK: Testing: 3 tests

# CHECK-LABEL: UNRESOLVED: shtest-keyword-parse-errors :: empty.txt
# CHECK:       {{^}}Test has no 'RUN:' line{{$}}

# CHECK-LABEL: UNRESOLVED: shtest-keyword-parse-errors :: multiple-allow-retries.txt
# CHECK:       {{^}}Test has more than one ALLOW_RETRIES lines{{$}}

# CHECK-LABEL: UNRESOLVED: shtest-keyword-parse-errors :: unterminated-run.txt
# CHECK:       {{^}}Test has unterminated 'RUN:' directive (with '\') at line 1{{$}}
