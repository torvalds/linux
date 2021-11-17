#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Disassemble the Code: line in Linux oopses
# usage: decodecode < oops.file
#
# options: set env. variable AFLAGS=options to pass options to "as";
# e.g., to decode an i386 oops on an x86_64 system, use:
# AFLAGS=--32 decodecode < 386.oops
# PC=hex - the PC (program counter) the oops points to

cleanup() {
	rm -f $T $T.s $T.o $T.oo $T.aa $T.dis
	exit 1
}

die() {
	echo "$@"
	exit 1
}

trap cleanup EXIT

T=`mktemp` || die "cannot create temp file"
code=
cont=

while read i ; do

case "$i" in
*Code:*)
	code=$i
	cont=yes
	;;
*)
	[ -n "$cont" ] && {
		xdump="$(echo $i | grep '^[[:xdigit:]<>[:space:]]\+$')"
		if [ -n "$xdump" ]; then
			code="$code $xdump"
		else
			cont=
		fi
	}
	;;
esac

done

if [ -z "$code" ]; then
	rm $T
	exit
fi

echo $code
code=`echo $code | sed -e 's/.*Code: //'`

width=`expr index "$code" ' '`
width=$((($width-1)/2))
case $width in
1) type=byte ;;
2) type=2byte ;;
4) type=4byte ;;
esac

if [ -z "$ARCH" ]; then
    case `uname -m` in
	aarch64*) ARCH=arm64 ;;
	arm*) ARCH=arm ;;
    esac
fi

# Params: (tmp_file, pc_sub)
disas() {
	t=$1
	pc_sub=$2

	${CROSS_COMPILE}as $AFLAGS -o $t.o $t.s > /dev/null 2>&1

	if [ "$ARCH" = "arm" ]; then
		if [ $width -eq 2 ]; then
			OBJDUMPFLAGS="-M force-thumb"
		fi

		${CROSS_COMPILE}strip $t.o
	fi

	if [ "$ARCH" = "arm64" ]; then
		if [ $width -eq 4 ]; then
			type=inst
		fi

		${CROSS_COMPILE}strip $t.o
	fi

	if [ $pc_sub -ne 0 ]; then
		if [ $PC ]; then
			adj_vma=$(( $PC - $pc_sub ))
			OBJDUMPFLAGS="$OBJDUMPFLAGS --adjust-vma=$adj_vma"
		fi
	fi

	${CROSS_COMPILE}objdump $OBJDUMPFLAGS -S $t.o | \
		grep -v "/tmp\|Disassembly\|\.text\|^$" > $t.dis 2>&1
}

marker=`expr index "$code" "\<"`
if [ $marker -eq 0 ]; then
	marker=`expr index "$code" "\("`
fi


touch $T.oo
if [ $marker -ne 0 ]; then
	# 2 opcode bytes and a single space
	pc_sub=$(( $marker / 3 ))
	echo All code >> $T.oo
	echo ======== >> $T.oo
	beforemark=`echo "$code"`
	echo -n "	.$type 0x" > $T.s
	echo $beforemark | sed -e 's/ /,0x/g; s/[<>()]//g' >> $T.s
	disas $T $pc_sub
	cat $T.dis >> $T.oo
	rm -f $T.o $T.s $T.dis

# and fix code at-and-after marker
	code=`echo "$code" | cut -c$((${marker} + 1))-`
fi
echo Code starting with the faulting instruction  > $T.aa
echo =========================================== >> $T.aa
code=`echo $code | sed -e 's/ [<(]/ /;s/[>)] / /;s/ /,0x/g; s/[>)]$//'`
echo -n "	.$type 0x" > $T.s
echo $code >> $T.s
disas $T 0
cat $T.dis >> $T.aa

# (lines of whole $T.oo) - (lines of $T.aa, i.e. "Code starting") + 3,
# i.e. the title + the "===..=" line (sed is counting from 1, 0 address is
# special)
faultlinenum=$(( $(wc -l $T.oo  | cut -d" " -f1) - \
		 $(wc -l $T.aa  | cut -d" " -f1) + 3))

faultline=`cat $T.dis | head -1 | cut -d":" -f2-`
faultline=`echo "$faultline" | sed -e 's/\[/\\\[/g; s/\]/\\\]/g'`

cat $T.oo | sed -e "${faultlinenum}s/^\([^:]*:\)\(.*\)/\1\*\2\t\t<-- trapping instruction/"
echo
cat $T.aa
cleanup
