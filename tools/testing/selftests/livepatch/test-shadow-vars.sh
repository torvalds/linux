#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2018 Joe Lawrence <joe.lawrence@redhat.com>

. $(dirname $0)/functions.sh

MOD_TEST=test_klp_shadow_vars

setup_config


# TEST: basic shadow variable API
# - load a module that exercises the shadow variable API

echo -n "TEST: basic shadow variable API ... "
dmesg -C

load_mod $MOD_TEST
unload_mod $MOD_TEST

check_result "% modprobe $MOD_TEST
$MOD_TEST: klp_shadow_get(obj=PTR5, id=0x1234) = PTR0
$MOD_TEST:   got expected NULL result
$MOD_TEST: shadow_ctor: PTR6 -> PTR1
$MOD_TEST: klp_shadow_alloc(obj=PTR5, id=0x1234, size=8, gfp_flags=GFP_KERNEL), ctor=PTR7, ctor_data=PTR1 = PTR6
$MOD_TEST: shadow_ctor: PTR8 -> PTR2
$MOD_TEST: klp_shadow_alloc(obj=PTR9, id=0x1234, size=8, gfp_flags=GFP_KERNEL), ctor=PTR7, ctor_data=PTR2 = PTR8
$MOD_TEST: shadow_ctor: PTR10 -> PTR3
$MOD_TEST: klp_shadow_alloc(obj=PTR5, id=0x1235, size=8, gfp_flags=GFP_KERNEL), ctor=PTR7, ctor_data=PTR3 = PTR10
$MOD_TEST: klp_shadow_get(obj=PTR5, id=0x1234) = PTR6
$MOD_TEST:   got expected PTR6 -> PTR1 result
$MOD_TEST: klp_shadow_get(obj=PTR9, id=0x1234) = PTR8
$MOD_TEST:   got expected PTR8 -> PTR2 result
$MOD_TEST: klp_shadow_get(obj=PTR5, id=0x1235) = PTR10
$MOD_TEST:   got expected PTR10 -> PTR3 result
$MOD_TEST: shadow_ctor: PTR11 -> PTR4
$MOD_TEST: klp_shadow_get_or_alloc(obj=PTR12, id=0x1234, size=8, gfp_flags=GFP_KERNEL), ctor=PTR7, ctor_data=PTR4 = PTR11
$MOD_TEST: klp_shadow_get_or_alloc(obj=PTR12, id=0x1234, size=8, gfp_flags=GFP_KERNEL), ctor=PTR7, ctor_data=PTR4 = PTR11
$MOD_TEST:   got expected PTR11 -> PTR4 result
$MOD_TEST: shadow_dtor(obj=PTR5, shadow_data=PTR6)
$MOD_TEST: klp_shadow_free(obj=PTR5, id=0x1234, dtor=PTR13)
$MOD_TEST: klp_shadow_get(obj=PTR5, id=0x1234) = PTR0
$MOD_TEST:   got expected NULL result
$MOD_TEST: shadow_dtor(obj=PTR9, shadow_data=PTR8)
$MOD_TEST: klp_shadow_free(obj=PTR9, id=0x1234, dtor=PTR13)
$MOD_TEST: klp_shadow_get(obj=PTR9, id=0x1234) = PTR0
$MOD_TEST:   got expected NULL result
$MOD_TEST: shadow_dtor(obj=PTR12, shadow_data=PTR11)
$MOD_TEST: klp_shadow_free(obj=PTR12, id=0x1234, dtor=PTR13)
$MOD_TEST: klp_shadow_get(obj=PTR12, id=0x1234) = PTR0
$MOD_TEST:   got expected NULL result
$MOD_TEST: klp_shadow_get(obj=PTR5, id=0x1235) = PTR10
$MOD_TEST:   got expected PTR10 -> PTR3 result
$MOD_TEST: shadow_dtor(obj=PTR5, shadow_data=PTR10)
$MOD_TEST: klp_shadow_free_all(id=0x1235, dtor=PTR13)
$MOD_TEST: klp_shadow_get(obj=PTR5, id=0x1234) = PTR0
$MOD_TEST:   shadow_get() got expected NULL result
% rmmod test_klp_shadow_vars"

exit 0
