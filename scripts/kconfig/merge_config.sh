#!/bin/sh
#  merge_config.sh - Takes a list of config fragment values, and merges
#  them one by one. Provides warnings on overridden values, and specified
#  values that did not make it to the resulting .config file (due to missed
#  dependencies or config symbol removal).
#
#  Portions reused from kconf_check and generate_cfg:
#  http://git.yoctoproject.org/cgit/cgit.cgi/yocto-kernel-tools/tree/tools/kconf_check
#  http://git.yoctoproject.org/cgit/cgit.cgi/yocto-kernel-tools/tree/tools/generate_cfg
#
#  Copyright (c) 2009-2010 Wind River Systems, Inc.
#  Copyright 2011 Linaro
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License version 2 as
#  published by the Free Software Foundation.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#  See the GNU General Public License for more details.

clean_up() {
	rm -f $TMP_FILE
	exit
}
trap clean_up HUP INT TERM

usage() {
	echo "Usage: $0 [OPTIONS] [CONFIG [...]]"
	echo "  -h    display this help text"
	echo "  -m    only merge the fragments, do not execute the make command"
	echo "  -n    use allnoconfig instead of alldefconfig"
}

MAKE=true
ALLTARGET=alldefconfig

while true; do
	case $1 in
	"-n")
		ALLTARGET=allnoconfig
		shift
		continue
		;;
	"-m")
		MAKE=false
		shift
		continue
		;;
	"-h")
		usage
		exit
		;;
	*)
		break
		;;
	esac
done



MERGE_LIST=$*
SED_CONFIG_EXP="s/^\(# \)\{0,1\}\(CONFIG_[a-zA-Z0-9_]*\)[= ].*/\2/p"
TMP_FILE=$(mktemp ./.tmp.config.XXXXXXXXXX)

# Merge files, printing warnings on overrided values
for MERGE_FILE in $MERGE_LIST ; do
	echo "Merging $MERGE_FILE"
	CFG_LIST=$(sed -n "$SED_CONFIG_EXP" $MERGE_FILE)

	for CFG in $CFG_LIST ; do
		grep -q -w $CFG $TMP_FILE
		if [ $? -eq 0 ] ; then
			PREV_VAL=$(grep -w $CFG $TMP_FILE)
			NEW_VAL=$(grep -w $CFG $MERGE_FILE)
			if [ "x$PREV_VAL" != "x$NEW_VAL" ] ; then
			echo Value of $CFG is redefined by fragment $MERGE_FILE:
			echo Previous  value: $PREV_VAL
			echo New value:       $NEW_VAL
			echo
			fi
			sed -i "/$CFG[ =]/d" $TMP_FILE
		fi
	done
	cat $MERGE_FILE >> $TMP_FILE
done

if [ "$MAKE" = "false" ]; then
	cp $TMP_FILE .config
	echo "#"
	echo "# merged configuration written to .config (needs make)"
	echo "#"
	clean_up
	exit
fi

# Use the merged file as the starting point for:
# alldefconfig: Fills in any missing symbols with Kconfig default
# allnoconfig: Fills in any missing symbols with # CONFIG_* is not set
make KCONFIG_ALLCONFIG=$TMP_FILE $ALLTARGET


# Check all specified config values took (might have missed-dependency issues)
for CFG in $(sed -n "$SED_CONFIG_EXP" $TMP_FILE); do

	REQUESTED_VAL=$(grep -w -e "$CFG" $TMP_FILE)
	ACTUAL_VAL=$(grep -w -e "$CFG" .config)
	if [ "x$REQUESTED_VAL" != "x$ACTUAL_VAL" ] ; then
		echo "Value requested for $CFG not in final .config"
		echo "Requested value:  $REQUESTED_VAL"
		echo "Actual value:     $ACTUAL_VAL"
		echo ""
	fi
done

clean_up
