#!/bin/bash
#
# Kselftest Install
# Install kselftest tests
# Author: Shuah Khan <shuahkh@osg.samsung.com>
# Copyright (C) 2015 Samsung Electronics Co., Ltd.

# This software may be freely redistributed under the terms of the GNU
# General Public License (GPLv2).

install_loc=`pwd`

main()
{
	if [ $(basename $install_loc) !=  "selftests" ]; then
		echo "$0: Please run it in selftests directory ..."
		exit 1;
	fi
	if [ "$#" -eq 0 ]; then
		echo "$0: Installing in default location - $install_loc ..."
	elif [ ! -d "$1" ]; then
		echo "$0: $1 doesn't exist!!"
		exit 1;
	else
		install_loc=$1
		echo "$0: Installing in specified location - $install_loc ..."
	fi

	install_dir=$install_loc/kselftest

# Create install directory
	mkdir -p $install_dir
# Build tests
	INSTALL_PATH=$install_dir make install
}

main "$@"
