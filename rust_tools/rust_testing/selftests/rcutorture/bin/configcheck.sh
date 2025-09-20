#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Usage: configcheck.sh .config .config-template
#
# Non-empty output if errors detected.
#
# Copyright (C) IBM Corporation, 2011
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

T="`mktemp -d ${TMPDIR-/tmp}/configcheck.sh.XXXXXX`"
trap 'rm -rf $T' 0

# function test_kconfig_enabled ( Kconfig-var=val )
function test_kconfig_enabled () {
	if ! grep -q "^$1$" $T/.config
	then
		echo :$1: improperly set
		return 1
	fi
	return 0
}

# function test_kconfig_disabled ( Kconfig-var )
function test_kconfig_disabled () {
	if grep -q "^$1=n$" $T/.config
	then
		return 0
	fi
	if grep -q "^$1=" $T/.config
	then
		echo :$1=n: improperly set
		return 1
	fi
	return 0
}

sed -e 's/"//g' < $1 > $T/.config
sed -e 's/^#CHECK#//' < $2 > $T/ConfigFragment
grep '^CONFIG_.*=n$' $T/ConfigFragment |
	sed -e 's/^/test_kconfig_disabled /' -e 's/=n$//' > $T/kconfig-n.sh
. $T/kconfig-n.sh
grep -v '^CONFIG_.*=n$' $T/ConfigFragment | grep '^CONFIG_' |
	sed -e 's/^/test_kconfig_enabled /' > $T/kconfig-not-n.sh
. $T/kconfig-not-n.sh
