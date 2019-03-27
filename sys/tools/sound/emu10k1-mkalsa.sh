# $FreeBSD$

GREP=${GREP:-grep}
CC=${CC:-cc}
AWK=${AWK:-awk}
MV=${MV:=mv}
RM=${RM:=rm}
IN=$1
OUT=$2

trap "${RM} -f $OUT.tmp" EXIT

$GREP -v '#include' $IN | \
$CC -E -D__KERNEL__ -dM -  | \
$AWK -F"[     (]" '
/define/  {
	print "#ifndef " $2;
	print;
	print "#endif";
}' > $OUT.tmp
${MV} -f $OUT.tmp $OUT
