#!/bin/bash
#
# Get an estimate of how CPU-hoggy to be.
#
# Usage: cpus2use.sh
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
# Copyright (C) IBM Corporation, 2013
#
# Authors: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

ncpus=`grep '^processor' /proc/cpuinfo | wc -l`
idlecpus=`mpstat | tail -1 | \
	awk -v ncpus=$ncpus '{ print ncpus * ($7 + $12) / 100 }'`
awk -v ncpus=$ncpus -v idlecpus=$idlecpus < /dev/null '
BEGIN {
	cpus2use = idlecpus;
	if (cpus2use < 1)
		cpus2use = 1;
	if (cpus2use < ncpus / 10)
		cpus2use = ncpus / 10;
	if (cpus2use == int(cpus2use))
		cpus2use = int(cpus2use)
	else
		cpus2use = int(cpus2use) + 1
	print cpus2use;
}'

