# RUN: %{lit} -v --show-all %{inputs}/shtest-if-else/test.txt \
# RUN:    | FileCheck %{inputs}/shtest-if-else/test.txt --match-full-lines \
# RUN:                --implicit-check-not='RUN:'

# RUN: not %{lit} -v --show-all %{inputs}/shtest-if-else/test-neg1.txt 2>&1 \
# RUN:    | FileCheck %{inputs}/shtest-if-else/test-neg1.txt

# RUN: not %{lit} -v --show-all %{inputs}/shtest-if-else/test-neg2.txt 2>&1 \
# RUN:    | FileCheck %{inputs}/shtest-if-else/test-neg2.txt

# RUN: not %{lit} -v --show-all %{inputs}/shtest-if-else/test-neg3.txt 2>&1 \
# RUN:    | FileCheck %{inputs}/shtest-if-else/test-neg3.txt

# RUN: not %{lit} -v --show-all %{inputs}/shtest-if-else/test-neg4.txt 2>&1 \
# RUN:    | FileCheck %{inputs}/shtest-if-else/test-neg4.txt
