#!/bin/sh
# ----------------------------------------------------------------------------
# "THE BEER-WARE LICENSE" (Revision 42):
# <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
# can do whatever you want with this stuff. If we meet some day, and you think
# this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
# ----------------------------------------------------------------------------
#
# $FreeBSD$
#
# This script tries to find #include statements which are not needed in
# the FreeBSD kernel tree.
#

set -e

# Base of the kernel sources you want to work on
cd /sys

# Set to true to start from scratch, false to resume
init=false

# Which kernels you want to check
kernels="LINT GENERIC"

NO_MODULES=yes
export NO_MODULES

if $init ; then
	(
	echo "Cleaning modules"
	cd modules
	make clean > /dev/null 2>&1
	make cleandir > /dev/null 2>&1
	make cleandir > /dev/null 2>&1
	make clean > /dev/null 2>&1
	make clean > /dev/null 2>&1
	)

	(
	echo "Cleaning compile"
	cd compile
	ls | grep -v CVS | xargs rm -rf
	)
fi

(
echo "Cleaning temp files"
find . -name '*.h_' -print | xargs rm -f
find . -name '::*' -print | xargs rm -f
find . -name '*.o' -size 0 -print | xargs rm -f
)

echo "Configuring kernels"
(
	cd i386/conf
	make LINT
	if $init ; then
		rm -rf ../../compile/LINT ../../compile/GENERIC
	fi
	config LINT
	config GENERIC
)

for i in $kernels
do
	(
	echo "Compiling $i"
	cd compile/$i
	make > x.0 2>&1
	tail -4 x.0
	if [ ! -f kernel ] ; then
		echo "Error: No $i kernel built"
		exit 1
	fi
	)
done

(
echo "Compiling modules"
cd modules
make > x.0 2>&1 
)

# Generate the list of object files we want to check
# you can put a convenient grep right before the sort
# if you want just some specific subset of files checked
(
cd modules
for i in *
do
	if [ -d $i -a $i != CVS ] ; then
		( cd $i ; ls *.o 2>/dev/null || true)
	fi
done
cd ../compile
for i in $kernels
do
	( cd $i ; ls *.o 2>/dev/null )
done
) | sed '
/aicasm/d	
/genassym/d
/vers.o/d
/setdef0.o/d
/setdef1.o/d
' | sort -u > _

objlist=`cat _`


for o in $objlist
do
	l=""
	src=""
	for k in $kernels
	do
		if [ ! -f compile/$k/$o ] ; then
			continue;
		fi
		l="$l compile/$k"
		if [ "x$src" = "x" ] ; then
			cd compile/$k
			mv $o ${o}_
			make -n $o > _
			mv ${o}_ $o
			src=compile/$k/`awk '$1 == "cc" {print $NF}' _`
			cd ../..
			if expr "x$src" : 'x.*\.c$' > /dev/null ; then
				true
			else
				echo NO SRC $o
				src=""
			fi
		fi
	done
	for m in modules/*
	do
		if [ ! -d $m -o ! -f $m/$o ] ; then
			continue;
		fi
		l="$l $m"
		if [ "x$src" = "x" ] ; then
			cd $m
			mv $o ${o}_
			make -n $o > _
			mv ${o}_ $o
			src=`awk '$1 == "cc" {print $NF}' _`
			cd ../..
			if expr "x$src" : 'x.*\.c$' > /dev/null ; then
				if [ "`dirname $src`" = "." ] ; then
					src="$m/$src"
				fi
				true
			else
				echo NO SRC $o
				src=""
			fi
		fi
	done
	if [ "x$src" = "x" ] ; then
		echo "NO SOURCE $o"
		continue
	fi
	echo "OBJ	$o"
	echo "	SRC	$src"

	grep -n '^[ 	]*#[ 	]*include' $src | sed '
	s/^\([0-9]*\):[ 	]*#[ 	]*include[ 	]*[<"]/\1 /
	s/[">].*//
	/ opt_/d
	' | sort -rn | while read lin incl
	do
		S=""
		echo "		INCL	$lin	$incl"
		cp $src ${src}_

		# Check if we can compile without this #include line.

		sed "${lin}s/.*//" ${src}_ > ${src}
		for t in $l
		do
			cd $t
			mv ${o} ${o}_
			if make ${o} > _log 2>&1 ; then
				if cmp -s ${o} ${o}_ ; then
					echo "			$t	same object"
				else
					echo "			$t	changed object"
					S=TAG
				fi
			else
				echo "			$t	used"
				S=TAG
			fi
			mv ${o}_ ${o}
			cd ../..
			if [ "x$S" != "x" ] ; then
				break
			fi
		done
		if [ "x$S" != "x" ] ; then
			mv ${src}_ ${src}
			continue
		fi

		# Check if this is because it is a nested #include
		for t in $l
		do
			cd $t
			rm -rf foo
			mkdir -p foo/${incl}
			rmdir foo/${incl}
			touch foo/${incl}
			mv ${o} ${o}_
			if make INCLMAGIC=-Ifoo ${o} > _log2 2>&1 ; then
				if cmp -s ${o} ${o}_ ; then
					echo "			$t	still same object"
				else
					echo "			$t	changed object"
					S=TAG
				fi
			else
				echo "			$t	nested include"
				S=TAG
			fi
			rm -rf foo
			mv ${o}_ ${o}
			cd ../..
			if [ "x$S" != "x" ] ; then
				break
			fi
		done
		if [ "x$S" != "x" ] ; then
			mv ${src}_ ${src}
			continue
		fi

		# Check if this is because it is #ifdef'ed out

		sed "${lin}s/.*/#error \"BARF\"/" ${src}_ > ${src}
		for t in $l
		do
			cd $t
			mv ${o} ${o}_
			if make ${o} > /dev/null 2>&1 ; then
				echo "			$t	line not read"
				S=TAG
			fi
			mv ${o}_ ${o}
			cd ../..
			if [ "x$S" != "x" ] ; then
				break
			fi
		done

		mv ${src}_ ${src}
		if [ "x$S" != "x" ] ; then
			continue
		fi

		# Check if the warnings changed.

		for t in $l
		do
			cd $t
			mv ${o} ${o}_
			if make ${o} > _ref 2>&1 ; then
				if cmp -s _ref _log ; then
					echo "			$t	same warnings"
				else
					echo "			$t	changed warnings"
					S=TAG
				fi
			else
				echo "ARGHH!!!"
				exit 9
			fi
					
			mv ${o}_ ${o}
			cd ../..
			if [ "x$S" != "x" ] ; then
				break
			fi
		done
		if [ "x$S" != "x" ] ; then
			continue
		fi
		cp $src ${src}_
		sed "${lin}d" ${src}_ > ${src}
		rm ${src}_
		touch _again
		echo "BINGO $src $lin $incl $obj $l"
	done
done
