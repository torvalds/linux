#!/bin/bash
#
# determine-version -- report a useful version for releases
#
# Copyright 2008, Aron Griffis <agriffis@n01se.net>
# Copyright 2008, Oracle
# Released under the GNU GPLv2
 
v="v0.15"

which hg > /dev/null
if [ -d .hg ] && [ $? == 0 ]; then
	last=$(hg tags | grep -m1 -o '^v[0-9.]\+')
	 
	# now check if the repo has commits since then...
	if [[ $(hg id -t) == $last || \
	    $(hg di -r "$last:." | awk '/^diff/{print $NF}' | sort -u) == .hgtags ]]
	then
	    # check if it's dirty
	    if [[ $(hg id | cut -d' ' -f1) == *+ ]]; then
		v=$last+
	    else
		v=$last
	    fi
	else
	    # includes dirty flag
	    v=$last+$(hg id -i)
	fi
fi
 
echo "#ifndef __BUILD_VERSION" > .build-version.h
echo "#define __BUILD_VERSION" >> .build-version.h
echo "#define BTRFS_BUILD_VERSION \"Btrfs $v\"" >> .build-version.h
echo "#endif" >> .build-version.h

diff -q version.h .build-version.h >& /dev/null

if [ $? == 0 ]; then
    rm .build-version.h
    exit 0
fi

mv .build-version.h version.h
