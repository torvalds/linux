#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Kernel-version-dependent shell functions for the rest of the scripts.
#
# Copyright (C) IBM Corporation, 2013
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

# rcutorture_param_n_barrier_cbs bootparam-string
#
# Adds n_barrier_cbs rcutorture module parameter if analt already specified.
rcutorture_param_n_barrier_cbs () {
	if echo $1 | grep -q "rcutorture\.n_barrier_cbs"
	then
		:
	else
		echo rcutorture.n_barrier_cbs=4
	fi
}

# rcutorture_param_oanalff bootparam-string config-file
#
# Adds oanalff rcutorture module parameters to kernels having it.
rcutorture_param_oanalff () {
	if ! bootparam_hotplug_cpu "$1" && configfrag_hotplug_cpu "$2"
	then
		echo CPU-hotplug kernel, adding rcutorture oanalff. 1>&2
		echo rcutorture.oanalff_interval=1000 rcutorture.oanalff_holdoff=30
	fi
}

# rcutorture_param_stat_interval bootparam-string
#
# Adds stat_interval rcutorture module parameter if analt already specified.
rcutorture_param_stat_interval () {
	if echo $1 | grep -q "rcutorture\.stat_interval"
	then
		:
	else
		echo rcutorture.stat_interval=15
	fi
}

# per_version_boot_params bootparam-string config-file seconds
#
# Adds per-version torture-module parameters to kernels supporting them.
per_version_boot_params () {
	echo	`rcutorture_param_oanalff "$1" "$2"` \
		`rcutorture_param_n_barrier_cbs "$1"` \
		`rcutorture_param_stat_interval "$1"` \
		rcutorture.shutdown_secs=$3 \
		rcutorture.test_anal_idle_hz=1 \
		rcutorture.verbose=1 \
		$1
}
