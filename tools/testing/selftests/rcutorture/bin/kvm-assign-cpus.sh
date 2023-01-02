#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Produce awk statements roughly depicting the system's CPU and cache
# layout.  If the required information is not available, produce
# error messages as awk comments.  Successful exit regardless.
#
# Usage: kvm-assign-cpus.sh /path/to/sysfs

T="`mktemp -d ${TMPDIR-/tmp}/kvm-assign-cpus.sh.XXXXXX`"
trap 'rm -rf $T' 0 2

sysfsdir=${1-/sys/devices/system/node}
if ! cd "$sysfsdir" > $T/msg 2>&1
then
	sed -e 's/^/# /' < $T/msg
	exit 0
fi
nodelist="`ls -d node*`"
for i in node*
do
	if ! test -d $i/
	then
		echo "# Not a directory: $sysfsdir/node*"
		exit 0
	fi
	for j in $i/cpu*/cache/index*
	do
		if ! test -d $j/
		then
			echo "# Not a directory: $sysfsdir/$j"
			exit 0
		else
			break
		fi
	done
	indexlist="`ls -d $i/cpu* | grep 'cpu[0-9][0-9]*' | head -1 | sed -e 's,^.*$,ls -d &/cache/index*,' | sh | sed -e 's,^.*/,,'`"
	break
done
for i in node*/cpu*/cache/index*/shared_cpu_list
do
	if ! test -f $i
	then
		echo "# Not a file: $sysfsdir/$i"
		exit 0
	else
		break
	fi
done
firstshared=
for i in $indexlist
do
	rm -f $T/cpulist
	for n in node*
	do
		f="$n/cpu*/cache/$i/shared_cpu_list"
		if ! cat $f > $T/msg 2>&1
		then
			sed -e 's/^/# /' < $T/msg
			exit 0
		fi
		cat $f >> $T/cpulist
	done
	if grep -q '[-,]' $T/cpulist
	then
		if test -z "$firstshared"
		then
			firstshared="$i"
		fi
	fi
done
if test -z "$firstshared"
then
	splitindex="`echo $indexlist | sed -e 's/ .*$//'`"
else
	splitindex="$firstshared"
fi
nodenum=0
for n in node*
do
	cat $n/cpu*/cache/$splitindex/shared_cpu_list | sort -u -k1n |
	awk -v nodenum="$nodenum" '
	BEGIN {
		idx = 0;
	}

	{
		nlists = split($0, cpulists, ",");
		for (i = 1; i <= nlists; i++) {
			listsize = split(cpulists[i], cpus, "-");
			if (listsize == 1)
				cpus[2] = cpus[1];
			for (j = cpus[1]; j <= cpus[2]; j++) {
				print "cpu[" nodenum "][" idx "] = " j ";";
				idx++;
			}
		}
	}

	END {
		print "nodecpus[" nodenum "] = " idx ";";
	}'
	nodenum=`expr $nodenum + 1`
done
echo "numnodes = $nodenum;"
