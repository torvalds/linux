#!/bin/sh
# $FreeBSD$

usage()
{
	echo "usage: genassym [-o outfile] objfile"
	exit 1
}


work()
{
	${NM:='nm'} ${NMFLAGS} "$1" | ${AWK:='awk'} '
	/ C .*sign$/ {
		sign = substr($1, length($1) - 3, 4)
		sub("^0*", "", sign)
		if (sign != "")
			sign = "-"
	}
	/ C .*w0$/ {
		w0 = substr($1, length($1) - 3, 4)
	}
	/ C .*w1$/ {
		w1 = substr($1, length($1) - 3, 4)
	}
	/ C .*w2$/ {
		w2 = substr($1, length($1) - 3, 4)
	}
	/ C .*w3$/ {
		w3 = substr($1, length($1) - 3, 4)
		w = w3 w2 w1 w0
		sub("^0*", "", w)
		if (w == "")
			w = "0"
		hex = ""
		if (w != "0")
			hex = "0x"
		sub("w3$", "", $3)
		# This still has minor problems representing INT_MIN, etc. 
		# E.g.,
		# with 32-bit 2''s complement ints, this prints -0x80000000,
		# which has the wrong type (unsigned int).
		printf("#define\t%s\t%s%s%s\n", $3, sign, hex, w)
	} '
}


#
#MAIN PROGGRAM
#
use_outfile="no"
while getopts "o:" option
do
	case "$option" in
	o)	outfile="$OPTARG"
		use_outfile="yes";;
	*)	usage;;
	esac
done
shift $(($OPTIND - 1))
case $# in
1)	;;
*)	usage;;
esac

if [ "$use_outfile" = "yes" ]
then
	work $1  3>"$outfile" >&3 3>&-
else
	work $1
fi

