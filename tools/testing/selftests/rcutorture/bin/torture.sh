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

RCUTORTURE="`pwd`/tools/testing/selftests/rcutorture"; export RCUTORTURE
PATH=${RCUTORTURE}/bin:$PATH; export PATH
. functions.sh

TORTURE_ALLOTED_CPUS="`identify_qemu_vcpus`"
MAKE_ALLOTED_CPUS=$((TORTURE_ALLOTED_CPUS*2))
SCALE_ALLOTED_CPUS=$((TORTURE_ALLOTED_CPUS/2))
if test "$SCALE_ALLOTED_CPUS" -lt 1
then
	SCALE_ALLOTED_CPUS=1
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
do_rt=yes
do_rcutasksflavors=yes
do_srcu_lockdep=yes

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
	echo "       --do-all"
	echo "       --do-allmodconfig / --do-no-allmodconfig / --no-allmodconfig"
	echo "       --do-clocksourcewd / --do-no-clocksourcewd / --no-clocksourcewd"
	echo "       --do-kasan / --do-no-kasan / --no-kasan"
	echo "       --do-kcsan / --do-no-kcsan / --no-kcsan"
	echo "       --do-kvfree / --do-no-kvfree / --no-kvfree"
	echo "       --do-locktorture / --do-no-locktorture / --no-locktorture"
	echo "       --do-none"
	echo "       --do-rcuscale / --do-no-rcuscale / --no-rcuscale"
	echo "       --do-rcutasksflavors / --do-no-rcutasksflavors / --no-rcutasksflavors"
	echo "       --do-rcutorture / --do-no-rcutorture / --no-rcutorture"
	echo "       --do-refscale / --do-no-refscale / --no-refscale"
	echo "       --do-rt / --do-no-rt / --no-rt"
	echo "       --do-scftorture / --do-no-scftorture / --no-scftorture"
	echo "       --do-srcu-lockdep / --do-no-srcu-lockdep / --no-srcu-lockdep"
	echo "       --duration [ <minutes> | <hours>h | <days>d ]"
	echo "       --guest-cpu-limit N"
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
		do_rcutasksflavor=yes
		do_rcutorture=yes
		do_locktorture=yes
		do_scftorture=yes
		do_rcuscale=yes
		do_refscale=yes
		do_rt=yes
		do_kvfree=yes
		do_kasan=yes
		do_kcsan=yes
		do_clocksourcewd=yes
		do_srcu_lockdep=yes
		;;
	--do-allmodconfig|--do-no-allmodconfig|--no-allmodconfig)
		do_allmodconfig=`doyesno "$1" --do-allmodconfig`
		;;
	--do-clocksourcewd|--do-no-clocksourcewd|--no-clocksourcewd)
		do_clocksourcewd=`doyesno "$1" --do-clocksourcewd`
		;;
	--do-kasan|--do-no-kasan|--no-kasan)
		do_kasan=`doyesno "$1" --do-kasan`
		;;
	--do-kcsan|--do-no-kcsan|--no-kcsan)
		do_kcsan=`doyesno "$1" --do-kcsan`
		;;
	--do-kvfree|--do-no-kvfree|--no-kvfree)
		do_kvfree=`doyesno "$1" --do-kvfree`
		;;
	--do-locktorture|--do-no-locktorture|--no-locktorture)
		do_locktorture=`doyesno "$1" --do-locktorture`
		;;
	--do-none|--donone)
		do_allmodconfig=no
		do_rcutasksflavors=no
		do_rcutorture=no
		do_locktorture=no
		do_scftorture=no
		do_rcuscale=no
		do_refscale=no
		do_rt=no
		do_kvfree=no
		do_kasan=no
		do_kcsan=no
		do_clocksourcewd=no
		do_srcu_lockdep=no
		;;
	--do-rcuscale|--do-no-rcuscale|--no-rcuscale)
		do_rcuscale=`doyesno "$1" --do-rcuscale`
		;;
	--do-rcutasksflavors|--do-no-rcutasksflavors|--no-rcutasksflavors)
		do_rcutasksflavors=`doyesno "$1" --do-rcutasksflavors`
		;;
	--do-rcutorture|--do-no-rcutorture|--no-rcutorture)
		do_rcutorture=`doyesno "$1" --do-rcutorture`
		;;
	--do-refscale|--do-no-refscale|--no-refscale)
		do_refscale=`doyesno "$1" --do-refscale`
		;;
	--do-rt|--do-no-rt|--no-rt)
		do_rt=`doyesno "$1" --do-rt`
		;;
	--do-scftorture|--do-no-scftorture|--no-scftorture)
		do_scftorture=`doyesno "$1" --do-scftorture`
		;;
	--do-srcu-lockdep|--do-no-srcu-lockdep|--no-srcu-lockdep)
		do_srcu_lockdep=`doyesno "$1" --do-srcu-lockdep`
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
	--guest-cpu-limit|--guest-cpu-lim)
		checkarg --guest-cpu-limit "(number)" "$#" "$2" '^[0-9]*$' '^--'
		if (("$2" <= "$TORTURE_ALLOTED_CPUS" / 2))
		then
			SCALE_ALLOTED_CPUS="$2"
			VERBOSE_BATCH_CPUS="$((SCALE_ALLOTED_CPUS/8))"
			if (("$VERBOSE_BATCH_CPUS" < 2))
			then
				VERBOSE_BATCH_CPUS=0
			fi
		else
			echo "Ignoring value of $2 for --guest-cpu-limit which is greater than (("$TORTURE_ALLOTED_CPUS" / 2))."
		fi
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

