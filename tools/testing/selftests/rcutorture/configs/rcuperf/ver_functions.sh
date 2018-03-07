#!/bin/bash
#
# Torture-suite-dependent shell functions for the rest of the scripts.
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
# Copyright (C) IBM Corporation, 2015
#
# Authors: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

# rcuperf_param_nreaders bootparam-string
#
# Adds nreaders rcuperf module parameter if not already specified.
rcuperf_param_nreaders () {
	if ! echo "$1" | grep -q "rcuperf.nreaders"
	then
		echo rcuperf.nreaders=-1
	fi
}

# rcuperf_param_nwriters bootparam-string
#
# Adds nwriters rcuperf module parameter if not already specified.
rcuperf_param_nwriters () {
	if ! echo "$1" | grep -q "rcuperf.nwriters"
	then
		echo rcuperf.nwriters=-1
	fi
}

# per_version_boot_params bootparam-string config-file seconds
#
# Adds per-version torture-module parameters to kernels supporting them.
per_version_boot_params () {
	echo $1 `rcuperf_param_nreaders "$1"` \
		`rcuperf_param_nwriters "$1"` \
		rcuperf.shutdown=1 \
		rcuperf.verbose=1
}
