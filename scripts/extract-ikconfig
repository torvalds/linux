#!/bin/sh
# ----------------------------------------------------------------------
# extract-ikconfig - Extract the .config file from a kernel image
#
# This will only work when the kernel was compiled with CONFIG_IKCONFIG.
#
# The obscure use of the "tr" filter is to work around older versions of
# "grep" that report the byte offset of the line instead of the pattern.
#
# (c) 2009,2010 Dick Streefland <dick@streefland.net>
# Licensed under the terms of the GNU General Public License.
# ----------------------------------------------------------------------

cf1='IKCFG_ST\037\213\010'
cf2='0123456789'

dump_config()
{
	if	pos=`tr "$cf1\n$cf2" "\n$cf2=" < "$1" | grep -abo "^$cf2"`
	then
		pos=${pos%%:*}
		tail -c+$(($pos+8)) "$1" | zcat > $tmp1 2> /dev/null
		if	[ $? != 1 ]
		then	# exit status must be 0 or 2 (trailing garbage warning)
			cat $tmp1
			exit 0
		fi
	fi
}

try_decompress()
{
	for	pos in `tr "$1\n$2" "\n$2=" < "$img" | grep -abo "^$2"`
	do
		pos=${pos%%:*}
		tail -c+$pos "$img" | $3 > $tmp2 2> /dev/null
		dump_config $tmp2
	done
}

# Check invocation:
me=${0##*/}
img=$1
if	[ $# -ne 1 -o ! -s "$img" ]
then
	echo "Usage: $me <kernel-image>" >&2
	exit 2
fi

# Prepare temp files:
tmp1=/tmp/ikconfig$$.1
tmp2=/tmp/ikconfig$$.2
trap "rm -f $tmp1 $tmp2" 0

# Initial attempt for uncompressed images or objects:
dump_config "$img"

# That didn't work, so retry after decompression.
try_decompress '\037\213\010' xy    gunzip
try_decompress '\3757zXZ\000' abcde unxz
try_decompress 'BZh'          xy    bunzip2
try_decompress '\135\0\0\0'   xxx   unlzma
try_decompress '\211\114\132' xy    'lzop -d'

# Bail out:
echo "$me: Cannot find kernel config." >&2
exit 1
