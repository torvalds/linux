#!/bin/bash
#
# Kernel-version-dependent shell functions for the rest of the scripts.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, you can access it online at
# http://www.gnu.org/licenses/gpl-2.0.html.
#
# Copyright (C) IBM Corporation, 2014
#
# Authors: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

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
		locktorture.locktorture_runnable=1 \
		locktorture.verbose=1
}
