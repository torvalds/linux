#!/bin/bash
#
# Shell functions for the rest of the scripts.
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
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#
# Copyright (C) IBM Corporation, 2013
#
# Authors: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

# bootparam_hotplug_cpu bootparam-string
#
# Returns 1 if the specified boot-parameter string tells rcutorture to
# test CPU-hotplug operations.
bootparam_hotplug_cpu () {
	echo "$1" | grep -q "rcutorture\.onoff_"
}

# configfrag_hotplug_cpu config-fragment-file
#
# Returns 1 if the config fragment specifies hotplug CPU.
configfrag_hotplug_cpu () {
	cf=$1
	if test ! -r $cf
	then
		echo Unreadable config fragment $cf 1>&2
		exit -1
	fi
	grep -q '^CONFIG_HOTPLUG_CPU=y$' $cf
}
