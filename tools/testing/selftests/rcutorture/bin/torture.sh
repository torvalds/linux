#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Run a series of torture tests, intended for overnight or
# longer timeframes, and also for large systems.
#
# Usage: torture.sh [ options ]
#
# Copyright (C) 2020 Facebook, Inc.
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

scriptname=$0
args="$*"

KVM="`pwd`/tools/testing/selftests/rcutorture"; export KVM
PATH=${KVM}/bin:$PATH; export PATH
. functions.sh

TORTURE_ALLOTED_CPUS="`identify_qemu_vcpus`"
MAKE_ALLOTED_CPUS=$((TORTURE_ALLOTED_CPUS*2))
HALF_ALLOTED_CPUS=$((TORTURE_ALLOTED_CPUS/2))
if test "$HALF_ALLOTED_CPUS" -lt 1
then
	HALF_ALLOTED_CPUS=1
fi
VERBOSE_BATCH_CPUS=$((TORTURE_ALLOTED_CPUS/16))
if test "$VERBOSE_BATCH_CPUS" -lt 2
then
	VERBOSE_BATCH_CPUS=0
fi

# Configurations/scenarios.
configs_rcutorture=
configs_locktorture=
configs_scftorture=
kcsan_kmake_args=

# Default compression, duration, and apportionment.
compress_concurrency="`identify_qemu_vcpus`"
duration_base=10
duration_rcutorture_frac=7
duration_locktorture_frac=1
duration_scftorture_frac=2

# "yes" or "no" parameters
do_allmodconfig=yes
do_rcutorture=yes
do_locktorture=yes
do_scftorture=yes
do_rcuscale=yes
do_refscale=yes
do_kvfree=yes
do_kasan=yes
do_kcsan=no
do_clocksourcewd=yes

# doyesno - Helper function for yes/no arguments
function doyesno () {
	if test "$1" = "$2"
	then
		echo yes
	else
		echo no
	fi
}

usage () {
	echo "Usage: $scriptname optional arguments:"
	echo "       --compress-concurrency concurrency"
	echo "       --configs-rcutorture \"config-file list w/ repeat factor (3*TINY01)\""
	echo "       --configs-locktorture \"config-file list w/ repeat factor (10*LOCK01)\""
	echo "       --configs-scftorture \"config-file list w/ repeat factor (2*CFLIST)\""
	echo "       --doall"
	echo "       --doallmodconfig / --do-no-allmodconfig"
	echo "       --do-clocksourcewd / --do-no-clocksourcewd"
	echo "       --do-kasan / --do-no-kasan"
	echo "       --do-kcsan / --do-no-kcsan"
	echo "       --do-kvfree / --do-no-kvfree"
	echo "       --do-locktorture / --do-no-locktorture"
	echo "       --do-none"
	echo "       --do-rcuscale / --do-no-rcuscale"
	echo "       --do-rcutorture / --do-no-rcutorture"
	echo "       --do-refscale / --do-no-refscale"
	echo "       --do-scftorture / --do-no-scftorture"
	echo "       --duration [ <minutes> | <hours>h | <days>d ]"
	echo "       --kcsan-kmake-arg kernel-make-arguments"
	exit 1
}

