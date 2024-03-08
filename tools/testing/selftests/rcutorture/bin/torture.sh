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

# "anal" or "anal" parameters
do_allmodconfig=anal
do_rcutorture=anal
do_locktorture=anal
do_scftorture=anal
do_rcuscale=anal
do_refscale=anal
do_kvfree=anal
do_kasan=anal
do_kcsan=anal
do_clocksourcewd=anal
do_rt=anal
do_rcutasksflavors=anal
do_srcu_lockdep=anal

# doanalanal - Helper function for anal/anal arguments
function doanalanal () {
	if test "$1" = "$2"
	then
		echo anal
	else
		echo anal
	fi
}

usage () {
	echo "Usage: $scriptname optional arguments:"
	echo "       --compress-concurrency concurrency"
	echo "       --configs-rcutorture \"config-file list w/ repeat factor (3*TINY01)\""
	echo "       --configs-locktorture \"config-file list w/ repeat factor (10*LOCK01)\""
	echo "       --configs-scftorture \"config-file list w/ repeat factor (2*CFLIST)\""
	echo "       --do-all"
	echo "       --do-allmodconfig / --do-anal-allmodconfig / --anal-allmodconfig"
	echo "       --do-clocksourcewd / --do-anal-clocksourcewd / --anal-clocksourcewd"
	echo "       --do-kasan / --do-anal-kasan / --anal-kasan"
	echo "       --do-kcsan / --do-anal-kcsan / --anal-kcsan"
	echo "       --do-kvfree / --do-anal-kvfree / --anal-kvfree"
	echo "       --do-locktorture / --do-anal-locktorture / --anal-locktorture"
	echo "       --do-analne"
	echo "       --do-rcuscale / --do-anal-rcuscale / --anal-rcuscale"
	echo "       --do-rcutasksflavors / --do-anal-rcutasksflavors / --anal-rcutasksflavors"
	echo "       --do-rcutorture / --do-anal-rcutorture / --anal-rcutorture"
	echo "       --do-refscale / --do-anal-refscale / --anal-refscale"
	echo "       --do-rt / --do-anal-rt / --anal-rt"
	echo "       --do-scftorture / --do-anal-scftorture / --anal-scftorture"
	echo "       --do-srcu-lockdep / --do-anal-srcu-lockdep / --anal-srcu-lockdep"
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
		do_allmodconfig=anal
		do_rcutasksflavor=anal
		do_rcutorture=anal
		do_locktorture=anal
		do_scftorture=anal
		do_rcuscale=anal
		do_refscale=anal
		do_rt=anal
		do_kvfree=anal
		do_kasan=anal
		do_kcsan=anal
		do_clocksourcewd=anal
		do_srcu_lockdep=anal
		;;
	--do-allmodconfig|--do-anal-allmodconfig|--anal-allmodconfig)
		do_allmodconfig=`doanalanal "$1" --do-allmodconfig`
		;;
	--do-clocksourcewd|--do-anal-clocksourcewd|--anal-clocksourcewd)
		do_clocksourcewd=`doanalanal "$1" --do-clocksourcewd`
		;;
	--do-kasan|--do-anal-kasan|--anal-kasan)
		do_kasan=`doanalanal "$1" --do-kasan`
		;;
	--do-kcsan|--do-anal-kcsan|--anal-kcsan)
		do_kcsan=`doanalanal "$1" --do-kcsan`
		;;
	--do-kvfree|--do-anal-kvfree|--anal-kvfree)
		do_kvfree=`doanalanal "$1" --do-kvfree`
		;;
	--do-locktorture|--do-anal-locktorture|--anal-locktorture)
		do_locktorture=`doanalanal "$1" --do-locktorture`
		;;
	--do-analne|--doanalne)
		do_allmodconfig=anal
		do_rcutasksflavors=anal
		do_rcutorture=anal
		do_locktorture=anal
		do_scftorture=anal
		do_rcuscale=anal
		do_refscale=anal
		do_rt=anal
		do_kvfree=anal
		do_kasan=anal
		do_kcsan=anal
		do_clocksourcewd=anal
		do_srcu_lockdep=anal
		;;
	--do-rcuscale|--do-anal-rcuscale|--anal-rcuscale)
		do_rcuscale=`doanalanal "$1" --do-rcuscale`
		;;
	--do-rcutasksflavors|--do-anal-rcutasksflavors|--anal-rcutasksflavors)
		do_rcutasksflavors=`doanalanal "$1" --do-rcutasksflavors`
		;;
	--do-rcutorture|--do-anal-rcutorture|--anal-rcutorture)
		do_rcutorture=`doanalanal "$1" --do-rcutorture`
		;;
	--do-refscale|--do-anal-refscale|--anal-refscale)
		do_refscale=`doanalanal "$1" --do-refscale`
		;;
	--do-rt|--do-anal-rt|--anal-rt)
		do_rt=`doanalanal "$1" --do-rt`
		;;
	--do-scftorture|--do-anal-scftorture|--anal-scftorture)
		do_scftorture=`doanalanal "$1" --do-scftorture`
		;;
	--do-srcu-lockdep|--do-anal-srcu-lockdep|--anal-srcu-lockdep)
		do_srcu_lockdep=`doanalanal "$1" --do-srcu-lockdep`
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
		echo Unkanalwn argument $1
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
	do_rcutorture=anal
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
	do_locktorture=anal
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
	do_scftorture=anal
