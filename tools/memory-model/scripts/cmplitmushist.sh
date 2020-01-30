#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Compares .out and .out.new files for each name on standard input,
# one full pathname per line.  Outputs comparison results followed by
# a summary.
#
# sh cmplitmushist.sh

T=/tmp/cmplitmushist.sh.$$
trap 'rm -rf $T' 0
mkdir $T

# comparetest oldpath newpath
perfect=0
obsline=0
noobsline=0
obsresult=0
badcompare=0
comparetest () {
	grep -v 'maxresident)k\|minor)pagefaults\|^Time' $1 > $T/oldout
	grep -v 'maxresident)k\|minor)pagefaults\|^Time' $2 > $T/newout
	if cmp -s $T/oldout $T/newout && grep -q '^Observation' $1
	then
		echo Exact output match: $2
		perfect=`expr "$perfect" + 1`
		return 0
	fi

	grep '^Observation' $1 > $T/oldout
	grep '^Observation' $2 > $T/newout
	if test -s $T/oldout -o -s $T/newout
	then
		if cmp -s $T/oldout $T/newout
		then
			echo Matching Observation result and counts: $2
			obsline=`expr "$obsline" + 1`
			return 0
		fi
	else
		echo Missing Observation line "(e.g., herd7 timeout)": $2
		noobsline=`expr "$noobsline" + 1`
		return 0
	fi

	grep '^Observation' $1 | awk '{ print $3 }' > $T/oldout
	grep '^Observation' $2 | awk '{ print $3 }' > $T/newout
	if cmp -s $T/oldout $T/newout
	then
		echo Matching Observation Always/Sometimes/Never result: $2
		obsresult=`expr "$obsresult" + 1`
		return 0
	fi
	echo ' !!!' Result changed: $2
	badcompare=`expr "$badcompare" + 1`
	return 1
}

sed -e 's/^.*$/comparetest &.out &.out.new/' > $T/cmpscript
. $T/cmpscript > $T/cmpscript.out
cat $T/cmpscript.out

echo ' ---' Summary: 1>&2
grep '!!!' $T/cmpscript.out 1>&2
if test "$perfect" -ne 0
then
	echo Exact output matches: $perfect 1>&2
fi
if test "$obsline" -ne 0
then
	echo Matching Observation result and counts: $obsline 1>&2
fi
if test "$noobsline" -ne 0
then
	echo Missing Observation line "(e.g., herd7 timeout)": $noobsline 1>&2
fi
if test "$obsresult" -ne 0
then
	echo Matching Observation Always/Sometimes/Never result: $obsresult 1>&2
fi
if test "$badcompare" -ne 0
then
	echo "!!!" Result changed: $badcompare 1>&2
	exit 1
fi

exit 0
