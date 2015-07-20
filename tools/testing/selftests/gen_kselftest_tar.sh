#!/bin/bash
#
# gen_kselftest_tar
# Generate kselftest tarball
# Author: Shuah Khan <shuahkh@osg.samsung.com>
# Copyright (C) 2015 Samsung Electronics Co., Ltd.

# This software may be freely redistributed under the terms of the GNU
# General Public License (GPLv2).

# main
main()
{
	if [ "$#" -eq 0 ]; then
		echo "$0: Generating default compression gzip"
		copts="cvzf"
		ext=".tar.gz"
	else
		case "$1" in
			tar)
				copts="cvf"
				ext=".tar"
				;;
			targz)
				copts="cvzf"
				ext=".tar.gz"
				;;
			tarbz2)
				copts="cvjf"
				ext=".tar.bz2"
				;;
			tarxz)
				copts="cvJf"
				ext=".tar.xz"
				;;
			*)
			echo "Unknown tarball format $1"
			exit 1
			;;
	esac
	fi

	install_dir=./kselftest

# Run install using INSTALL_KSFT_PATH override to generate install
# directory
./kselftest_install.sh
tar $copts kselftest${ext} $install_dir
echo "Kselftest archive kselftest${ext} created!"

# clean up install directory
rm -rf kselftest
}

main "$@"
