#!/bin/sh
# Usage: sh config2frag.sh < .config > configfrag
#
# Converts the "# CONFIG_XXX is not set" to "CONFIG_XXX=n" so that the
# resulting file becomes a legitimate Kconfig fragment.
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

LANG=C sed -e 's/^# CONFIG_\([a-zA-Z0-9_]*\) is not set$/CONFIG_\1=n/'
