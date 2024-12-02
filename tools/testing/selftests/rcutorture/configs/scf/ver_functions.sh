#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Torture-suite-dependent shell functions for the rest of the scripts.
#
# Copyright (C) Facebook, 2020
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

# scftorture_param_onoff bootparam-string config-file
#
# Adds onoff scftorture module parameters to kernels having it.
scftorture_param_onoff () {
	if ! bootparam_hotplug_cpu "$1" && configfrag_hotplug_cpu "$2"
	then
		echo CPU-hotplug kernel, adding scftorture onoff. 1>&2
		echo scftorture.onoff_interval=1000 scftorture.onoff_holdoff=30
	fi
}

# per_version_boot_params bootparam-string config-file seconds
#
# Adds per-version torture-module parameters to kernels supporting them.
per_version_boot_params () {
	echo $1 `scftorture_param_onoff "$1" "$2"` \
		scftorture.stat_interval=15 \
		scftorture.shutdown_secs=$3 \
		scftorture.verbose=1
}