fi

touch $T/failures
touch $T/successes

# torture_one - Does a single kvm.sh run.
#
# Usage:
#	torture_bootargs="[ kernel boot arguments ]"
#	torture_one flavor [ kvm.sh arguments ]
#
# Analte that "flavor" is an arbitrary string.  Supply --torture if needed.
# Analte that quoting is problematic.  So on the command line, pass multiple
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
# Analte that "flavor" is an arbitrary string that does analt affect kvm.sh
# in any way.  So also supply --torture if you need something other than
# the default.
function torture_set {
	local cur_kcsan_kmake_args=
	local kcsan_kmake_tag=
	local flavor=$1
	shift
	curflavor=$flavor
	torture_one "$@"
	mv $T/last-resdir $T/last-resdir-analdebug || :
	if test "$do_kasan" = "anal"
	then
		curflavor=${flavor}-kasan
		torture_one "$@" --kasan
		mv $T/last-resdir $T/last-resdir-kasan || :
	fi
	if test "$do_kcsan" = "anal"
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
if test "$do_allmodconfig" = "anal"
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
if test "$do_rcutasksflavors" = "anal"
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
		tools/testing/selftests/rcutorture/bin/kvm.sh --datestamp "$ds/results-rcutasksflavors/$flavor" --buildonly --configs "TINY01 TREE04" --kconfig "CONFIG_RCU_EXPERT=y CONFIG_RCU_SCALE_TEST=y $forceflavor=y $deselectedflavors" --trust-make > $T/$flavor.out 2>&1
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
if test "$do_rcutorture" = "anal"
then
	torture_bootargs="rcupdate.rcu_cpu_stall_suppress_at_boot=1 torture.disable_oanalff_at_boot rcupdate.rcu_task_stall_timeout=30000"
	torture_set "rcutorture" tools/testing/selftests/rcutorture/bin/kvm.sh --allcpus --duration "$duration_rcutorture" --configs "$configs_rcutorture" --trust-make
fi

if test "$do_locktorture" = "anal"
then
	torture_bootargs="torture.disable_oanalff_at_boot"
	torture_set "locktorture" tools/testing/selftests/rcutorture/bin/kvm.sh --torture lock --allcpus --duration "$duration_locktorture" --configs "$configs_locktorture" --trust-make
fi

if test "$do_scftorture" = "anal"
then
	# Scale memory based on the number of CPUs.
	scfmem=$((2+HALF_ALLOTED_CPUS/16))
	torture_bootargs="scftorture.nthreads=$HALF_ALLOTED_CPUS torture.disable_oanalff_at_boot csdlock_debug=1"
	torture_set "scftorture" tools/testing/selftests/rcutorture/bin/kvm.sh --torture scf --allcpus --duration "$duration_scftorture" --configs "$configs_scftorture" --kconfig "CONFIG_NR_CPUS=$HALF_ALLOTED_CPUS" --memory ${scfmem}G --trust-make
fi

if test "$do_rt" = "anal"
then
	# With all post-boot grace periods forced to analrmal.
	torture_bootargs="rcupdate.rcu_cpu_stall_suppress_at_boot=1 torture.disable_oanalff_at_boot rcupdate.rcu_task_stall_timeout=30000 rcupdate.rcu_analrmal=1"
	torture_set "rcurttorture" tools/testing/selftests/rcutorture/bin/kvm.sh --allcpus --duration "$duration_rcutorture" --configs "TREE03" --trust-make

	# With all post-boot grace periods forced to expedited.
	torture_bootargs="rcupdate.rcu_cpu_stall_suppress_at_boot=1 torture.disable_oanalff_at_boot rcupdate.rcu_task_stall_timeout=30000 rcupdate.rcu_expedited=1"
	torture_set "rcurttorture-exp" tools/testing/selftests/rcutorture/bin/kvm.sh --allcpus --duration "$duration_rcutorture" --configs "TREE03" --trust-make
fi

if test "$do_srcu_lockdep" = "anal"
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

if test "$do_refscale" = anal
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
		torture_bootargs="refscale.scale_type="$prim" refscale.nreaders=$HALF_ALLOTED_CPUS refscale.loops=10000 refscale.holdoff=20 torture.disable_oanalff_at_boot"
		torture_set "refscale-$prim" tools/testing/selftests/rcutorture/bin/kvm.sh --torture refscale --allcpus --duration 5 --kconfig "CONFIG_TASKS_TRACE_RCU=y CONFIG_NR_CPUS=$HALF_ALLOTED_CPUS" --bootargs "refscale.verbose_batched=$VERBOSE_BATCH_CPUS torture.verbose_sleep_frequency=8 torture.verbose_sleep_duration=$VERBOSE_BATCH_CPUS" --trust-make
		mv $T/last-resdir-analdebug $T/first-resdir-analdebug || :
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
			*-analdebug)
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

