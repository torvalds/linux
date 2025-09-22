# Test LLVM_PROFILE_FILE is set when --per-test-coverage is passed to command line.

# RUN: %{lit} -a -vv --per-test-coverage -Dexecute_external=False \
# RUN:     %{inputs}/per-test-coverage/per-test-coverage.py | \
# RUN:   FileCheck -DOUT=stdout %s

# RUN: %{lit} -a -vv --per-test-coverage -Dexecute_external=True \
# RUN:        %{inputs}/per-test-coverage/per-test-coverage.py | \
# RUN:   FileCheck -DOUT=stderr %s

#      CHECK: {{^}}PASS: per-test-coverage :: per-test-coverage.py ({{[^)]*}})
#      CHECK: Command Output ([[OUT]]):
# CHECK-NEXT: --
#      CHECK: export
#      CHECK: LLVM_PROFILE_FILE=per-test-coverage0.profraw
#      CHECK: per-test-coverage.py
#      CHECK: {{RUN}}: at line 2
#      CHECK: export
#      CHECK: LLVM_PROFILE_FILE=per-test-coverage1.profraw
#      CHECK: per-test-coverage.py
#      CHECK: {{RUN}}: at line 3
#      CHECK: export
#      CHECK: LLVM_PROFILE_FILE=per-test-coverage2.profraw
#      CHECK: per-test-coverage.py