T="`mktemp -d ${TMPDIR-/tmp}/torture.sh.XXXXXX`"
trap 'rm -rf $T' 0 2

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
	else
		echo $resdir > $T/last-resdir
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
	mv $T/last-resdir $T/last-resdir-nodebug || :
	if test "$do_kasan" = "yes"
	then
		curflavor=${flavor}-kasan
		torture_one "$@" --kasan
		mv $T/last-resdir $T/last-resdir-kasan || :
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
		mv $T/last-resdir $T/last-resdir-kcsan || :
	fi
}

# make allmodconfig
if test "$do_allmodconfig" = "yes"
then
	echo " --- allmodconfig:" Start `date` | tee -a $T/log
	amcdir="tools/testing/selftests/rcutorture/res/$ds/allmodconfig"
	mkdir -p "$amcdir"
	echo " --- make clean" | tee $amcdir/log > "$amcdir/Make.out" 2>&1
	make -j$MAKE_ALLOTED_CPUS clean >> "$amcdir/Make.out" 2>&1
	retcode=$?
	buildphase='"make clean"'
	if test "$retcode" -eq 0
	then
		echo " --- make allmodconfig" | tee -a $amcdir/log >> "$amcdir/Make.out" 2>&1
		cp .config $amcdir
		make -j$MAKE_ALLOTED_CPUS allmodconfig >> "$amcdir/Make.out" 2>&1
		retcode=$?
		buildphase='"make allmodconfig"'
	fi
	if test "$retcode" -eq 0
	then
		echo " --- make " | tee -a $amcdir/log >> "$amcdir/Make.out" 2>&1
		make -j$MAKE_ALLOTED_CPUS >> "$amcdir/Make.out" 2>&1
		retcode="$?"
		echo $retcode > "$amcdir/Make.exitcode"
		buildphase='"make"'
	fi
	if test "$retcode" -eq 0
	then
		echo "allmodconfig($retcode)" $amcdir >> $T/successes
		echo Success >> $amcdir/log
	else
		echo "allmodconfig($retcode)" $amcdir >> $T/failures
		echo " --- allmodconfig Test summary:" >> $amcdir/log
		echo " --- Summary: Exit code $retcode from $buildphase, see Make.out" >> $amcdir/log
	fi
fi

# Test building RCU Tasks flavors in isolation, both SMP and !SMP
if test "$do_rcutasksflavors" = "yes"
then
	echo " --- rcutasksflavors:" Start `date` | tee -a $T/log
	rtfdir="tools/testing/selftests/rcutorture/res/$ds/results-rcutasksflavors"
	mkdir -p "$rtfdir"
	cat > $T/rcutasksflavors << __EOF__