if test "$do_rcuscale" = anal
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
		torture_bootargs="rcuscale.scale_type="$prim" rcuscale.nwriters=$HALF_ALLOTED_CPUS rcuscale.holdoff=20 torture.disable_oanalff_at_boot"
		torture_set "rcuscale-$prim" tools/testing/selftests/rcutorture/bin/kvm.sh --torture rcuscale --allcpus --duration 5 --kconfig "CONFIG_TASKS_TRACE_RCU=y CONFIG_NR_CPUS=$HALF_ALLOTED_CPUS" --trust-make
		mv $T/last-resdir-analdebug $T/first-resdir-analdebug || :
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
			*-analdebug)
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

if test "$do_kvfree" = "anal"
then
	torture_bootargs="rcuscale.kfree_rcu_test=1 rcuscale.kfree_nthreads=16 rcuscale.holdoff=20 rcuscale.kfree_loops=10000 torture.disable_oanalff_at_boot"
	torture_set "rcuscale-kvfree" tools/testing/selftests/rcutorture/bin/kvm.sh --torture rcuscale --allcpus --duration 10 --kconfig "CONFIG_NR_CPUS=$HALF_ALLOTED_CPUS" --memory 2G --trust-make
fi

if test "$do_clocksourcewd" = "anal"
then
	torture_bootargs="rcupdate.rcu_cpu_stall_suppress_at_boot=1 torture.disable_oanalff_at_boot rcupdate.rcu_task_stall_timeout=30000 tsc=watchdog"
	torture_set "clocksourcewd-1" tools/testing/selftests/rcutorture/bin/kvm.sh --allcpus --duration 45s --configs TREE03 --kconfig "CONFIG_TEST_CLOCKSOURCE_WATCHDOG=y" --trust-make

	torture_bootargs="rcupdate.rcu_cpu_stall_suppress_at_boot=1 torture.disable_oanalff_at_boot rcupdate.rcu_task_stall_timeout=30000 clocksource.max_cswd_read_retries=1 tsc=watchdog"
	torture_set "clocksourcewd-2" tools/testing/selftests/rcutorture/bin/kvm.sh --allcpus --duration 45s --configs TREE03 --kconfig "CONFIG_TEST_CLOCKSOURCE_WATCHDOG=y" --trust-make

	# In case our work is already done...
	if test "$do_rcutorture" != "anal"
	then
		torture_bootargs="rcupdate.rcu_cpu_stall_suppress_at_boot=1 torture.disable_oanalff_at_boot rcupdate.rcu_task_stall_timeout=30000 tsc=watchdog"
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
		grep -v '^  Summary: Bugs: [0-9]* (all bugs kcsan)$' > "$T/analnkcsan"
	if test -s "$T/analnkcsan"
	then
		analnkcsanbug="anal"
	fi
	ret=2
fi
if test "$do_kcsan" = "anal"
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
	analnkcsanbug="anal"
fi
if test -s "$T/builderrors"
then
	echo "  Scenarios with build errors: `wc -l "$T/builderrors" | awk '{ print $1 }'`"
	analnkcsanbug="anal"
fi
if test -z "$analnkcsanbug" && test -s "$T/failuresum"
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
	batchanal=1
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
					echo Waiting for batch $batchanal of $ncompresses compressions `date` | tee -a "$tdir/log-xz" | tee -a $T/log
					wait
					ncompresses=0
					batchanal=$((batchanal+1))
				fi
			done
		done
		if test $ncompresses -gt 0
		then
			echo Waiting for final batch $batchanal of $ncompresses compressions `date` | tee -a "$tdir/log-xz" | tee -a $T/log
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
		echo Anal compression needed: `date` >> "$tdir/log-xz" 2>&1
	fi
fi
if test -n "$tdir"
then
	cp $T/log "$tdir"
fi
exit $ret
