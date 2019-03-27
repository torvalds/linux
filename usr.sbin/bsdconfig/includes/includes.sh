#!/bin/sh
#-
# Copyright (c) 2013 Devin Teske
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#
############################################################ INCLUDES

# Prevent common.subr from auto initializing debugging (this is not an inter-
# active utility that requires debugging; also `-d' has been repurposed).
#
DEBUG_SELF_INITIALIZE=NO

BSDCFG_SHARE="/usr/share/bsdconfig"
. $BSDCFG_SHARE/common.subr || exit 1
f_dprintf "%s: loading includes..." "$0"

BSDCFG_LIBE="/usr/libexec/bsdconfig" APP_DIR="includes"
f_include_lang $BSDCFG_LIBE/include/messages.subr
f_include_lang $BSDCFG_LIBE/$APP_DIR/include/messages.subr

f_index_menusel_keyword $BSDCFG_LIBE/$APP_DIR/INDEX "$pgm" ipgm &&
	pgm="${ipgm:-$pgm}"

############################################################ GLOBALS

#
# Options
#
USE_COLOR=1
SHOW_DESC=
SHOW_FUNCS=
FUNC_PATTERN=

############################################################ FUNCTIONS

# show_functions $file
#
# Show the functions in the given include file.
#
show_include()
{
	local file="${1#./}"

	local pattern="${FUNC_PATTERN:-.*}"
	output=$( awk \
		-v use_color=${USE_COLOR:-0} \
		-v re="$pattern" \
		-v show_desc=${SHOW_DESC:-0} '
        function _asorti(src, dest)
        {
		k = nitems = 0;

		# Copy src indices to dest and calculate array length
		for (i in src) dest[++nitems] = i

		# Sort the array of indices (dest) using insertion sort method
		for (i = 1; i <= nitems; k = i++)
		{
			idx = dest[i]
			while ((k > 0) && (dest[k] > idx))
			{
				dest[k+1] = dest[k]
				k--
			}
			dest[k+1] = idx
		}

		return nitems
        }
	/^$/,/^#/ {
		if ($0 ~ /^# f_/) {
			if (!match($2, re)) next
			fn = $2
			if (use_color)
				syntax[fn] = sprintf("+%s[1;31m%s[0m%s\n",
				       substr($0, 2, RSTART),
				       substr($0, 2 + RSTART, RLENGTH),
				       substr($0, 2 + RSTART + RLENGTH))
			else
				syntax[fn] = "+" substr($0, 2) "\n"
			if (show_desc)
				print_more = 1
			else
				print_more = substr($0, length($0)) == "\\"
		}
		if (show_desc && print_more) {
			getline
			while ($0 ~ /^#/) {
				syntax[fn] = syntax[fn] " " substr($0, 2) "\n"
				getline
			}
			print_more = 0
		} else while (print_more) {
			getline
			syntax[fn] = syntax[fn] " " substr($0, 2) "\n"
			print_more = substr($0, length($0)) == "\\"
		}
	}
	END {
		n = _asorti(syntax, sorted_indices)
		for (i = 1; i <= n; i++)
			printf "%s", syntax[sorted_indices[i]]
	}' "$file" )
	if [ "$output" ]; then
		if [ ! "$SHOW_FUNCS" ]; then
			echo "$file"
			return $SUCCESS
		fi
		if [ "$FUNC_PATTERN" ]; then
			printf ">>> $msg_functions_in_matching\n" \
			       "$file" "$FUNC_PATTERN"
		else
			printf ">>> $msg_functions_in\n" "$file"
		fi
		echo "$output"
		echo # blank line to simplify awk(1)-based reparse
	fi
}

############################################################ MAIN

# Incorporate rc-file if it exists
[ -f "$HOME/.bsdconfigrc" ] && f_include "$HOME/.bsdconfigrc"

# Are we in a terminal?
[ -t 1 ] || USE_COLOR=

#
# Process command-line arguments
#
while getopts adfF:hn flag; do
	case "$flag" in
	a) USE_COLOR=1 ;;
	d) SHOW_DESC=1 SHOW_FUNCS=1 ;;
	f) SHOW_FUNCS=1 ;;
	F) FUNC_PATTERN="$OPTARG" ;;
	n) USE_COLOR= ;;
	h|\?) f_usage $BSDCFG_LIBE/$APP_DIR/USAGE "PROGRAM_NAME" "$pgm" ;;
	esac
done
shift $(( $OPTIND - 1 ))

# cd(1) to `share' dir so relative paths work for find and positional args
cd $BSDCFG_SHARE || f_die # Pedantic

#
# If given an argument, operate on it specifically (implied `-f') and exit
#
[ $# -gt 0 ] && SHOW_FUNCS=1
for include in "$@"; do
	# See if they've just omitted the `*.subr' suffix
	[ -f "$include.subr" -a ! -f "$include" ] && include="$include.subr"
	if [ ! -f "$include" ]; then
		printf "$msg_no_such_file_or_directory\n" "$0" "$include"
		exit $FAILURE
	elif [ ! -r "$include" ]; then
		printf "$msg_permission_denied\n" "$0" "$include"
		exit $FAILURE
	fi
	show_include "$include" || f_die
done

# Exit if we processed some include arguments
[ $# -gt 0 ] && exit $SUCCESS

#
# Operate an all known include files
# NB: If we get this far, we had no include arguments
#
find -s . -type f -and -iname '*.subr' | while read file; do
	if [ "$SHOW_FUNCS" -o "$FUNC_PATTERN" ]; then
		show_include "$file"
	else
		echo "${file#./}"
	fi
done

exit $SUCCESS

################################################################################
# END
################################################################################