#CHECK#CONFIG_TASKS_RCU=n
#CHECK#CONFIG_TASKS_RUDE_RCU=n
#CHECK#CONFIG_TASKS_TRACE_RCU=n
__EOF__
	for flavor in CONFIG_TASKS_RCU CONFIG_TASKS_RUDE_RCU CONFIG_TASKS_TRACE_RCU
	do
		forceflavor="`echo $flavor | sed -e 's/^CONFIG/CONFIG_FORCE/'`"
		deselectedflavors="`grep -v $flavor $T/rcutasksflavors | tr '\012' ' ' | tr -s ' ' | sed -e 's/ *$//'`"
		echo " --- Running RCU Tasks Trace flavor $flavor `date`" >> $rtfdir/log
		tools/testing/selftests/rcutorture/bin/kvm.sh --datestamp "$ds/results-rcutasksflavors/$flavor" --buildonly --configs "TINY01 TREE04" --kconfig "CONFIG_RCU_EXPERT=y CONFIG_RCU_SCALE_TEST=y CONFIG_KPROBES=n CONFIG_RCU_TRACE=n CONFIG_TRACING=n CONFIG_BLK_DEV_IO_TRACE=n CONFIG_UPROBE_EVENTS=n $forceflavor=y $deselectedflavors" --trust-make > $T/$flavor.out 2>&1
		retcode=$?
		if test "$retcode" -ne 0
		then
			break
		fi
	done
	if test "$retcode" -eq 0
	then
		echo "rcutasksflavors($retcode)" $rtfdir >> $T/successes
		echo Success >> $rtfdir/log
	else
		echo "rcutasksflavors($retcode)" $rtfdir >> $T/failures
		echo " --- rcutasksflavors Test summary:" >> $rtfdir/log
		echo " --- Summary: Exit code $retcode from $flavor, see Make.out" >> $rtfdir/log
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
	# Scale memory based on the number of CPUs.
	scfmem=$((3+SCALE_ALLOTED_CPUS/16))
	torture_bootargs="scftorture.nthreads=$SCALE_ALLOTED_CPUS torture.disable_onoff_at_boot csdlock_debug=1"
	torture_set "scftorture" tools/testing/selftests/rcutorture/bin/kvm.sh --torture scf --allcpus --duration "$duration_scftorture" --configs "$configs_scftorture" --kconfig "CONFIG_NR_CPUS=$SCALE_ALLOTED_CPUS" --memory ${scfmem}G --trust-make
fi

if test "$do_rt" = "yes"
then
	# With all post-boot grace periods forced to normal.
	torture_bootargs="rcupdate.rcu_cpu_stall_suppress_at_boot=1 torture.disable_onoff_at_boot rcupdate.rcu_task_stall_timeout=30000 rcupdate.rcu_normal=1"
	torture_set "rcurttorture" tools/testing/selftests/rcutorture/bin/kvm.sh --allcpus --duration "$duration_rcutorture" --configs "TREE03" --trust-make

	# With all post-boot grace periods forced to expedited.
	torture_bootargs="rcupdate.rcu_cpu_stall_suppress_at_boot=1 torture.disable_onoff_at_boot rcupdate.rcu_task_stall_timeout=30000 rcupdate.rcu_expedited=1"
	torture_set "rcurttorture-exp" tools/testing/selftests/rcutorture/bin/kvm.sh --allcpus --duration "$duration_rcutorture" --configs "TREE03" --trust-make
fi

if test "$do_srcu_lockdep" = "yes"
then
	echo " --- do-srcu-lockdep:" Start `date` | tee -a $T/log
	tools/testing/selftests/rcutorture/bin/srcu_lockdep.sh --datestamp "$ds/results-srcu-lockdep" > $T/srcu_lockdep.sh.out 2>&1
	retcode=$?
	cp $T/srcu_lockdep.sh.out "tools/testing/selftests/rcutorture/res/$ds/results-srcu-lockdep/log"
	if test "$retcode" -eq 0
	then
		echo "srcu_lockdep($retcode)" "tools/testing/selftests/rcutorture/res/$ds/results-srcu-lockdep" >> $T/successes
		echo Success >> "tools/testing/selftests/rcutorture/res/$ds/results-srcu-lockdep/log"
	else
		echo "srcu_lockdep($retcode)" "tools/testing/selftests/rcutorture/res/$ds/results-srcu-lockdep" >> $T/failures
		echo " --- srcu_lockdep Test Summary:" >> "tools/testing/selftests/rcutorture/res/$ds/results-srcu-lockdep/log"
		echo " --- Summary: Exit code $retcode from srcu_lockdep.sh, see ds/results-srcu-lockdep" >> "tools/testing/selftests/rcutorture/res/$ds/results-srcu-lockdep/log"
	fi
