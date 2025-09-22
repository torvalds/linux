# REQUIRES: lit-max-individual-test-time

# llvm.org/PR33944
# UNSUPPORTED: system-windows

###############################################################################
# Check tests can hit timeout when set
###############################################################################

# Test per test timeout using external shell
# RUN: not %{lit} \
# RUN: %{inputs}/shtest-timeout/infinite_loop.py \
# RUN: -j 1 -v --debug --timeout 1 --param external=1 > %t.extsh.out 2> %t.extsh.err
# RUN: FileCheck --check-prefix=CHECK-OUT-COMMON < %t.extsh.out %s
# RUN: FileCheck --check-prefix=CHECK-EXTSH-ERR < %t.extsh.err %s
#
# CHECK-EXTSH-ERR: Using external shell

# Test per test timeout using internal shell
# RUN: not %{lit} \
# RUN: %{inputs}/shtest-timeout/infinite_loop.py \
# RUN: -j 1 -v --debug --timeout 1 --param external=0 > %t.intsh.out 2> %t.intsh.err
# RUN: FileCheck  --check-prefix=CHECK-OUT-COMMON < %t.intsh.out %s
# RUN: FileCheck --check-prefix=CHECK-INTSH-OUT < %t.intsh.out %s
# RUN: FileCheck --check-prefix=CHECK-INTSH-ERR < %t.intsh.err %s

# CHECK-INTSH-OUT: TIMEOUT: per_test_timeout :: infinite_loop.py
# CHECK-INTSH-OUT: command reached timeout: True

# CHECK-INTSH-ERR: Using internal shell

# Test per test timeout set via a config file rather than on the command line
# RUN: not %{lit} \
# RUN: %{inputs}/shtest-timeout/infinite_loop.py \
# RUN: -j 1 -v --debug --param external=0 \
# RUN: --param set_timeout=1 > %t.cfgset.out 2> %t.cfgset.err
# RUN: FileCheck --check-prefix=CHECK-OUT-COMMON  < %t.cfgset.out %s
# RUN: FileCheck --check-prefix=CHECK-CFGSET-ERR < %t.cfgset.err %s
#
# CHECK-CFGSET-ERR: Using internal shell

# CHECK-OUT-COMMON: TIMEOUT: per_test_timeout :: infinite_loop.py
# CHECK-OUT-COMMON: Timeout: Reached timeout of 1 seconds
# CHECK-OUT-COMMON: Timed Out: 1


###############################################################################
# Check tests can complete in with a timeout set
#
# `short.py` should execute quickly so we shouldn't wait anywhere near the
# 3600 second timeout.
###############################################################################

# Test per test timeout using external shell
# RUN: %{lit} \
# RUN: %{inputs}/shtest-timeout/short.py \
# RUN: -j 1 -v --debug --timeout 3600 --param external=1 > %t.pass.extsh.out 2> %t.pass.extsh.err
# RUN: FileCheck --check-prefix=CHECK-OUT-COMMON-SHORT < %t.pass.extsh.out %s
# RUN: FileCheck --check-prefix=CHECK-EXTSH-ERR < %t.pass.extsh.err %s

# Test per test timeout using internal shell
# RUN: %{lit} \
# RUN: %{inputs}/shtest-timeout/short.py \
# RUN: -j 1 -v --debug --timeout 3600 --param external=0 > %t.pass.intsh.out 2> %t.pass.intsh.err
# RUN: FileCheck  --check-prefix=CHECK-OUT-COMMON-SHORT < %t.pass.intsh.out %s
# RUN: FileCheck --check-prefix=CHECK-INTSH-ERR < %t.pass.intsh.err %s

# CHECK-OUT-COMMON-SHORT: PASS: per_test_timeout :: short.py
# CHECK-OUT-COMMON-SHORT: Passed: 1

# Test per test timeout via a config file and on the command line.
# The value set on the command line should override the config file.
# RUN: %{lit} \
# RUN:   %{inputs}/shtest-timeout/short.py \
# RUN:   -j 1 -v --debug --param external=0 \
# RUN: --param set_timeout=1 --timeout=3600 > %t.pass.cmdover.out 2> %t.pass.cmdover.err
# RUN: FileCheck --check-prefix=CHECK-OUT-COMMON-SHORT  < %t.pass.cmdover.out %s
# RUN: FileCheck --check-prefix=CHECK-CMDLINE-OVERRIDE-ERR < %t.pass.cmdover.err %s

# CHECK-CMDLINE-OVERRIDE-ERR: Forcing timeout to be 3600 seconds
