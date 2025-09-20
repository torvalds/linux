#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Create a testid.txt file in the specified directory.
#
# Usage: mktestid.sh dirpath
#
# Copyright (C) Meta Platforms, Inc.  2025
#
# Author: Paul E. McKenney <paulmck@kernel.org>

resdir="$1"
if test -z "${resdir}" || ! test -d "${resdir}" || ! test -w "${resdir}"
then
	echo Path '"'${resdir}'"' not writeable directory, no ${resdir}/testid.txt.
	exit 1
fi
echo Build directory: `pwd` > ${resdir}/testid.txt
if test -d .git
then
	echo Current commit: `git rev-parse HEAD` >> ${resdir}/testid.txt
	echo >> ${resdir}/testid.txt
	echo ' ---' Output of "'"git status"'": >> ${resdir}/testid.txt
	git status >> ${resdir}/testid.txt
	echo >> ${resdir}/testid.txt
	echo >> ${resdir}/testid.txt
	echo ' ---' Output of "'"git diff HEAD"'": >> ${resdir}/testid.txt
	git diff HEAD >> ${resdir}/testid.txt
fi
