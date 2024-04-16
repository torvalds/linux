#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2018 Joe Lawrence <joe.lawrence@redhat.com>

. $(dirname $0)/functions.sh

MOD_TEST=test_klp_shadow_vars

setup_config


# - load a module that exercises the shadow variable API

start_test "basic shadow variable API"

load_mod $MOD_TEST
unload_mod $MOD_TEST

check_result "% modprobe $MOD_TEST
$MOD_TEST: klp_shadow_get(obj=PTR1, id=0x1234) = PTR0
$MOD_TEST:   got expected NULL result
$MOD_TEST: shadow_ctor: PTR3 -> PTR2
$MOD_TEST: klp_shadow_get_or_alloc(obj=PTR1, id=0x1234, size=8, gfp_flags=GFP_KERNEL), ctor=PTR4, ctor_data=PTR2 = PTR3
$MOD_TEST: shadow_ctor: PTR6 -> PTR5
$MOD_TEST: klp_shadow_alloc(obj=PTR1, id=0x1235, size=8, gfp_flags=GFP_KERNEL), ctor=PTR4, ctor_data=PTR5 = PTR6
$MOD_TEST: shadow_ctor: PTR8 -> PTR7
$MOD_TEST: klp_shadow_alloc(obj=PTR9, id=0x1234, size=8, gfp_flags=GFP_KERNEL), ctor=PTR4, ctor_data=PTR7 = PTR8
$MOD_TEST: shadow_ctor: PTR11 -> PTR10
$MOD_TEST: klp_shadow_alloc(obj=PTR9, id=0x1235, size=8, gfp_flags=GFP_KERNEL), ctor=PTR4, ctor_data=PTR10 = PTR11
$MOD_TEST: shadow_ctor: PTR13 -> PTR12
$MOD_TEST: klp_shadow_get_or_alloc(obj=PTR14, id=0x1234, size=8, gfp_flags=GFP_KERNEL), ctor=PTR4, ctor_data=PTR12 = PTR13
$MOD_TEST: shadow_ctor: PTR16 -> PTR15
$MOD_TEST: klp_shadow_alloc(obj=PTR14, id=0x1235, size=8, gfp_flags=GFP_KERNEL), ctor=PTR4, ctor_data=PTR15 = PTR16
$MOD_TEST: klp_shadow_get(obj=PTR1, id=0x1234) = PTR3
$MOD_TEST:   got expected PTR3 -> PTR2 result
$MOD_TEST: klp_shadow_get(obj=PTR1, id=0x1235) = PTR6
$MOD_TEST:   got expected PTR6 -> PTR5 result
$MOD_TEST: klp_shadow_get(obj=PTR9, id=0x1234) = PTR8
$MOD_TEST:   got expected PTR8 -> PTR7 result
$MOD_TEST: klp_shadow_get(obj=PTR9, id=0x1235) = PTR11
$MOD_TEST:   got expected PTR11 -> PTR10 result
$MOD_TEST: klp_shadow_get(obj=PTR14, id=0x1234) = PTR13
$MOD_TEST:   got expected PTR13 -> PTR12 result
$MOD_TEST: klp_shadow_get(obj=PTR14, id=0x1235) = PTR16
$MOD_TEST:   got expected PTR16 -> PTR15 result
$MOD_TEST: klp_shadow_get_or_alloc(obj=PTR1, id=0x1234, size=8, gfp_flags=GFP_KERNEL), ctor=PTR4, ctor_data=PTR2 = PTR3
$MOD_TEST:   got expected PTR3 -> PTR2 result
$MOD_TEST: klp_shadow_get_or_alloc(obj=PTR9, id=0x1234, size=8, gfp_flags=GFP_KERNEL), ctor=PTR4, ctor_data=PTR7 = PTR8
$MOD_TEST:   got expected PTR8 -> PTR7 result
$MOD_TEST: klp_shadow_get_or_alloc(obj=PTR14, id=0x1234, size=8, gfp_flags=GFP_KERNEL), ctor=PTR4, ctor_data=PTR12 = PTR13
$MOD_TEST:   got expected PTR13 -> PTR12 result
$MOD_TEST: shadow_dtor(obj=PTR1, shadow_data=PTR3)
$MOD_TEST: klp_shadow_free(obj=PTR1, id=0x1234, dtor=PTR17)
$MOD_TEST: klp_shadow_get(obj=PTR1, id=0x1234) = PTR0
$MOD_TEST:   got expected NULL result
$MOD_TEST: shadow_dtor(obj=PTR9, shadow_data=PTR8)
$MOD_TEST: klp_shadow_free(obj=PTR9, id=0x1234, dtor=PTR17)
$MOD_TEST: klp_shadow_get(obj=PTR9, id=0x1234) = PTR0
$MOD_TEST:   got expected NULL result
$MOD_TEST: shadow_dtor(obj=PTR14, shadow_data=PTR13)
$MOD_TEST: klp_shadow_free(obj=PTR14, id=0x1234, dtor=PTR17)
$MOD_TEST: klp_shadow_get(obj=PTR14, id=0x1234) = PTR0
$MOD_TEST:   got expected NULL result
$MOD_TEST: klp_shadow_get(obj=PTR1, id=0x1235) = PTR6
$MOD_TEST:   got expected PTR6 -> PTR5 result
$MOD_TEST: klp_shadow_get(obj=PTR9, id=0x1235) = PTR11
$MOD_TEST:   got expected PTR11 -> PTR10 result
$MOD_TEST: klp_shadow_get(obj=PTR14, id=0x1235) = PTR16
$MOD_TEST:   got expected PTR16 -> PTR15 result
$MOD_TEST: klp_shadow_free_all(id=0x1235, dtor=PTR0)
$MOD_TEST: klp_shadow_get(obj=PTR1, id=0x1235) = PTR0
$MOD_TEST:   got expected NULL result
$MOD_TEST: klp_shadow_get(obj=PTR9, id=0x1235) = PTR0
$MOD_TEST:   got expected NULL result
$MOD_TEST: klp_shadow_get(obj=PTR14, id=0x1235) = PTR0
$MOD_TEST:   got expected NULL result
% rmmod $MOD_TEST"

exit 0
