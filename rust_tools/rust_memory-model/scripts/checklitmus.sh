#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Invokes runlitmus.sh and judgelitmus.sh on its arguments to run the
# specified litmus test and pass judgment on the results.
#
# Usage:
#	checklitmus.sh file.litmus
#
# Run this in the directory containing the memory model, specifying the
# pathname of the litmus test to check.  The caller is expected to have
# properly set up the LKMM environment variables.
#
# Copyright IBM Corporation, 2018
#
# Author: Paul E. McKenney <paulmck@linux.ibm.com>

scripts/runlitmus.sh $1
scripts/judgelitmus.sh $1