while test $# -gt 0
do
	case "$1" in
	--compress-concurrency)
		checkarg --compress-concurrency "(concurrency level)" $# "$2" '^[0-9][0-9]*$' '^error'
		compress_concurrency=$2
		shift
		;;
	--config-rcutorture|--configs-rcutorture)
		checkarg --configs-rcutorture "(list of config files)" "$#" "$2" '^[^/]\+$' '^--'
		configs_rcutorture="$configs_rcutorture $2"
		shift
		;;
	--config-locktorture|--configs-locktorture)
		checkarg --configs-locktorture "(list of config files)" "$#" "$2" '^[^/]\+$' '^--'
		configs_locktorture="$configs_locktorture $2"
		shift
		;;
	--config-scftorture|--configs-scftorture)
		checkarg --configs-scftorture "(list of config files)" "$#" "$2" '^[^/]\+$' '^--'
		configs_scftorture="$configs_scftorture $2"
		shift
		;;
	--do-all|--doall)
		do_allmodconfig=yes
		do_rcutorture=yes
		do_locktorture=yes
		do_scftorture=yes
		do_rcuscale=yes
		do_refscale=yes
		do_kvfree=yes
		do_kasan=yes
		do_kcsan=yes
		do_clocksourcewd=yes
		;;
	--do-allmodconfig|--do-no-allmodconfig)
		do_allmodconfig=`doyesno "$1" --do-allmodconfig`
		;;
	--do-clocksourcewd|--do-no-clocksourcewd)
		do_clocksourcewd=`doyesno "$1" --do-clocksourcewd`
		;;
	--do-kasan|--do-no-kasan)
		do_kasan=`doyesno "$1" --do-kasan`
		;;
	--do-kcsan|--do-no-kcsan)
		do_kcsan=`doyesno "$1" --do-kcsan`
		;;
	--do-kvfree|--do-no-kvfree)
		do_kvfree=`doyesno "$1" --do-kvfree`
		;;
	--do-locktorture|--do-no-locktorture)
		do_locktorture=`doyesno "$1" --do-locktorture`
		;;
	--do-none|--donone)
		do_allmodconfig=no
		do_rcutorture=no
		do_locktorture=no
		do_scftorture=no
		do_rcuscale=no
		do_refscale=no
		do_kvfree=no
		do_kasan=no
		do_kcsan=no
		do_clocksourcewd=no
		;;
	--do-rcuscale|--do-no-rcuscale)
		do_rcuscale=`doyesno "$1" --do-rcuscale`
		;;
	--do-rcutorture|--do-no-rcutorture)
		do_rcutorture=`doyesno "$1" --do-rcutorture`
		;;
	--do-refscale|--do-no-refscale)
		do_refscale=`doyesno "$1" --do-refscale`
		;;
	--do-scftorture|--do-no-scftorture)
		do_scftorture=`doyesno "$1" --do-scftorture`
		;;
	--duration)
		checkarg --duration "(minutes)" $# "$2" '^[0-9][0-9]*\(m\|h\|d\|\)$' '^error'
		mult=1
		if echo "$2" | grep -q 'm$'
		then
			mult=1
		elif echo "$2" | grep -q 'h$'
		then
			mult=60
		elif echo "$2" | grep -q 'd$'
		then
			mult=1440
		fi
		ts=`echo $2 | sed -e 's/[smhd]$//'`
		duration_base=$(($ts*mult))
		shift
		;;
	--kcsan-kmake-arg|--kcsan-kmake-args)
		checkarg --kcsan-kmake-arg "(kernel make arguments)" $# "$2" '.*' '^error$'
		kcsan_kmake_args="`echo "$kcsan_kmake_args $2" | sed -e 's/^ *//' -e 's/ *$//'`"
		shift
		;;
	*)
		echo Unknown argument $1
		usage
		;;
	esac
	shift
done

ds="`date +%Y.%m.%d-%H.%M.%S`-torture"
startdate="`date`"
starttime="`get_starttime`"

T=/tmp/torture.sh.$$
trap 'rm -rf $T' 0 2
mkdir $T

echo " --- " $scriptname $args | tee -a $T/log
echo " --- Results directory: " $ds | tee -a $T/log

# Calculate rcutorture defaults and apportion time
if test -z "$configs_rcutorture"
then
	configs_rcutorture=CFLIST
fi
duration_rcutorture=$((duration_base*duration_rcutorture_frac/10))
if test "$duration_rcutorture" -eq 0
then
	echo " --- Zero time for rcutorture, disabling" | tee -a $T/log
	do_rcutorture=no
fi

# Calculate locktorture defaults and apportion time
if test -z "$configs_locktorture"
then
	configs_locktorture=CFLIST
fi
duration_locktorture=$((duration_base*duration_locktorture_frac/10))
if test "$duration_locktorture" -eq 0
then
	echo " --- Zero time for locktorture, disabling" | tee -a $T/log
	do_locktorture=no
fi

# Calculate scftorture defaults and apportion time
if test -z "$configs_scftorture"
then
	configs_scftorture=CFLIST
fi
duration_scftorture=$((duration_base*duration_scftorture_frac/10))
if test "$duration_scftorture" -eq 0
then
	echo " --- Zero time for scftorture, disabling" | tee -a $T/log
	do_scftorture=no