fi

if test "$do_refscale" = yes
then
	primlist="`grep '\.name[ 	]*=' kernel/rcu/refscale.c | sed -e 's/^[^"]*"//' -e 's/".*$//'`"
else
	primlist=
fi
firsttime=1
do_kasan_save="$do_kasan"
do_kcsan_save="$do_kcsan"
for prim in $primlist
do
	if test -n "$firsttime"
	then
		torture_bootargs="refscale.scale_type="$prim" refscale.nreaders=$SCALE_ALLOTED_CPUS refscale.loops=10000 refscale.holdoff=20 torture.disable_onoff_at_boot"
		torture_set "refscale-$prim" tools/testing/selftests/rcutorture/bin/kvm.sh --torture refscale --allcpus --duration 5 --kconfig "CONFIG_TASKS_TRACE_RCU=y CONFIG_NR_CPUS=$SCALE_ALLOTED_CPUS" --bootargs "refscale.verbose_batched=$VERBOSE_BATCH_CPUS torture.verbose_sleep_frequency=8 torture.verbose_sleep_duration=$VERBOSE_BATCH_CPUS" --trust-make
		mv $T/last-resdir-nodebug $T/first-resdir-nodebug || :
		if test -f "$T/last-resdir-kasan"
		then
			mv $T/last-resdir-kasan $T/first-resdir-kasan || :
		fi
		if test -f "$T/last-resdir-kcsan"
		then
			mv $T/last-resdir-kcsan $T/first-resdir-kcsan || :
		fi
		firsttime=
		do_kasan=
		do_kcsan=
	else
		torture_bootargs=
		for i in $T/first-resdir-*
		do
			case "$i" in
			*-nodebug)
				torture_suffix=
				;;
			*-kasan)
				torture_suffix="-kasan"
				;;
			*-kcsan)
				torture_suffix="-kcsan"
				;;
			esac
			torture_set "refscale-$prim$torture_suffix" tools/testing/selftests/rcutorture/bin/kvm-again.sh "`cat "$i"`" --duration 5 --bootargs "refscale.scale_type=$prim"
		done
	fi
done
do_kasan="$do_kasan_save"
do_kcsan="$do_kcsan_save"

if test "$do_rcuscale" = yes
then
	primlist="`grep '\.name[ 	]*=' kernel/rcu/rcuscale.c | sed -e 's/^[^"]*"//' -e 's/".*$//'`"
else
	primlist=
fi
firsttime=1
do_kasan_save="$do_kasan"
do_kcsan_save="$do_kcsan"
for prim in $primlist
do
	if test -n "$firsttime"
	then
		torture_bootargs="rcuscale.scale_type="$prim" rcuscale.nwriters=$SCALE_ALLOTED_CPUS rcuscale.holdoff=20 torture.disable_onoff_at_boot"
		torture_set "rcuscale-$prim" tools/testing/selftests/rcutorture/bin/kvm.sh --torture rcuscale --allcpus --duration 5 --kconfig "CONFIG_TASKS_TRACE_RCU=y CONFIG_NR_CPUS=$SCALE_ALLOTED_CPUS" --trust-make
		mv $T/last-resdir-nodebug $T/first-resdir-nodebug || :
		if test -f "$T/last-resdir-kasan"
		then
			mv $T/last-resdir-kasan $T/first-resdir-kasan || :
		fi
		if test -f "$T/last-resdir-kcsan"
		then
			mv $T/last-resdir-kcsan $T/first-resdir-kcsan || :
		fi
		firsttime=
		do_kasan=
		do_kcsan=
	else
		torture_bootargs=
		for i in $T/first-resdir-*
		do
			case "$i" in
			*-nodebug)
				torture_suffix=
				;;
			*-kasan)
				torture_suffix="-kasan"
				;;
			*-kcsan)
				torture_suffix="-kcsan"
				;;
			esac
			torture_set "rcuscale-$prim$torture_suffix" tools/testing/selftests/rcutorture/bin/kvm-again.sh "`cat "$i"`" --duration 5 --bootargs "rcuscale.scale_type=$prim"
		done
	fi
