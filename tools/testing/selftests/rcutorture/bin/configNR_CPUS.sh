#!/bin/bash
#
# Extract the number of CPUs expected from the specified Kconfig-file
# fragment by checking CONFIG_SMP and CONFIG_NR_CPUS.  If the specified
# file gives no clue, base the number on the number of idle CPUs on
# the system.
#
# Usage: configNR_CPUS.sh config-frag
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

cf=$1
if test ! -r $cf
then
	echo Unreadable config fragment $cf 1>&2
	exit -1
fi
if grep -q '^CONFIG_SMP=n$' $cf
then
	echo 1
	exit 0
fi
if grep -q '^CONFIG_NR_CPUS=' $cf
then
	grep '^CONFIG_NR_CPUS=' $cf | 
		sed -e 's/^CONFIG_NR_CPUS=\([0-9]*\).*$/\1/'
	exit 0
fi
cpus2use.sh