fi

touch $T/failures
touch $T/successes

# torture_one - Does a single kvm.sh run.
#
# Usage:
#	torture_bootargs="[ kernel boot arguments ]"
#	torture_one flavor [ kvm.sh arguments ]
#
# Note that "flavor" is an arbitrary string.  Supply --torture if needed.
# Note that quoting is problematic.  So on the command line, pass multiple
# values with multiple kvm.sh argument instances.
function torture_one {
	local cur_bootargs=
	local boottag=

	echo " --- $curflavor:" Start `date` | tee -a $T/log
	if test -n "$torture_bootargs"
	then
		boottag="--bootargs"
		cur_bootargs="$torture_bootargs"
	fi
	"$@" $boottag "$cur_bootargs" --datestamp "$ds/results-$curflavor" > $T/$curflavor.out 2>&1
	retcode=$?
	resdir="`grep '^Results directory: ' $T/$curflavor.out | tail -1 | sed -e 's/^Results directory: //'`"
	if test -z "$resdir"
	then
		cat $T/$curflavor.out | tee -a $T/log
		echo retcode=$retcode | tee -a $T/log
	fi
	if test "$retcode" == 0
	then
		echo "$curflavor($retcode)" $resdir >> $T/successes
	else
		echo "$curflavor($retcode)" $resdir >> $T/failures
	fi
}

# torture_set - Does a set of tortures with and without KASAN and KCSAN.
#
# Usage:
#	torture_bootargs="[ kernel boot arguments ]"
#	torture_set flavor [ kvm.sh arguments ]
#
# Note that "flavor" is an arbitrary string that does not affect kvm.sh
# in any way.  So also supply --torture if you need something other than
# the default.
function torture_set {
	local cur_kcsan_kmake_args=
	local kcsan_kmake_tag=
	local flavor=$1
	shift
	curflavor=$flavor
	torture_one "$@"
	if test "$do_kasan" = "yes"
	then
		curflavor=${flavor}-kasan
		torture_one "$@" --kasan
	fi
	if test "$do_kcsan" = "yes"
	then
		curflavor=${flavor}-kcsan
		if test -n "$kcsan_kmake_args"
		then
			kcsan_kmake_tag="--kmake-args"
			cur_kcsan_kmake_args="$kcsan_kmake_args"
		fi
		torture_one "$@" --kconfig "CONFIG_DEBUG_LOCK_ALLOC=y CONFIG_PROVE_LOCKING=y" $kcsan_kmake_tag $cur_kcsan_kmake_args --kcsan
	fi
}

# make allmodconfig
if test "$do_allmodconfig" = "yes"
then
	echo " --- allmodconfig:" Start `date` | tee -a $T/log
	amcdir="tools/testing/selftests/rcutorture/res/$ds/allmodconfig"
	mkdir -p "$amcdir"
	echo " --- make clean" > "$amcdir/Make.out" 2>&1
	make -j$MAKE_ALLOTED_CPUS clean >> "$amcdir/Make.out" 2>&1
	echo " --- make allmodconfig" >> "$amcdir/Make.out" 2>&1
	make -j$MAKE_ALLOTED_CPUS allmodconfig >> "$amcdir/Make.out" 2>&1
	echo " --- make " >> "$amcdir/Make.out" 2>&1
	make -j$MAKE_ALLOTED_CPUS >> "$amcdir/Make.out" 2>&1
	retcode="$?"
	echo $retcode > "$amcdir/Make.exitcode"
	if test "$retcode" == 0
	then
		echo "allmodconfig($retcode)" $amcdir >> $T/successes
	else
		echo "allmodconfig($retcode)" $amcdir >> $T/failures
	fi
fi

# --torture rcu
if test "$do_rcutorture" = "yes"
then
	torture_bootargs="rcupdate.rcu_cpu_stall_suppress_at_boot=1 torture.disable_onoff_at_boot rcupdate.rcu_task_stall_timeout=30000"
	torture_set "rcutorture" tools/testing/selftests/rcutorture/bin/kvm.sh --allcpus --duration "$duration_rcutorture" --configs "$configs_rcutorture" --trust-make
fi

