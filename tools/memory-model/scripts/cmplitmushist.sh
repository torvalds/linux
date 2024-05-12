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
badmacnam=0
timedout=0
perfect=0
obsline=0
noobsline=0
obsresult=0
badcompare=0
comparetest () {
	if grep -q ': Unknown macro ' $1 || grep -q ': Unknown macro ' $2
	then
		if grep -q ': Unknown macro ' $1
		then
			badname=`grep ': Unknown macro ' $1 |
				sed -e 's/^.*: Unknown macro //' |
				sed -e 's/ (User error).*$//'`
			echo 'Current LKMM version does not know "'$badname'"' $1
		fi
		if grep -q ': Unknown macro ' $2
		then
			badname=`grep ': Unknown macro ' $2 |
				sed -e 's/^.*: Unknown macro //' |
				sed -e 's/ (User error).*$//'`
			echo 'Current LKMM version does not know "'$badname'"' $2
		fi
		badmacnam=`expr "$badmacnam" + 1`
		return 0
	elif grep -q '^Command exited with non-zero status 124' $1 ||
	     grep -q '^Command exited with non-zero status 124' $2
	then
		if grep -q '^Command exited with non-zero status 124' $1 &&
		   grep -q '^Command exited with non-zero status 124' $2
		then
			echo Both runs timed out: $2
		elif grep -q '^Command exited with non-zero status 124' $1
		then
			echo Old run timed out: $2
		elif grep -q '^Command exited with non-zero status 124' $2
		then
			echo New run timed out: $2
		fi
		timedout=`expr "$timedout" + 1`
		return 0
	fi
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
		echo Missing Observation line "(e.g., syntax error)": $2
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
	echo Missing Observation line "(e.g., syntax error)": $noobsline 1>&2
fi
if test "$obsresult" -ne 0
then
	echo Matching Observation Always/Sometimes/Never result: $obsresult 1>&2
fi
if test "$timedout" -ne 0
then
	echo "!!!" Timed out: $timedout 1>&2
fi
if test "$badmacnam" -ne 0
then
	echo "!!!" Unknown primitive: $badmacnam 1>&2
fi
if test "$badcompare" -ne 0
then
	echo "!!!" Result changed: $badcompare 1>&2
	exit 1
fi

exit 0
