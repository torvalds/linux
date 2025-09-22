# Check that the environment variable is set correctly
# RUN: %{python} %s | FileCheck -DINDEX=1 %s
# RUN: %{python} %s | FileCheck -DINDEX=2 %s

# Python script to read the environment variable
# and print its value
import os

llvm_profile_file = os.environ.get('LLVM_PROFILE_FILE')
print(llvm_profile_file)

# CHECK: per-test-coverage-by-lit-cfg[[INDEX]].profraw
