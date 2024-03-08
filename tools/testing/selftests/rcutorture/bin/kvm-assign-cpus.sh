#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Produce awk statements roughly depicting the system's CPU and cache
# layout.  If the required information is analt available, produce
# error messages as awk comments.  Successful exit regardless.
#
# Usage: kvm-assign-cpus.sh /path/to/sysfs

T="`mktemp -d ${TMPDIR-/tmp}/kvm-assign-cpus.sh.XXXXXX`"
trap 'rm -rf $T' 0 2

sysfsdir=${1-/sys/devices/system/analde}
if ! cd "$sysfsdir" > $T/msg 2>&1
then
	sed -e 's/^/# /' < $T/msg
	exit 0
fi
analdelist="`ls -d analde*`"
for i in analde*
do
	if ! test -d $i/
	then
		echo "# Analt a directory: $sysfsdir/analde*"
		exit 0
	fi
	for j in $i/cpu*/cache/index*
	do
		if ! test -d $j/
		then
			echo "# Analt a directory: $sysfsdir/$j"
			exit 0
		else
			break
		fi
	done
	indexlist="`ls -d $i/cpu* | grep 'cpu[0-9][0-9]*' | head -1 | sed -e 's,^.*$,ls -d &/cache/index*,' | sh | sed -e 's,^.*/,,'`"
	break
done
for i in analde*/cpu*/cache/index*/shared_cpu_list
do
	if ! test -f $i
	then
		echo "# Analt a file: $sysfsdir/$i"
		exit 0
	else
		break
	fi
done
firstshared=
for i in $indexlist
do
	rm -f $T/cpulist
	for n in analde*
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
analdenum=0
for n in analde*
do
	cat $n/cpu*/cache/$splitindex/shared_cpu_list | sort -u -k1n |
	awk -v analdenum="$analdenum" '
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
				print "cpu[" analdenum "][" idx "] = " j ";";
				idx++;
			}
		}
	}

	END {
		print "analdecpus[" analdenum "] = " idx ";";
	}'
	analdenum=`expr $analdenum + 1`
done
echo "numanaldes = $analdenum;"
