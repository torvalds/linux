:
#
# SPDX-License-Identifier: Beerware
#
# ----------------------------------------------------------------------------
# "THE BEER-WARE LICENSE" (Revision 42):
# <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
# can do whatever you want with this stuff. If we meet some day, and you think
# this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
# ----------------------------------------------------------------------------
#
# Sort options by "Matthew Emmerton" <matt@gsicomp.on.ca>
#
# $FreeBSD$
#
# This shell script will make a cross reference of the symbols of a kernel.
#

COMPILEDIR=/sys/`uname -m`/compile
KERNELNAME=LINT
SORTORDER=-k1

args=`getopt h?k:s: $*`;
if [ $? != 0 ]
then
	args="-h";
fi
set -- $args;
for i
do
	case "$i"
	in
	-h|-\?)
		echo "Usage: $0 [ -k <kernelname> ] [ -s [ 'symbol' | 'filename' ] ]";
		exit 0;
		;;
	-k)
		KERNELNAME=$2
		if [ -d ${COMPILEDIR}/${KERNELNAME} ];
		then
			shift; shift;
			continue;
		fi
		echo "Kernel '$KERNELNAME' does not exist in ${COMPILEDIR}!";
		exit 1;
		;;
	-s)
		if [ "x$2" = "xsymbol" ]
		then
			SORTORDER=-k1
			shift; shift;
			continue;
		fi
		if [ "x$2" = "xfilename" ]
		then
			SORTORDER=-k2
			shift; shift;
			continue;
		fi
		echo "Invalid selection for -s: $2";
		exit 1;
		;;
	--)
		shift;
		break;
		;;
	esac
done

cd ${COMPILEDIR}/${KERNELNAME}

MOD_OBJS=`find modules -name \*.o`

for i in *.o $MOD_OBJS
do
	nm -gon $i
done | sed '
/aicasm.*:/d
/genassym.*:/d
s/.*\///
s/:/ /
' |  awk '
NF > 1	{
	if ($2 == "t") 
		next
	if ($2 == "F")
		next
	if ($2 == "U") {
		ref[$3]=ref[$3]" "$1
		nm[$3]++
	} else if ($3 == "D" || $3 == "T" || $3 == "B" || $3 == "R" || $3 == "A") {
		if (def[$4] != "")
			def[$4]=def[$4]" "$1
		else
			def[$4]=$1
		nm[$4]++
	} else if ($2 == "?") {
		if (def[$3] == "S")
			i++
		else if (def[$3] != "")
			def[$3]=def[$3]",S"
		else
			def[$3]="S"
		ref[$3]=ref[$3]" "$1
		nm[$3]++
	} else if ($2 == "C") {
		if (def[$3] == $2)
			i++
		else if (def[$3] == "")
			def[$3]=$1
		else
			ref[$3]=ref[$3]" "$1
		nm[$3]++
	} else {
		print ">>>",$0
	}
	}
END	{
	for (i in nm) {
		printf "%s {%s} %s\n",i,def[i],ref[i]
	}
	}
' | sort $SORTORDER | awk '
	{
	if ($2 == "{S}")
		$2 = "<Linker set>"
	if (length($3) == 0) {
		printf "%-31s %d %s\tUNREF\n",$1,0, $2
		N1++
	} else if ($2 == "{}") {
		printf "%-31s %d {UNDEF}\n",$1, NF-2
		N2++
	} else {
		printf "%-31s %d %s",$1,NF-2,$2
		p = 80;
		for (i = 3 ; i <= NF; i++) {
			if (p+length ($i)+1 > 48) {
				printf "\n\t\t\t\t\t%s", $i
				p = 7;
			} else {
				printf " %s", $i
			}
			p += 1 + length ($i)
		}
		printf "\n"
		N3++
		if (NF-2 == 1) 
			N4++
		if (NF-2 == 2)
			N5++
	}
	}
END	{
	printf "Total symbols: %5d\n",N1+N2+N3
	printf "unref symbols: %5d\n",N1
	printf "undef symbols: %5d\n",N2
	printf "1 ref symbols: %5d\n",N4
	printf "2 ref symbols: %5d\n",N5
	}
'
