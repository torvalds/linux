# Test if lit_config.per_test_coverage in lit.cfg sets individual test case coverage.

# RUN: %{lit} -a -vv -Dexecute_external=False \
# RUN:     %{inputs}/per-test-coverage-by-lit-cfg/per-test-coverage-by-lit-cfg.py | \
# RUN:   FileCheck -DOUT=stdout %s

# RUN: %{lit} -a -vv -Dexecute_external=True \
# RUN:     %{inputs}/per-test-coverage-by-lit-cfg/per-test-coverage-by-lit-cfg.py | \
# RUN:   FileCheck -DOUT=stderr %s

#      CHECK: {{^}}PASS: per-test-coverage-by-lit-cfg :: per-test-coverage-by-lit-cfg.py ({{[^)]*}})
#      CHECK: Command Output ([[OUT]]):
# CHECK-NEXT: --
#      CHECK: export
#      CHECK: LLVM_PROFILE_FILE=per-test-coverage-by-lit-cfg0.profraw
#      CHECK: per-test-coverage-by-lit-cfg.py
#      CHECK: {{RUN}}: at line 2
#      CHECK: export
#      CHECK: LLVM_PROFILE_FILE=per-test-coverage-by-lit-cfg1.profraw
#      CHECK: per-test-coverage-by-lit-cfg.py
#      CHECK: {{RUN}}: at line 3
#      CHECK: export
#      CHECK: LLVM_PROFILE_FILE=per-test-coverage-by-lit-cfg2.profraw
#      CHECK: per-test-coverage-by-lit-cfg.py
