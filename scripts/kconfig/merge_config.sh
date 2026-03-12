#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
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

set -e

clean_up() {
	rm -f "$TMP_FILE"
	rm -f "$TMP_FILE.new"
}

usage() {
	echo "Usage: $0 [OPTIONS] [CONFIG [...]]"
	echo "  -h    display this help text"
	echo "  -m    only merge the fragments, do not execute the make command"
	echo "  -n    use allnoconfig instead of alldefconfig"
	echo "  -r    list redundant entries when merging fragments"
	echo "  -y    make builtin have precedence over modules"
	echo "  -O    dir to put generated output files.  Consider setting \$KCONFIG_CONFIG instead."
	echo "  -s    strict mode. Fail if the fragment redefines any value."
	echo "  -Q    disable warning messages for overridden options."
	echo
	echo "Used prefix: '$CONFIG_PREFIX'. You can redefine it with \$CONFIG_ environment variable."
}

RUNMAKE=true
ALLTARGET=alldefconfig
WARNREDUN=false
BUILTIN=false
OUTPUT=.
STRICT=false
CONFIG_PREFIX=${CONFIG_-CONFIG_}
WARNOVERRIDE=echo

if [ -z "$AWK" ]; then
	AWK=awk
fi

while true; do
	case $1 in
	"-n")
		ALLTARGET=allnoconfig
		shift
		continue
		;;
	"-m")
		RUNMAKE=false
		shift
		continue
		;;
	"-h")
		usage
		exit
		;;
	"-r")
		WARNREDUN=true
		shift
		continue
		;;
	"-y")
		BUILTIN=true
		shift
		continue
		;;
	"-O")
		if [ -d $2 ];then
			OUTPUT=$(echo $2 | sed 's/\/*$//')
		else
			echo "output directory $2 does not exist" 1>&2
			exit 1
		fi
		shift 2
		continue
		;;
	"-s")
		STRICT=true
		shift
		continue
		;;
	"-Q")
		WARNOVERRIDE=true
		shift
		continue
		;;
	*)
		break
		;;
	esac
done

if [ "$#" -lt 1 ] ; then
	usage
	exit
fi

if [ -z "$KCONFIG_CONFIG" ]; then
	if [ "$OUTPUT" != . ]; then
		KCONFIG_CONFIG=$(readlink -m -- "$OUTPUT/.config")
	else
		KCONFIG_CONFIG=.config
	fi
fi

INITFILE=$1
shift;

if [ ! -r "$INITFILE" ]; then
	echo "The base file '$INITFILE' does not exist. Creating one..." >&2
	touch "$INITFILE"
fi

MERGE_LIST=$*

TMP_FILE=$(mktemp ./.tmp.config.XXXXXXXXXX)

echo "Using $INITFILE as base"

trap clean_up EXIT

cat $INITFILE > $TMP_FILE

PROCESSED_FILES=""

