#!/usr/bin/env drgn
#
# Copyright (C) 2024 Tejun Heo <tj@kernel.org>
# Copyright (C) 2024 Meta Platforms, Inc. and affiliates.

desc = """
This is a drgn script to show the current sched_ext state.
For more info on drgn, visit https://github.com/osandov/drgn.
"""

import drgn
import sys

def err(s):
    print(s, file=sys.stderr, flush=True)
    sys.exit(1)

def read_int(name):
    return int(prog[name].value_())

def read_atomic(name):
    return prog[name].counter.value_()

def read_static_key(name):
    return prog[name].key.enabled.counter.value_()

def state_str(state):
    return prog['scx_enable_state_str'][state].string_().decode()

ops = prog['scx_ops']
enable_state = read_atomic("scx_enable_state_var")

print(f'ops           : {ops.name.string_().decode()}')
print(f'enabled       : {read_static_key("__scx_enabled")}')
print(f'switching_all : {read_int("scx_switching_all")}')
print(f'switched_all  : {read_static_key("__scx_switched_all")}')
print(f'enable_state  : {state_str(enable_state)} ({enable_state})')
print(f'in_softlockup : {prog["scx_in_softlockup"].value_()}')
print(f'breather_depth: {read_atomic("scx_breather_depth")}')
print(f'bypass_depth  : {prog["scx_bypass_depth"].value_()}')
print(f'nr_rejected   : {read_atomic("scx_nr_rejected")}')
print(f'enable_seq    : {read_atomic("scx_enable_seq")}')
