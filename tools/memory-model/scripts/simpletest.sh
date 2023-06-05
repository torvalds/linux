#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Give zero status if this is a simple test and non-zero otherwise.
# Simple tests do not contain locking, RCU, or SRCU.
#
# Usage:
#	simpletest.sh file.litmus
#
# Copyright IBM Corporation, 2019
#
# Author: Paul E. McKenney <paulmck@linux.ibm.com>


litmus=$1

if test -f "$litmus" -a -r "$litmus"
then
	:
else
	echo ' --- ' error: \"$litmus\" is not a readable file
	exit 255
fi
exclude="^[[:space:]]*\("
exclude="${exclude}spin_lock(\|spin_unlock(\|spin_trylock(\|spin_is_locked("
exclude="${exclude}\|rcu_read_lock(\|rcu_read_unlock("
exclude="${exclude}\|synchronize_rcu(\|synchronize_rcu_expedited("
exclude="${exclude}\|srcu_read_lock(\|srcu_read_unlock("
exclude="${exclude}\|synchronize_srcu(\|synchronize_srcu_expedited("
exclude="${exclude}\)"
if grep -q $exclude $litmus
then
	exit 255
fi
exit 0