# Merge files, printing warnings on overridden values
for ORIG_MERGE_FILE in $MERGE_LIST ; do
	echo "Merging $ORIG_MERGE_FILE"
	if [ ! -r "$ORIG_MERGE_FILE" ]; then
		echo "The merge file '$ORIG_MERGE_FILE' does not exist.  Exit." >&2
		exit 1
	fi

	# Check for duplicate input files
	case " $PROCESSED_FILES " in
		*" $ORIG_MERGE_FILE "*)
			${WARNOVERRIDE} "WARNING: Input file provided multiple times: $ORIG_MERGE_FILE"
			;;
	esac

	# Use awk for single-pass processing instead of per-symbol grep/sed
	if ! "$AWK" -v prefix="$CONFIG_PREFIX" \
		-v warnoverride="$WARNOVERRIDE" \
		-v strict="$STRICT" \
		-v builtin="$BUILTIN" \
		-v warnredun="$WARNREDUN" '
	BEGIN {
		strict_violated = 0
		cfg_regex = "^" prefix "[a-zA-Z0-9_]+"
		notset_regex = "^# " prefix "[a-zA-Z0-9_]+ is not set$"
	}

	# Extract config name from a line, returns "" if not a config line
	function get_cfg(line) {
		if (match(line, cfg_regex)) {
			return substr(line, RSTART, RLENGTH)
		} else if (match(line, notset_regex)) {
			# Extract CONFIG_FOO from "# CONFIG_FOO is not set"
			sub(/^# /, "", line)
			sub(/ is not set$/, "", line)
			return line
		}
		return ""
	}

	function warn_builtin(cfg, prev, new) {
		if (warnoverride == "true") return
		print cfg ": -y passed, will not demote y to m"
		print "Previous value: " prev
		print "New value: " new
		print ""
	}

	function warn_redefined(cfg, prev, new) {
		if (warnoverride == "true") return
		print "Value of " cfg " is redefined by fragment " mergefile ":"
		print "Previous value: " prev
		print "New value: " new
		print ""
	}

	function warn_redundant(cfg) {
		if (warnredun != "true" || warnoverride == "true") return
		print "Value of " cfg " is redundant by fragment " mergefile ":"
	}

	# First pass: read merge file, store all lines and index
	FILENAME == ARGV[1] {
	        mergefile = FILENAME
		merge_lines[FNR] = $0
		merge_total = FNR
		cfg = get_cfg($0)
		if (cfg != "") {
			merge_cfg[cfg] = $0
			merge_cfg_line[cfg] = FNR
		}
		next
	}

	# Second pass: process base file (TMP_FILE)
	FILENAME == ARGV[2] {
		cfg = get_cfg($0)

		# Not a config or not in merge file - keep it
		if (cfg == "" || !(cfg in merge_cfg)) {
			print $0 >> ARGV[3]
			next
		}

	        prev_val = $0
		new_val = merge_cfg[cfg]

		# BUILTIN: do not demote y to m
		if (builtin == "true" && new_val ~ /=m$/ && prev_val ~ /=y$/) {
			warn_builtin(cfg, prev_val, new_val)
			print $0 >> ARGV[3]
			skip_merge[merge_cfg_line[cfg]] = 1
			next
		}

		# Values equal - redundant
		if (prev_val == new_val) {
			warn_redundant(cfg)
			next
		}

		# "=n" is the same as "is not set"
		if (prev_val ~ /=n$/ && new_val ~ / is not set$/) {
			print $0 >> ARGV[3]
			next
		}

		# Values differ - redefined
		warn_redefined(cfg, prev_val, new_val)
		if (strict == "true") {
			strict_violated = 1
		}
	}

	# output file, skip all lines
	FILENAME == ARGV[3] {
		nextfile
	}

	END {
		# Newline in case base file lacks trailing newline
		print "" >> ARGV[3]
		# Append merge file, skipping lines marked for builtin preservation
		for (i = 1; i <= merge_total; i++) {
			if (!(i in skip_merge)) {
				print merge_lines[i] >> ARGV[3]
			}
		}
		if (strict_violated) {
			exit 1
		}
	}' \
	"$ORIG_MERGE_FILE" "$TMP_FILE" "$TMP_FILE.new"; then
		# awk exited non-zero, strict mode was violated
		STRICT_MODE_VIOLATED=true
	fi
	mv "$TMP_FILE.new" "$TMP_FILE"
	PROCESSED_FILES="$PROCESSED_FILES $ORIG_MERGE_FILE"
done
if [ "$STRICT_MODE_VIOLATED" = "true" ]; then
	echo "The fragment redefined a value and strict mode had been passed."
	exit 1
fi

if [ "$RUNMAKE" = "false" ]; then
	cp -T -- "$TMP_FILE" "$KCONFIG_CONFIG"
	echo "#"
	echo "# merged configuration written to $KCONFIG_CONFIG (needs make)"
	echo "#"
	exit
fi

# If we have an output dir, setup the O= argument, otherwise leave
# it blank, since O=. will create an unnecessary ./source softlink
OUTPUT_ARG=""
if [ "$OUTPUT" != "." ] ; then
	OUTPUT_ARG="O=$OUTPUT"
fi


# Use the merged file as the starting point for:
# alldefconfig: Fills in any missing symbols with Kconfig default
# allnoconfig: Fills in any missing symbols with # CONFIG_* is not set
make KCONFIG_ALLCONFIG=$TMP_FILE $OUTPUT_ARG $ALLTARGET

# Check all specified config values took effect (might have missed-dependency issues)
if ! "$AWK" -v prefix="$CONFIG_PREFIX" \
	-v warnoverride="$WARNOVERRIDE" \
	-v strict="$STRICT" \
	-v warnredun="$WARNREDUN" '
BEGIN {
	strict_violated = 0
	cfg_regex = "^" prefix "[a-zA-Z0-9_]+"
	notset_regex = "^# " prefix "[a-zA-Z0-9_]+ is not set$"
}

# Extract config name from a line, returns "" if not a config line
function get_cfg(line) {
	if (match(line, cfg_regex)) {
		return substr(line, RSTART, RLENGTH)
	} else if (match(line, notset_regex)) {
		# Extract CONFIG_FOO from "# CONFIG_FOO is not set"
		sub(/^# /, "", line)
		sub(/ is not set$/, "", line)
		return line
	}
	return ""
}

function warn_mismatch(cfg, merged, final) {
	if (warnredun == "true") return
	if (final == "" && !(merged ~ / is not set$/ || merged ~ /=n$/)) {
		print "WARNING: Value requested for " cfg " not in final .config"
		print "Requested value: " merged
		print "Actual value:    " final
	} else if (final == "" && merged ~ / is not set$/) {
		# not set, pass
	} else if (merged == "" && final != "") {
		print "WARNING: " cfg " not in merged config but added in final .config:"
		print "Requested value: " merged
		print "Actual value:    " final
	} else {
		print "WARNING: " cfg " differs:"
		print "Requested value: " merged
		print "Actual value:    " final
	}
}

# First pass: read effective config file, store all lines
FILENAME == ARGV[1] {
	cfg = get_cfg($0)
	if (cfg != "") {
		config_cfg[cfg] = $0
	}
	next
}

# Second pass: process merged config and compare against effective config
{
	cfg = get_cfg($0)
	if (cfg == "") next

	# strip trailing comment
	sub(/[[:space:]]+#.*/, "", $0)
	merged_val = $0
	final_val = config_cfg[cfg]

	if (merged_val == final_val) next

	if (merged_val ~ /=n$/ && final_val ~ / is not set$/) next
	if (merged_val ~ /=n$/ && final_val == "") next

	warn_mismatch(cfg, merged_val, final_val)

	if (strict == "true") {
		strict_violated = 1
	}
}

END {
	if (strict_violated) {
		exit 1
	}
}' \
"$KCONFIG_CONFIG" "$TMP_FILE"; then
	# awk exited non-zero, strict mode was violated
	STRICT_MODE_VIOLATED=true
fi

if [ "$STRICT" == "true" ] && [ "$STRICT_MODE_VIOLATED" == "true" ]; then
	echo "Requested and effective config differ"
	exit 1
fi
