## Show that lit reports the path of tools found via use_llvm_tool.
## Additionally show that use_llvm_tool uses in order of preference:
## 1) The path specified in an environment variable,
## 2) The LLVM tools build directory,
## 3) The PATH, if requested.

# RUN: %{lit} %{inputs}/use-llvm-tool 2>&1 | \
# RUN:   FileCheck %s -DDIR=%p

## The exact breakdown of cases is:
## Case | Env | Build Dir | PATH |
##   1  |  /  |     X     |  N/S | <- Can be found via env
##   2  |  X  |     /     |  N/S | <- Can be found via build dir if env specified
##   3  | N/S |     /     |  N/S | <- Can be found via build dir
##   4  | N/S |     X     |   /  | <- Can be found via PATH, if requested
##   5  | N/S |     X     |  N/S | <- Cannot be found via PATH, if not requested
##   6  |  /  |     /     |   /  | <- Env is preferred over build, PATH
##   7  | N/S |     /     |   /  | <- Build dir is preferred over PATH
##   8  |  X  |     X     |   X  | <- Say nothing if cannot be found if not required
##   9  | N/S |  override |  N/S | <- Use specified search directory, instead of default directory
##  10  | N/S |  override |   /  | <- Use PATH if not in search directory

## Check the exact path reported for the first case, but don't bother for the
## others.
# CHECK:      note: using case1: [[DIR]]{{[\\/]}}Inputs{{[\\/]}}use-llvm-tool{{[\\/]}}env-case1
# CHECK-NEXT: note: using case2: {{.*}}build{{[\\/]}}case2
# CHECK-NEXT: note: using case3: {{.*}}build{{[\\/]}}case3
# CHECK-NEXT: note: using case4: {{.*}}path{{[\\/]}}case4
# CHECK-NOT:  case5
# CHECK-NEXT: note: using case6: {{.*}}env-case6
# CHECK-NEXT: note: using case7: {{.*}}build{{[\\/]}}case7
# CHECK-NOT:  case8
# CHECK-NEXT: note: using case9: {{.*}}search2{{[\\/]}}case9
# CHECK-NEXT: note: using case10: {{.*}}path{{[\\/]}}case10

## Test that if required is True, lit errors if the tool is not found.
# RUN: not %{lit} %{inputs}/use-llvm-tool-required 2>&1 | \
# RUN:   FileCheck %s --check-prefix=ERROR
# ERROR:      note: using found: {{.*}}found
# ERROR-NEXT: fatal: couldn't find 'not-found' program
