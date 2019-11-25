#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0
# gen_kselftest_tar
# Generate kselftest tarball
# Author: Shuah Khan <shuahkh@osg.samsung.com>
# Copyright (C) 2015 Samsung Electronics Co., Ltd.

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

	# Create working directory.
	dest=`pwd`
	install_work="$dest"/kselftest_install
	install_name=kselftest
	install_dir="$install_work"/"$install_name"
	mkdir -p "$install_dir"

	# Run install using INSTALL_KSFT_PATH override to generate install
	# directory
	./kselftest_install.sh "$install_dir"
	(cd "$install_work"; tar $copts "$dest"/kselftest${ext} $install_name)
	echo "Kselftest archive kselftest${ext} created!"

	# clean up top-level install work directory
	rm -rf "$install_work"
}

main "$@"
