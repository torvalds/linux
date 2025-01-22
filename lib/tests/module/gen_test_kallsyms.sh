#!/usr/bin/env bash

TARGET=$(basename $1)
DIR=lib/tests/module
TARGET="$DIR/$TARGET"
NUM_SYMS=$2
SCALE_FACTOR=$3
TEST_TYPE=$(echo $TARGET | sed -e 's|lib/tests/module/test_kallsyms_||g')
TEST_TYPE=$(echo $TEST_TYPE | sed -e 's|.c||g')
FIRST_B_LOOKUP=1

if [[ $NUM_SYMS -gt 2 ]]; then
	FIRST_B_LOOKUP=$((NUM_SYMS/2))
fi

gen_template_module_header()
{
	cat <<____END_MODULE
// SPDX-License-Identifier: GPL-2.0-or-later OR copyleft-next-0.3.1
/*
 * Copyright (C) 2023 Luis Chamberlain <mcgrof@kernel.org>
 *
 * Automatically generated code for testing, do not edit manually.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

____END_MODULE
}

gen_num_syms()
{
	PREFIX=$1
	NUM=$2
	for i in $(seq 1 $NUM); do
		printf "int auto_test_%s_%010d = 0;\n" $PREFIX $i
		printf "EXPORT_SYMBOL_GPL(auto_test_%s_%010d);\n" $PREFIX $i
	done
	echo
}

gen_template_module_data_a()
{
	gen_num_syms a $1
	cat <<____END_MODULE
static int auto_runtime_test(void)
{
	return 0;
}

____END_MODULE
}

gen_template_module_data_b()
{
	printf "\nextern int auto_test_a_%010d;\n\n" $FIRST_B_LOOKUP
	echo "static int auto_runtime_test(void)"
	echo "{"
	printf "\nreturn auto_test_a_%010d;\n" $FIRST_B_LOOKUP
	echo "}"
}

gen_template_module_data_c()
{
	gen_num_syms c $1
	cat <<____END_MODULE
static int auto_runtime_test(void)
{
	return 0;
}

____END_MODULE
}

gen_template_module_data_d()
{
	gen_num_syms d $1
	cat <<____END_MODULE
static int auto_runtime_test(void)
{
	return 0;
}

____END_MODULE
}

gen_template_module_exit()
{
	cat <<____END_MODULE
static int __init auto_test_module_init(void)
{
	return auto_runtime_test();
}
module_init(auto_test_module_init);

static void __exit auto_test_module_exit(void)
{
}
module_exit(auto_test_module_exit);

MODULE_AUTHOR("Luis Chamberlain <mcgrof@kernel.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Test module for kallsyms");
____END_MODULE
}

case $TEST_TYPE in
	a)
		gen_template_module_header > $TARGET
		gen_template_module_data_a $NUM_SYMS >> $TARGET
		gen_template_module_exit >> $TARGET
		;;
	b)
		gen_template_module_header > $TARGET
		gen_template_module_data_b >> $TARGET
		gen_template_module_exit >> $TARGET
		;;
	c)
		gen_template_module_header > $TARGET
		gen_template_module_data_c $((NUM_SYMS * SCALE_FACTOR)) >> $TARGET
		gen_template_module_exit >> $TARGET
		;;
	d)
		gen_template_module_header > $TARGET
		gen_template_module_data_d $((NUM_SYMS * SCALE_FACTOR * 2)) >> $TARGET
		gen_template_module_exit >> $TARGET
		;;
	*)
		;;
esac