if test "$do_locktorture" = "yes"
then
	torture_bootargs="torture.disable_onoff_at_boot"
	torture_set "locktorture" tools/testing/selftests/rcutorture/bin/kvm.sh --torture lock --allcpus --duration "$duration_locktorture" --configs "$configs_locktorture" --trust-make
fi

if test "$do_scftorture" = "yes"
then
	torture_bootargs="scftorture.nthreads=$HALF_ALLOTED_CPUS torture.disable_onoff_at_boot"
	torture_set "scftorture" tools/testing/selftests/rcutorture/bin/kvm.sh --torture scf --allcpus --duration "$duration_scftorture" --configs "$configs_scftorture" --kconfig "CONFIG_NR_CPUS=$HALF_ALLOTED_CPUS" --memory 1G --trust-make
fi

if test "$do_refscale" = yes
then
	primlist="`grep '\.name[ 	]*=' kernel/rcu/refscale.c | sed -e 's/^[^"]*"//' -e 's/".*$//'`"
else
	primlist=
fi
for prim in $primlist
do
	torture_bootargs="refscale.scale_type="$prim" refscale.nreaders=$HALF_ALLOTED_CPUS refscale.loops=10000 refscale.holdoff=20 torture.disable_onoff_at_boot"
	torture_set "refscale-$prim" tools/testing/selftests/rcutorture/bin/kvm.sh --torture refscale --allcpus --duration 5 --kconfig "CONFIG_NR_CPUS=$HALF_ALLOTED_CPUS" --bootargs "verbose_batched=$VERBOSE_BATCH_CPUS torture.verbose_sleep_frequency=8 torture.verbose_sleep_duration=$VERBOSE_BATCH_CPUS" --trust-make
done

if test "$do_rcuscale" = yes
then
	primlist="`grep '\.name[ 	]*=' kernel/rcu/rcuscale.c | sed -e 's/^[^"]*"//' -e 's/".*$//'`"
else
	primlist=
fi
for prim in $primlist
do
	torture_bootargs="rcuscale.scale_type="$prim" rcuscale.nwriters=$HALF_ALLOTED_CPUS rcuscale.holdoff=20 torture.disable_onoff_at_boot"
	torture_set "rcuscale-$prim" tools/testing/selftests/rcutorture/bin/kvm.sh --torture rcuscale --allcpus --duration 5 --kconfig "CONFIG_NR_CPUS=$HALF_ALLOTED_CPUS" --trust-make
done

if test "$do_kvfree" = "yes"
then
	torture_bootargs="rcuscale.kfree_rcu_test=1 rcuscale.kfree_nthreads=16 rcuscale.holdoff=20 rcuscale.kfree_loops=10000 torture.disable_onoff_at_boot"
	torture_set "rcuscale-kvfree" tools/testing/selftests/rcutorture/bin/kvm.sh --torture rcuscale --allcpus --duration 10 --kconfig "CONFIG_NR_CPUS=$HALF_ALLOTED_CPUS" --memory 1G --trust-make
fi

if test "$do_clocksourcewd" = "yes"
then
	torture_bootargs="rcupdate.rcu_cpu_stall_suppress_at_boot=1 torture.disable_onoff_at_boot rcupdate.rcu_task_stall_timeout=30000"
	torture_set "clocksourcewd-1" tools/testing/selftests/rcutorture/bin/kvm.sh --allcpus --duration 45s --configs TREE03 --kconfig "CONFIG_TEST_CLOCKSOURCE_WATCHDOG=y" --trust-make

	torture_bootargs="rcupdate.rcu_cpu_stall_suppress_at_boot=1 torture.disable_onoff_at_boot rcupdate.rcu_task_stall_timeout=30000 clocksource.max_cswd_read_retries=1"
	torture_set "clocksourcewd-2" tools/testing/selftests/rcutorture/bin/kvm.sh --allcpus --duration 45s --configs TREE03 --kconfig "CONFIG_TEST_CLOCKSOURCE_WATCHDOG=y" --trust-make

	# In case our work is already done...
	if test "$do_rcutorture" != "yes"
	then
		torture_bootargs="rcupdate.rcu_cpu_stall_suppress_at_boot=1 torture.disable_onoff_at_boot rcupdate.rcu_task_stall_timeout=30000"
		torture_set "clocksourcewd-3" tools/testing/selftests/rcutorture/bin/kvm.sh --allcpus --duration 45s --configs TREE03 --trust-make
	fi
