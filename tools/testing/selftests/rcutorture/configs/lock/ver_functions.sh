#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Kernel-version-dependent shell functions for the rest of the scripts.
#
# Copyright (C) IBM Corporation, 2014
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

# locktorture_param_onoff bootparam-string config-file
#
# Adds onoff locktorture module parameters to kernels having it.
locktorture_param_onoff () {
	if ! bootparam_hotplug_cpu "$1" && configfrag_hotplug_cpu "$2"
	then
		echo CPU-hotplug kernel, adding locktorture onoff. 1>&2
		echo locktorture.onoff_interval=3 locktorture.onoff_holdoff=30
	fi
}

# per_version_boot_params bootparam-string config-file seconds
#
# Adds per-version torture-module parameters to kernels supporting them.
per_version_boot_params () {
	echo $1 `locktorture_param_onoff "$1" "$2"` \
		locktorture.stat_interval=15 \
		locktorture.shutdown_secs=$3 \
		locktorture.verbose=1
}