done
do_kasan="$do_kasan_save"
do_kcsan="$do_kcsan_save"

if test "$do_kvfree" = "yes"
then
	torture_bootargs="rcuscale.kfree_rcu_test=1 rcuscale.kfree_nthreads=16 rcuscale.holdoff=20 rcuscale.kfree_loops=10000 torture.disable_onoff_at_boot"
	torture_set "rcuscale-kvfree" tools/testing/selftests/rcutorture/bin/kvm.sh --torture rcuscale --allcpus --duration $duration_rcutorture --kconfig "CONFIG_NR_CPUS=$SCALE_ALLOTED_CPUS" --memory 2G --trust-make
fi

if test "$do_clocksourcewd" = "yes"
then
	torture_bootargs="rcupdate.rcu_cpu_stall_suppress_at_boot=1 torture.disable_onoff_at_boot rcupdate.rcu_task_stall_timeout=30000 tsc=watchdog"
	torture_set "clocksourcewd-1" tools/testing/selftests/rcutorture/bin/kvm.sh --allcpus --duration 45s --configs TREE03 --kconfig "CONFIG_TEST_CLOCKSOURCE_WATCHDOG=y" --trust-make

	torture_bootargs="rcupdate.rcu_cpu_stall_suppress_at_boot=1 torture.disable_onoff_at_boot rcupdate.rcu_task_stall_timeout=30000 tsc=watchdog"
	torture_set "clocksourcewd-2" tools/testing/selftests/rcutorture/bin/kvm.sh --allcpus --duration 45s --configs TREE03 --kconfig "CONFIG_TEST_CLOCKSOURCE_WATCHDOG=y" --trust-make

	# In case our work is already done...
	if test "$do_rcutorture" != "yes"
	then
		torture_bootargs="rcupdate.rcu_cpu_stall_suppress_at_boot=1 torture.disable_onoff_at_boot rcupdate.rcu_task_stall_timeout=30000 tsc=watchdog"
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
tdir="`cat $T/successes $T/failures | head -1 | awk '{ print $NF }' | sed -e 's,/[^/]\+/*$,,'`"
find "$tdir" -name 'ConfigFragment.diags' -print > $T/configerrors
find "$tdir" -name 'Make.out.diags' -print > $T/builderrors
if test -s "$T/configerrors"
then
	echo "  Scenarios with .config errors: `wc -l "$T/configerrors" | awk '{ print $1 }'`"
	nonkcsanbug="yes"
fi
if test -s "$T/builderrors"
then
	echo "  Scenarios with build errors: `wc -l "$T/builderrors" | awk '{ print $1 }'`"
	nonkcsanbug="yes"
fi
if test -z "$nonkcsanbug" && test -s "$T/failuresum"
then
	echo "  All bugs were KCSAN failures."
fi
if test -n "$tdir" && test $compress_concurrency -gt 0
then
	# KASAN vmlinux files can approach 1GB in size, so compress them.
	echo Looking for K[AC]SAN files to compress: `date` > "$tdir/log-xz" 2>&1
	find "$tdir" -type d -name '*-k[ac]san' -print > $T/xz-todo-all
	find "$tdir" -type f -name 're-run' -print | sed -e 's,/re-run,,' |
		grep -e '-k[ac]san$' > $T/xz-todo-copy
	sort $T/xz-todo-all $T/xz-todo-copy | uniq -u > $T/xz-todo
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
		if test -s $T/xz-todo-copy
		then
			# The trick here is that we need corresponding
			# vmlinux files from corresponding scenarios.
			echo Linking vmlinux.xz files to re-use scenarios `date` | tee -a "$tdir/log-xz" | tee -a $T/log
			dirstash="`pwd`"
			for i in `cat $T/xz-todo-copy`
			do
				cd $i
				find . -name vmlinux -print > $T/xz-todo-copy-vmlinux
				for v in `cat $T/xz-todo-copy-vmlinux`
				do
					rm -f "$v"
					cp -l `cat $i/re-run`/"$i/$v".xz "`dirname "$v"`"
				done
				cd "$dirstash"
			done
		fi
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