fi

echo " --- " $scriptname $args
echo " --- " Done `date` | tee -a $T/log
ret=0
nsuccesses=0
echo SUCCESSES: | tee -a $T/log
if test -s "$T/successes"
then
	cat "$T/successes" | tee -a $T/log
	nsuccesses="`wc -l "$T/successes" | awk '{ print $1 }'`"
fi
nfailures=0
echo FAILURES: | tee -a $T/log
if test -s "$T/failures"
then
	awk < "$T/failures" -v sq="'" '{ print "echo " sq $0 sq; print "sed -e " sq "1,/^ --- .* Test summary:$/d" sq " " $2 "/log | grep Summary: | sed -e " sq "s/^[^S]*/  /" sq; }' | sh | tee -a $T/log | tee "$T/failuresum"
	nfailures="`wc -l "$T/failures" | awk '{ print $1 }'`"
	grep "^  Summary: " "$T/failuresum" |
		grep -v '^  Summary: Bugs: [0-9]* (all bugs kcsan)$' > "$T/nonkcsan"
	if test -s "$T/nonkcsan"
	then
		nonkcsanbug="yes"
	fi
	ret=2
fi
if test "$do_kcsan" = "yes"
then
	TORTURE_KCONFIG_KCSAN_ARG=1 tools/testing/selftests/rcutorture/bin/kcsan-collapse.sh tools/testing/selftests/rcutorture/res/$ds > tools/testing/selftests/rcutorture/res/$ds/kcsan.sum
fi
echo Started at $startdate, ended at `date`, duration `get_starttime_duration $starttime`. | tee -a $T/log
echo Summary: Successes: $nsuccesses Failures: $nfailures. | tee -a $T/log
if test -z "$nonkcsanbug" && test -s "$T/failuresum"
then
	echo "  All bugs were KCSAN failures."
fi
tdir="`cat $T/successes $T/failures | head -1 | awk '{ print $NF }' | sed -e 's,/[^/]\+/*$,,'`"
if test -n "$tdir" && test $compress_concurrency -gt 0
then
	# KASAN vmlinux files can approach 1GB in size, so compress them.
	echo Looking for K[AC]SAN files to compress: `date` > "$tdir/log-xz" 2>&1
	find "$tdir" -type d -name '*-k[ac]san' -print > $T/xz-todo
	ncompresses=0
	batchno=1
	if test -s $T/xz-todo
	then
		for i in `cat $T/xz-todo`
		do
			find $i -name 'vmlinux*' -print
		done | wc -l | awk '{ print $1 }' > $T/xz-todo-count
		n2compress="`cat $T/xz-todo-count`"
		echo Size before compressing $n2compress files: `du -sh $tdir | awk '{ print $1 }'` `date` 2>&1 | tee -a "$tdir/log-xz" | tee -a $T/log
		for i in `cat $T/xz-todo`
		do
			echo Compressing vmlinux files in ${i}: `date` >> "$tdir/log-xz" 2>&1
			for j in $i/*/vmlinux
			do
				xz "$j" >> "$tdir/log-xz" 2>&1 &
				ncompresses=$((ncompresses+1))
				if test $ncompresses -ge $compress_concurrency
				then
					echo Waiting for batch $batchno of $ncompresses compressions `date` | tee -a "$tdir/log-xz" | tee -a $T/log
					wait
					ncompresses=0
					batchno=$((batchno+1))
				fi
			done
		done
		if test $ncompresses -gt 0
		then
			echo Waiting for final batch $batchno of $ncompresses compressions `date` | tee -a "$tdir/log-xz" | tee -a $T/log
		fi
		wait
		echo Size after compressing $n2compress files: `du -sh $tdir | awk '{ print $1 }'` `date` 2>&1 | tee -a "$tdir/log-xz" | tee -a $T/log
		echo Total duration `get_starttime_duration $starttime`. | tee -a $T/log
	else
		echo No compression needed: `date` >> "$tdir/log-xz" 2>&1
	fi
fi
if test -n "$tdir"
then
	cp $T/log "$tdir"
fi
exit $ret
