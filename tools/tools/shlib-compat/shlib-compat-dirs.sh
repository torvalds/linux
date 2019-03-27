#!/bin/sh -e
#
# $FreeBSD$

SHLIB_COMPAT=$(dirname $0)/shlib-compat.py

if [ $# -lt 3 ]; then
	echo "Usage: $0 orig-dir new-dir output-dir"
	exit 1
fi

orig=$1
new=$2
out=$3
shift 3

remove_empty() {
	local i
	for i in $*; do
		[ -s $i ] || rm -f $i
	done
}

test_file() {
	local i
	for i in $*; do
		if [ \! -f $1 ]; then
			echo "file not found: $1"
			return 1
		fi
	done
}

rorig=`realpath $orig`
rnew=`realpath $new`
list=`(cd $rorig; ls; cd $rnew; ls) | sort -u`
for i in $list; do
	echo $i
	test_file $orig/$i $new/$i || continue
	$SHLIB_COMPAT --out-orig $out/$i.orig.c --out-new $out/$i.new.c -v "$@" \
		$orig/$i $new/$i > $out/$i.cmp 2> $out/$i.err || true
	remove_empty $out/$i.orig.c $out/$i.new.c $out/$i.cmp $out/$i.err
	if [ -f $out/$i.orig.c -a -f $out/$i.new.c ]; then
		astyle --quiet --style=bsd -k3 $out/$i.orig.c $out/$i.new.c
		rm -f $out/$i.orig.c.orig $out/$i.new.c.orig
		diff -u $out/$i.orig.c $out/$i.new.c > $out/$i.diff || true
	fi
done
