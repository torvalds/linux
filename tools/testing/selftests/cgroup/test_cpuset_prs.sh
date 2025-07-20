#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test for cpuset v2 partition root state (PRS)
#
# The sched verbose flag can be optionally set so that the console log
# can be examined for the correct setting of scheduling domain.
#

skip_test() {
	echo "$1"
	echo "Test SKIPPED"
	exit 4 # ksft_skip
}

[[ $(id -u) -eq 0 ]] || skip_test "Test must be run as root!"


# Get wait_inotify location
WAIT_INOTIFY=$(cd $(dirname $0); pwd)/wait_inotify

# Find cgroup v2 mount point
CGROUP2=$(mount -t cgroup2 | head -1 | awk -e '{print $3}')
[[ -n "$CGROUP2" ]] || skip_test "Cgroup v2 mount point not found!"
SUBPARTS_CPUS=$CGROUP2/.__DEBUG__.cpuset.cpus.subpartitions
CPULIST=$(cat $CGROUP2/cpuset.cpus.effective)

NR_CPUS=$(lscpu | grep "^CPU(s):" | sed -e "s/.*:[[:space:]]*//")
[[ $NR_CPUS -lt 8 ]] && skip_test "Test needs at least 8 cpus available!"

# Check to see if /dev/console exists and is writable
if [[ -c /dev/console && -w /dev/console ]]
then
	CONSOLE=/dev/console
else
	CONSOLE=/dev/null
fi

# Set verbose flag and delay factor
PROG=$1
VERBOSE=0
DELAY_FACTOR=1
SCHED_DEBUG=
while [[ "$1" = -* ]]
do
	case "$1" in
		-v) ((VERBOSE++))
		    # Enable sched/verbose can slow thing down
		    [[ $DELAY_FACTOR -eq 1 ]] &&
			DELAY_FACTOR=2
		    ;;
		-d) DELAY_FACTOR=$2
		    shift
		    ;;
		*)  echo "Usage: $PROG [-v] [-d <delay-factor>"
		    exit
		    ;;
	esac
	shift
done

# Set sched verbose flag if available when "-v" option is specified
if [[ $VERBOSE -gt 0 && -d /sys/kernel/debug/sched ]]
then
	# Used to restore the original setting during cleanup
	SCHED_DEBUG=$(cat /sys/kernel/debug/sched/verbose)
	echo Y > /sys/kernel/debug/sched/verbose
fi

cd $CGROUP2
echo +cpuset > cgroup.subtree_control

#
# If cpuset has been set up and used in child cgroups, we may not be able to
# create partition under root cgroup because of the CPU exclusivity rule.
# So we are going to skip the test if this is the case.
#
[[ -d test ]] || mkdir test
echo 0-6 > test/cpuset.cpus
echo root > test/cpuset.cpus.partition
cat test/cpuset.cpus.partition | grep -q invalid
RESULT=$?
echo member > test/cpuset.cpus.partition
echo "" > test/cpuset.cpus
[[ $RESULT -eq 0 ]] && skip_test "Child cgroups are using cpuset!"

#
# If isolated CPUs have been reserved at boot time (as shown in
# cpuset.cpus.isolated), these isolated CPUs should be outside of CPUs 0-8
# that will be used by this script for testing purpose. If not, some of
# the tests may fail incorrectly. Wait a bit and retry again just in case
# these isolated CPUs are leftover from previous run and have just been
# cleaned up earlier in this script.
#
# These pre-isolated CPUs should stay in an isolated state throughout the
# testing process for now.
#
BOOT_ISOLCPUS=$(cat $CGROUP2/cpuset.cpus.isolated)
[[ -n "$BOOT_ISOLCPUS" ]] && {
	sleep 0.5
	BOOT_ISOLCPUS=$(cat $CGROUP2/cpuset.cpus.isolated)
}
if [[ -n "$BOOT_ISOLCPUS" ]]
then
	[[ $(echo $BOOT_ISOLCPUS | sed -e "s/[,-].*//") -le 8 ]] &&
		skip_test "Pre-isolated CPUs ($BOOT_ISOLCPUS) overlap CPUs to be tested"
	echo "Pre-isolated CPUs: $BOOT_ISOLCPUS"
fi

cleanup()
{
	online_cpus
	cd $CGROUP2
	rmdir A1/A2/A3 A1/A2 A1 B1 test/A1 test/B1 test > /dev/null 2>&1
	rmdir rtest/p1/c11 rtest/p1/c12 rtest/p2/c21 \
	      rtest/p2/c22 rtest/p1 rtest/p2 rtest > /dev/null 2>&1
	[[ -n "$SCHED_DEBUG" ]] &&
		echo "$SCHED_DEBUG" > /sys/kernel/debug/sched/verbose
}

# Pause in ms
pause()
{
	DELAY=$1
	LOOP=0
	while [[ $LOOP -lt $DELAY_FACTOR ]]
	do
		sleep $DELAY
		((LOOP++))
	done
	return 0
}

console_msg()
{
	MSG=$1
	echo "$MSG"
	echo "" > $CONSOLE
	echo "$MSG" > $CONSOLE
	pause 0.01
}

test_partition()
{
	EXPECTED_VAL=$1
	echo $EXPECTED_VAL > cpuset.cpus.partition
	[[ $? -eq 0 ]] || exit 1
	ACTUAL_VAL=$(cat cpuset.cpus.partition)
	[[ $ACTUAL_VAL != $EXPECTED_VAL ]] && {
		echo "cpuset.cpus.partition: expect $EXPECTED_VAL, found $ACTUAL_VAL"
		echo "Test FAILED"
		exit 1
	}
}

test_effective_cpus()
{
	EXPECTED_VAL=$1
	ACTUAL_VAL=$(cat cpuset.cpus.effective)
	[[ "$ACTUAL_VAL" != "$EXPECTED_VAL" ]] && {
		echo "cpuset.cpus.effective: expect '$EXPECTED_VAL', found '$ACTUAL_VAL'"
		echo "Test FAILED"
		exit 1
	}
}

# Adding current process to cgroup.procs as a test
test_add_proc()
{
	OUTSTR="$1"
	ERRMSG=$((echo $$ > cgroup.procs) |& cat)
	echo $ERRMSG | grep -q "$OUTSTR"
	[[ $? -ne 0 ]] && {
		echo "cgroup.procs: expect '$OUTSTR', got '$ERRMSG'"
		echo "Test FAILED"
		exit 1
	}
	echo $$ > $CGROUP2/cgroup.procs	# Move out the task
}

#
# Cpuset controller state transition test matrix.
#
# Cgroup test hierarchy
#
#	      root
#	        |
#	 +------+------+
#	 |             |
#	 A1            B1
#	 |
#	 A2
#	 |
#	 A3
#
#  P<v> = set cpus.partition (0:member, 1:root, 2:isolated)
#  C<l> = add cpu-list to cpuset.cpus
#  X<l> = add cpu-list to cpuset.cpus.exclusive
#  S<p> = use prefix in subtree_control
#  T    = put a task into cgroup
#  CX<l> = add cpu-list to both cpuset.cpus and cpuset.cpus.exclusive
#  O<c>=<v> = Write <v> to CPU online file of <c>
#
# ECPUs    - effective CPUs of cpusets
# Pstate   - partition root state
# ISOLCPUS - isolated CPUs (<icpus>[,<icpus2>])
#
# Note that if there are 2 fields in ISOLCPUS, the first one is for
# sched-debug matching which includes offline CPUs and single-CPU partitions
# while the second one is for matching cpuset.cpus.isolated.
#
SETUP_A123_PARTITIONS="C1-3:P1:S+ C2-3:P1:S+ C3:P1"
TEST_MATRIX=(
	#  old-A1 old-A2 old-A3 old-B1 new-A1 new-A2 new-A3 new-B1 fail ECPUs Pstate ISOLCPUS
	#  ------ ------ ------ ------ ------ ------ ------ ------ ---- ----- ------ --------
	"   C0-1     .      .    C2-3    S+    C4-5     .      .     0 A2:0-1"
	"   C0-1     .      .    C2-3    P1      .      .      .     0 "
	"   C0-1     .      .    C2-3   P1:S+ C0-1:P1   .      .     0 "
	"   C0-1     .      .    C2-3   P1:S+  C1:P1    .      .     0 "
	"  C0-1:S+   .      .    C2-3     .      .      .     P1     0 "
	"  C0-1:P1   .      .    C2-3    S+     C1      .      .     0 "
	"  C0-1:P1   .      .    C2-3    S+    C1:P1    .      .     0 "
	"  C0-1:P1   .      .    C2-3    S+    C1:P1    .     P1     0 "
	"  C0-1:P1   .      .    C2-3   C4-5     .      .      .     0 A1:4-5"
	"  C0-1:P1   .      .    C2-3  S+:C4-5   .      .      .     0 A1:4-5"
	"   C0-1     .      .   C2-3:P1   .      .      .     C2     0 "
	"   C0-1     .      .   C2-3:P1   .      .      .    C4-5    0 B1:4-5"
	"C0-3:P1:S+ C2-3:P1 .      .      .      .      .      .     0 A1:0-1|A2:2-3|XA2:2-3"
	"C0-3:P1:S+ C2-3:P1 .      .     C1-3    .      .      .     0 A1:1|A2:2-3|XA2:2-3"
	"C2-3:P1:S+  C3:P1  .      .     C3      .      .      .     0 A1:|A2:3|XA2:3 A1:P1|A2:P1"
	"C2-3:P1:S+  C3:P1  .      .     C3      P0     .      .     0 A1:3|A2:3 A1:P1|A2:P0"
	"C2-3:P1:S+  C2:P1  .      .     C2-4    .      .      .     0 A1:3-4|A2:2"
	"C2-3:P1:S+  C3:P1  .      .     C3      .      .     C0-2   0 A1:|B1:0-2 A1:P1|A2:P1"
	"$SETUP_A123_PARTITIONS    .     C2-3    .      .      .     0 A1:|A2:2|A3:3 A1:P1|A2:P1|A3:P1"

	# CPU offlining cases:
	"   C0-1     .      .    C2-3    S+    C4-5     .     O2=0   0 A1:0-1|B1:3"
	"C0-3:P1:S+ C2-3:P1 .      .     O2=0    .      .      .     0 A1:0-1|A2:3"
	"C0-3:P1:S+ C2-3:P1 .      .     O2=0   O2=1    .      .     0 A1:0-1|A2:2-3"
	"C0-3:P1:S+ C2-3:P1 .      .     O1=0    .      .      .     0 A1:0|A2:2-3"
	"C0-3:P1:S+ C2-3:P1 .      .     O1=0   O1=1    .      .     0 A1:0-1|A2:2-3"
	"C2-3:P1:S+  C3:P1  .      .     O3=0   O3=1    .      .     0 A1:2|A2:3 A1:P1|A2:P1"
	"C2-3:P1:S+  C3:P2  .      .     O3=0   O3=1    .      .     0 A1:2|A2:3 A1:P1|A2:P2"
	"C2-3:P1:S+  C3:P1  .      .     O2=0   O2=1    .      .     0 A1:2|A2:3 A1:P1|A2:P1"
	"C2-3:P1:S+  C3:P2  .      .     O2=0   O2=1    .      .     0 A1:2|A2:3 A1:P1|A2:P2"
	"C2-3:P1:S+  C3:P1  .      .     O2=0    .      .      .     0 A1:|A2:3 A1:P1|A2:P1"
	"C2-3:P1:S+  C3:P1  .      .     O3=0    .      .      .     0 A1:2|A2: A1:P1|A2:P1"
	"C2-3:P1:S+  C3:P1  .      .    T:O2=0   .      .      .     0 A1:3|A2:3 A1:P1|A2:P-1"
	"C2-3:P1:S+  C3:P1  .      .      .    T:O3=0   .      .     0 A1:2|A2:2 A1:P1|A2:P-1"
	"$SETUP_A123_PARTITIONS    .     O1=0    .      .      .     0 A1:|A2:2|A3:3 A1:P1|A2:P1|A3:P1"
	"$SETUP_A123_PARTITIONS    .     O2=0    .      .      .     0 A1:1|A2:|A3:3 A1:P1|A2:P1|A3:P1"
	"$SETUP_A123_PARTITIONS    .     O3=0    .      .      .     0 A1:1|A2:2|A3: A1:P1|A2:P1|A3:P1"
	"$SETUP_A123_PARTITIONS    .    T:O1=0   .      .      .     0 A1:2-3|A2:2-3|A3:3 A1:P1|A2:P-1|A3:P-1"
	"$SETUP_A123_PARTITIONS    .      .    T:O2=0   .      .     0 A1:1|A2:3|A3:3 A1:P1|A2:P1|A3:P-1"
	"$SETUP_A123_PARTITIONS    .      .      .    T:O3=0   .     0 A1:1|A2:2|A3:2 A1:P1|A2:P1|A3:P-1"
	"$SETUP_A123_PARTITIONS    .    T:O1=0  O1=1    .      .     0 A1:1|A2:2|A3:3 A1:P1|A2:P1|A3:P1"
	"$SETUP_A123_PARTITIONS    .      .    T:O2=0  O2=1    .     0 A1:1|A2:2|A3:3 A1:P1|A2:P1|A3:P1"
	"$SETUP_A123_PARTITIONS    .      .      .    T:O3=0  O3=1   0 A1:1|A2:2|A3:3 A1:P1|A2:P1|A3:P1"
	"$SETUP_A123_PARTITIONS    .    T:O1=0  O2=0   O1=1    .     0 A1:1|A2:|A3:3 A1:P1|A2:P1|A3:P1"
	"$SETUP_A123_PARTITIONS    .    T:O1=0  O2=0   O2=1    .     0 A1:2-3|A2:2-3|A3:3 A1:P1|A2:P-1|A3:P-1"

	#  old-A1 old-A2 old-A3 old-B1 new-A1 new-A2 new-A3 new-B1 fail ECPUs Pstate ISOLCPUS
	#  ------ ------ ------ ------ ------ ------ ------ ------ ---- ----- ------ --------
	#
	# Remote partition and cpuset.cpus.exclusive tests
	#
	" C0-3:S+ C1-3:S+ C2-3     .    X2-3     .      .      .     0 A1:0-3|A2:1-3|A3:2-3|XA1:2-3"
	" C0-3:S+ C1-3:S+ C2-3     .    X2-3  X2-3:P2   .      .     0 A1:0-1|A2:2-3|A3:2-3 A1:P0|A2:P2 2-3"
	" C0-3:S+ C1-3:S+ C2-3     .    X2-3   X3:P2    .      .     0 A1:0-2|A2:3|A3:3 A1:P0|A2:P2 3"
	" C0-3:S+ C1-3:S+ C2-3     .    X2-3   X2-3  X2-3:P2   .     0 A1:0-1|A2:1|A3:2-3 A1:P0|A3:P2 2-3"
	" C0-3:S+ C1-3:S+ C2-3     .    X2-3   X2-3 X2-3:P2:C3 .     0 A1:0-1|A2:1|A3:2-3 A1:P0|A3:P2 2-3"
	" C0-3:S+ C1-3:S+ C2-3   C2-3     .      .      .      P2    0 A1:0-3|A2:1-3|A3:2-3|B1:2-3 A1:P0|A3:P0|B1:P-2"
	" C0-3:S+ C1-3:S+ C2-3   C4-5     .      .      .      P2    0 B1:4-5 B1:P2 4-5"
	" C0-3:S+ C1-3:S+ C2-3    C4    X2-3   X2-3  X2-3:P2   P2    0 A3:2-3|B1:4 A3:P2|B1:P2 2-4"
	" C0-3:S+ C1-3:S+ C2-3    C4    X2-3   X2-3 X2-3:P2:C1-3 P2  0 A3:2-3|B1:4 A3:P2|B1:P2 2-4"
	" C0-3:S+ C1-3:S+ C2-3    C4    X1-3  X1-3:P2   P2     .     0 A2:1|A3:2-3 A2:P2|A3:P2 1-3"
	" C0-3:S+ C1-3:S+ C2-3    C4    X2-3   X2-3  X2-3:P2 P2:C4-5 0 A3:2-3|B1:4-5 A3:P2|B1:P2 2-5"
	" C4:X0-3:S+ X1-3:S+ X2-3  .      .      P2     .      .     0 A1:4|A2:1-3|A3:1-3 A2:P2 1-3"
	" C4:X0-3:S+ X1-3:S+ X2-3  .      .      .      P2     .     0 A1:4|A2:4|A3:2-3 A3:P2 2-3"

	# Nested remote/local partition tests
	" C0-3:S+ C1-3:S+ C2-3   C4-5   X2-3  X2-3:P1   P2     P1    0 A1:0-1|A2:|A3:2-3|B1:4-5 \
								       A1:P0|A2:P1|A3:P2|B1:P1 2-3"
	" C0-3:S+ C1-3:S+ C2-3    C4    X2-3  X2-3:P1   P2     P1    0 A1:0-1|A2:|A3:2-3|B1:4 \
								       A1:P0|A2:P1|A3:P2|B1:P1 2-4|2-3"
	" C0-3:S+ C1-3:S+ C2-3    C4    X2-3  X2-3:P1    .     P1    0 A1:0-1|A2:2-3|A3:2-3|B1:4 \
								       A1:P0|A2:P1|A3:P0|B1:P1"
	" C0-3:S+ C1-3:S+  C3     C4    X2-3  X2-3:P1   P2     P1    0 A1:0-1|A2:2|A3:3|B1:4 \
								       A1:P0|A2:P1|A3:P2|B1:P1 2-4|3"
	" C0-4:S+ C1-4:S+ C2-4     .    X2-4  X2-4:P2  X4:P1    .    0 A1:0-1|A2:2-3|A3:4 \
								       A1:P0|A2:P2|A3:P1 2-4|2-3"
	" C0-4:S+ C1-4:S+ C2-4     .    X2-4  X2-4:P2 X3-4:P1   .    0 A1:0-1|A2:2|A3:3-4 \
								       A1:P0|A2:P2|A3:P1 2"
	" C0-4:X2-4:S+ C1-4:X2-4:S+:P2 C2-4:X4:P1 \
				   .      .      X5      .      .    0 A1:0-4|A2:1-4|A3:2-4 \
								       A1:P0|A2:P-2|A3:P-1 ."
	" C0-4:X2-4:S+ C1-4:X2-4:S+:P2 C2-4:X4:P1 \
				   .      .      .      X1      .    0 A1:0-1|A2:2-4|A3:2-4 \
								       A1:P0|A2:P2|A3:P-1 2-4"

	# Remote partition offline tests
	" C0-3:S+ C1-3:S+ C2-3     .    X2-3   X2-3 X2-3:P2:O2=0 .   0 A1:0-1|A2:1|A3:3 A1:P0|A3:P2 2-3"
	" C0-3:S+ C1-3:S+ C2-3     .    X2-3   X2-3 X2-3:P2:O2=0 O2=1 0 A1:0-1|A2:1|A3:2-3 A1:P0|A3:P2 2-3"
	" C0-3:S+ C1-3:S+  C3      .    X2-3   X2-3    P2:O3=0   .   0 A1:0-2|A2:1-2|A3: A1:P0|A3:P2 3"
	" C0-3:S+ C1-3:S+  C3      .    X2-3   X2-3   T:P2:O3=0  .   0 A1:0-2|A2:1-2|A3:1-2 A1:P0|A3:P-2 3|"

	# An invalidated remote partition cannot self-recover from hotplug
	" C0-3:S+ C1-3:S+  C2      .    X2-3   X2-3   T:P2:O2=0 O2=1 0 A1:0-3|A2:1-3|A3:2 A1:P0|A3:P-2 ."

	# cpus.exclusive.effective clearing test
	" C0-3:S+ C1-3:S+  C2      .   X2-3:X    .      .      .     0 A1:0-3|A2:1-3|A3:2|XA1:"

	# Invalid to valid remote partition transition test
	" C0-3:S+   C1-3    .      .      .    X3:P2    .      .     0 A1:0-3|A2:1-3|XA2: A2:P-2 ."
	" C0-3:S+ C1-3:X3:P2
			    .      .    X2-3    P2      .      .     0 A1:0-2|A2:3|XA2:3 A2:P2 3"

	# Invalid to valid local partition direct transition tests
	" C1-3:S+:P2 X4:P2  .      .      .      .      .      .     0 A1:1-3|XA1:1-3|A2:1-3:XA2: A1:P2|A2:P-2 1-3"
	" C1-3:S+:P2 X4:P2  .      .      .    X3:P2    .      .     0 A1:1-2|XA1:1-3|A2:3:XA2:3 A1:P2|A2:P2 1-3"
	"  C0-3:P2   .      .    C4-6   C0-4     .      .      .     0 A1:0-4|B1:4-6 A1:P-2|B1:P0"
	"  C0-3:P2   .      .    C4-6 C0-4:C0-3  .      .      .     0 A1:0-3|B1:4-6 A1:P2|B1:P0 0-3"

	# Local partition invalidation tests
	" C0-3:X1-3:S+:P2 C1-3:X2-3:S+:P2 C2-3:X3:P2 \
				   .      .      .      .      .     0 A1:1|A2:2|A3:3 A1:P2|A2:P2|A3:P2 1-3"
	" C0-3:X1-3:S+:P2 C1-3:X2-3:S+:P2 C2-3:X3:P2 \
				   .      .     X4      .      .     0 A1:1-3|A2:1-3|A3:2-3|XA2:|XA3: A1:P2|A2:P-2|A3:P-2 1-3"
	" C0-3:X1-3:S+:P2 C1-3:X2-3:S+:P2 C2-3:X3:P2 \
				   .      .    C4:X     .      .     0 A1:1-3|A2:1-3|A3:2-3|XA2:|XA3: A1:P2|A2:P-2|A3:P-2 1-3"
	# Local partition CPU change tests
	" C0-5:S+:P2 C4-5:S+:P1 .  .      .    C3-5     .      .     0 A1:0-2|A2:3-5 A1:P2|A2:P1 0-2"
	" C0-5:S+:P2 C4-5:S+:P1 .  .    C1-5     .      .      .     0 A1:1-3|A2:4-5 A1:P2|A2:P1 1-3"

	# cpus_allowed/exclusive_cpus update tests
	" C0-3:X2-3:S+ C1-3:X2-3:S+ C2-3:X2-3 \
				   .    X:C4     .      P2     .     0 A1:4|A2:4|XA2:|XA3:|A3:4 \
								       A1:P0|A3:P-2 ."
	" C0-3:X2-3:S+ C1-3:X2-3:S+ C2-3:X2-3 \
				   .     X1      .      P2     .     0 A1:0-3|A2:1-3|XA1:1|XA2:|XA3:|A3:2-3 \
								       A1:P0|A3:P-2 ."
	" C0-3:X2-3:S+ C1-3:X2-3:S+ C2-3:X2-3 \
				   .      .     X3      P2     .     0 A1:0-2|A2:1-2|XA2:3|XA3:3|A3:3 \
								       A1:P0|A3:P2 3"
	" C0-3:X2-3:S+ C1-3:X2-3:S+ C2-3:X2-3:P2 \
				   .      .     X3      .      .     0 A1:0-2|A2:1-2|XA2:3|XA3:3|A3:3|XA3:3 \
								       A1:P0|A3:P2 3"
	" C0-3:X2-3:S+ C1-3:X2-3:S+ C2-3:X2-3:P2 \
				   .     X4      .      .      .     0 A1:0-3|A2:1-3|A3:2-3|XA1:4|XA2:|XA3 \
								       A1:P0|A3:P-2"

	#  old-A1 old-A2 old-A3 old-B1 new-A1 new-A2 new-A3 new-B1 fail ECPUs Pstate ISOLCPUS
	#  ------ ------ ------ ------ ------ ------ ------ ------ ---- ----- ------ --------
	#
	# Incorrect change to cpuset.cpus[.exclusive] invalidates partition root
	#
	# Adding CPUs to partition root that are not in parent's
	# cpuset.cpus is allowed, but those extra CPUs are ignored.
	"C2-3:P1:S+ C3:P1   .      .      .     C2-4    .      .     0 A1:|A2:2-3 A1:P1|A2:P1"

	# Taking away all CPUs from parent or itself if there are tasks
	# will make the partition invalid.
	"C2-3:P1:S+  C3:P1  .      .      T     C2-3    .      .     0 A1:2-3|A2:2-3 A1:P1|A2:P-1"
	" C3:P1:S+    C3    .      .      T      P1     .      .     0 A1:3|A2:3 A1:P1|A2:P-1"
	"$SETUP_A123_PARTITIONS    .    T:C2-3   .      .      .     0 A1:2-3|A2:2-3|A3:3 A1:P1|A2:P-1|A3:P-1"
	"$SETUP_A123_PARTITIONS    . T:C2-3:C1-3 .      .      .     0 A1:1|A2:2|A3:3 A1:P1|A2:P1|A3:P1"

	# Changing a partition root to member makes child partitions invalid
	"C2-3:P1:S+  C3:P1  .      .      P0     .      .      .     0 A1:2-3|A2:3 A1:P0|A2:P-1"
	"$SETUP_A123_PARTITIONS    .     C2-3    P0     .      .     0 A1:2-3|A2:2-3|A3:3 A1:P1|A2:P0|A3:P-1"

	# cpuset.cpus can contains cpus not in parent's cpuset.cpus as long
	# as they overlap.
	"C2-3:P1:S+  .      .      .      .   C3-4:P1   .      .     0 A1:2|A2:3 A1:P1|A2:P1"

	# Deletion of CPUs distributed to child cgroup is allowed.
	"C0-1:P1:S+ C1      .    C2-3   C4-5     .      .      .     0 A1:4-5|A2:4-5"

	# To become a valid partition root, cpuset.cpus must overlap parent's
	# cpuset.cpus.
	"  C0-1:P1   .      .    C2-3    S+   C4-5:P1   .      .     0 A1:0-1|A2:0-1 A1:P1|A2:P-1"

	# Enabling partition with child cpusets is allowed
	"  C0-1:S+  C1      .    C2-3    P1      .      .      .     0 A1:0-1|A2:1 A1:P1"

	# A partition root with non-partition root parent is invalid| but it
	# can be made valid if its parent becomes a partition root too.
	"  C0-1:S+  C1      .    C2-3     .      P2     .      .     0 A1:0-1|A2:1 A1:P0|A2:P-2"
	"  C0-1:S+ C1:P2    .    C2-3     P1     .      .      .     0 A1:0|A2:1 A1:P1|A2:P2 0-1|1"

	# A non-exclusive cpuset.cpus change will invalidate partition and its siblings
	"  C0-1:P1   .      .    C2-3   C0-2     .      .      .     0 A1:0-2|B1:2-3 A1:P-1|B1:P0"
	"  C0-1:P1   .      .  P1:C2-3  C0-2     .      .      .     0 A1:0-2|B1:2-3 A1:P-1|B1:P-1"
	"   C0-1     .      .  P1:C2-3  C0-2     .      .      .     0 A1:0-2|B1:2-3 A1:P0|B1:P-1"

	# cpuset.cpus can overlap with sibling cpuset.cpus.exclusive but not subsumed by it
	"   C0-3     .      .    C4-5     X5     .      .      .     0 A1:0-3|B1:4-5"

	# Child partition root that try to take all CPUs from parent partition
	# with tasks will remain invalid.
	" C1-4:P1:S+ P1     .      .       .     .      .      .     0 A1:1-4|A2:1-4 A1:P1|A2:P-1"
	" C1-4:P1:S+ P1     .      .       .   C1-4     .      .     0 A1|A2:1-4 A1:P1|A2:P1"
	" C1-4:P1:S+ P1     .      .       T   C1-4     .      .     0 A1:1-4|A2:1-4 A1:P1|A2:P-1"

	# Clearing of cpuset.cpus with a preset cpuset.cpus.exclusive shouldn't
	# affect cpuset.cpus.exclusive.effective.
	" C1-4:X3:S+ C1:X3  .      .       .     C      .      .     0 A2:1-4|XA2:3"

	# cpuset.cpus can contain CPUs that overlap a sibling cpuset with cpus.exclusive
	# but creating a local partition out of it is not allowed. Similarly and change
	# in cpuset.cpus of a local partition that overlaps sibling exclusive CPUs will
	# invalidate it.
	" CX1-4:S+ CX2-4:P2 .    C5-6      .     .      .      P1    0 A1:1|A2:2-4|B1:5-6|XB1:5-6 \
								       A1:P0|A2:P2:B1:P1 2-4"
	" CX1-4:S+ CX2-4:P2 .    C3-6      .     .      .      P1    0 A1:1|A2:2-4|B1:5-6 \
								       A1:P0|A2:P2:B1:P-1 2-4"
	" CX1-4:S+ CX2-4:P2 .    C5-6      .     .      .   P1:C3-6  0 A1:1|A2:2-4|B1:5-6 \
								       A1:P0|A2:P2:B1:P-1 2-4"

	#  old-A1 old-A2 old-A3 old-B1 new-A1 new-A2 new-A3 new-B1 fail ECPUs Pstate ISOLCPUS
	#  ------ ------ ------ ------ ------ ------ ------ ------ ---- ----- ------ --------
	# Failure cases:

	# A task cannot be added to a partition with no cpu
	"C2-3:P1:S+  C3:P1  .      .    O2=0:T   .      .      .     1 A1:|A2:3 A1:P1|A2:P1"

	# Changes to cpuset.cpus.exclusive that violate exclusivity rule is rejected
	"   C0-3     .      .    C4-5   X0-3     .      .     X3-5   1 A1:0-3|B1:4-5"

	# cpuset.cpus cannot be a subset of sibling cpuset.cpus.exclusive
	"   C0-3     .      .    C4-5   X3-5     .      .      .     1 A1:0-3|B1:4-5"
)

#
# Cpuset controller remote partition test matrix.
#
# Cgroup test hierarchy
#
#	      root
#	        |
#	      rtest (cpuset.cpus.exclusive=1-7)
#	        |
#	 +------+------+
#	 |             |
#	 p1            p2
#     +--+--+       +--+--+
#     |     |       |     |
#    c11   c12     c21   c22
#
# REMOTE_TEST_MATRIX uses the same notational convention as TEST_MATRIX.
# Only CPUs 1-7 should be used.
#
REMOTE_TEST_MATRIX=(
	#  old-p1 old-p2 old-c11 old-c12 old-c21 old-c22
	#  new-p1 new-p2 new-c11 new-c12 new-c21 new-c22 ECPUs Pstate ISOLCPUS
	#  ------ ------ ------- ------- ------- ------- ----- ------ --------
	" X1-3:S+ X4-6:S+ X1-2     X3     X4-5     X6 \
	      .      .     P2      P2      P2      P2    c11:1-2|c12:3|c21:4-5|c22:6 \
							 c11:P2|c12:P2|c21:P2|c22:P2 1-6"
	" CX1-4:S+   .   X1-2:P2   C3      .       .  \
	      .      .     .      C3-4     .       .     p1:3-4|c11:1-2|c12:3-4 \
							 p1:P0|c11:P2|c12:P0 1-2"
	" CX1-4:S+   .   X1-2:P2   .       .       .  \
	    X2-4     .     .       .       .       .     p1:1,3-4|c11:2 \
							 p1:P0|c11:P2 2"
	" CX1-5:S+   .   X1-2:P2 X3-5:P1   .       .  \
	    X2-4     .     .       .       .       .     p1:1,5|c11:2|c12:3-4 \
							 p1:P0|c11:P2|c12:P1 2"
	" CX1-4:S+   .   X1-2:P2 X3-4:P1   .       .  \
	      .      .     X2      .       .       .     p1:1|c11:2|c12:3-4 \
							 p1:P0|c11:P2|c12:P1 2"
	# p1 as member, will get its effective CPUs from its parent rtest
	" CX1-4:S+   .   X1-2:P2 X3-4:P1   .       .  \
	      .      .     X1     CX2-4    .       .     p1:5-7|c11:1|c12:2-4 \
							 p1:P0|c11:P2|c12:P1 1"
	" CX1-4:S+ X5-6:P1:S+ .    .       .       .  \
	      .      .   X1-2:P2  X4-5:P1  .     X1-7:P2 p1:3|c11:1-2|c12:4:c22:5-6 \
							 p1:P0|p2:P1|c11:P2|c12:P1|c22:P2 \
							 1-2,4-6|1-2,5-6"
)

#
# Write to the cpu online file
#  $1 - <c>=<v> where <c> = cpu number, <v> value to be written
#
write_cpu_online()
{
	CPU=${1%=*}
	VAL=${1#*=}
	CPUFILE=//sys/devices/system/cpu/cpu${CPU}/online
	if [[ $VAL -eq 0 ]]
	then
		OFFLINE_CPUS="$OFFLINE_CPUS $CPU"
	else
		[[ -n "$OFFLINE_CPUS" ]] && {
			OFFLINE_CPUS=$(echo $CPU $CPU $OFFLINE_CPUS | fmt -1 |\
					sort | uniq -u)
		}
	fi
	echo $VAL > $CPUFILE
	pause 0.05
}

#
# Set controller state
#  $1 - cgroup directory
#  $2 - state
#  $3 - showerr
#
# The presence of ":" in state means transition from one to the next.
#
set_ctrl_state()
{
	TMPMSG=/tmp/.msg_$$
	CGRP=$1
	STATE=$2
	SHOWERR=${3}
	CTRL=${CTRL:=$CONTROLLER}
	HASERR=0
	REDIRECT="2> $TMPMSG"
	[[ -z "$STATE" || "$STATE" = '.' ]] && return 0
	[[ $VERBOSE -gt 0 ]] && SHOWERR=1

	rm -f $TMPMSG
	for CMD in $(echo $STATE | sed -e "s/:/ /g")
	do
		TFILE=$CGRP/cgroup.procs
		SFILE=$CGRP/cgroup.subtree_control
		PFILE=$CGRP/cpuset.cpus.partition
		CFILE=$CGRP/cpuset.cpus
		XFILE=$CGRP/cpuset.cpus.exclusive
		case $CMD in
		    S*) PREFIX=${CMD#?}
			COMM="echo ${PREFIX}${CTRL} > $SFILE"
			eval $COMM $REDIRECT
			;;
		    X*)
			CPUS=${CMD#?}
			COMM="echo $CPUS > $XFILE"
			eval $COMM $REDIRECT
			;;
		    CX*)
			CPUS=${CMD#??}
			COMM="echo $CPUS > $CFILE; echo $CPUS > $XFILE"
			eval $COMM $REDIRECT
			;;
		    C*) CPUS=${CMD#?}
			COMM="echo $CPUS > $CFILE"
			eval $COMM $REDIRECT
			;;
		    P*) VAL=${CMD#?}
			case $VAL in
			0)  VAL=member
			    ;;
			1)  VAL=root
			    ;;
			2)  VAL=isolated
			    ;;
			*)
			    echo "Invalid partition state - $VAL"
			    exit 1
			    ;;
			esac
			COMM="echo $VAL > $PFILE"
			eval $COMM $REDIRECT
			;;
		    O*) VAL=${CMD#?}
			write_cpu_online $VAL
			;;
		    T*) COMM="echo 0 > $TFILE"
			eval $COMM $REDIRECT
			;;
		    *)  echo "Unknown command: $CMD"
		        exit 1
			;;
		esac
		RET=$?
		[[ $RET -ne 0 ]] && {
			[[ -n "$SHOWERR" ]] && {
				echo "$COMM"
				cat $TMPMSG
			}
			HASERR=1
		}
		pause 0.01
		rm -f $TMPMSG
	done
	return $HASERR
}

set_ctrl_state_noerr()
{
	CGRP=$1
	STATE=$2
	[[ -d $CGRP ]] || mkdir $CGRP
	set_ctrl_state $CGRP $STATE 1
	[[ $? -ne 0 ]] && {
		echo "ERROR: Failed to set $2 to cgroup $1!"
		exit 1
	}
}

online_cpus()
{
	[[ -n "OFFLINE_CPUS" ]] && {
		for C in $OFFLINE_CPUS
		do
			write_cpu_online ${C}=1
		done
	}
}

#
# Remove all the test cgroup directories
#
reset_cgroup_states()
{
	echo 0 > $CGROUP2/cgroup.procs
	online_cpus
	rmdir $RESET_LIST > /dev/null 2>&1
}

dump_states()
{
	for DIR in $CGROUP_LIST
	do
		CPUS=$DIR/cpuset.cpus
		ECPUS=$DIR/cpuset.cpus.effective
		XCPUS=$DIR/cpuset.cpus.exclusive
		XECPUS=$DIR/cpuset.cpus.exclusive.effective
		PRS=$DIR/cpuset.cpus.partition
		PCPUS=$DIR/.__DEBUG__.cpuset.cpus.subpartitions
		ISCPUS=$DIR/cpuset.cpus.isolated
		[[ -e $CPUS   ]] && echo "$CPUS: $(cat $CPUS)"
		[[ -e $XCPUS  ]] && echo "$XCPUS: $(cat $XCPUS)"
		[[ -e $ECPUS  ]] && echo "$ECPUS: $(cat $ECPUS)"
		[[ -e $XECPUS ]] && echo "$XECPUS: $(cat $XECPUS)"
		[[ -e $PRS    ]] && echo "$PRS: $(cat $PRS)"
		[[ -e $PCPUS  ]] && echo "$PCPUS: $(cat $PCPUS)"
		[[ -e $ISCPUS ]] && echo "$ISCPUS: $(cat $ISCPUS)"
	done
}

#
# Set the actual cgroup directory into $CGRP_DIR
# $1 - cgroup name
#
set_cgroup_dir()
{
	CGRP_DIR=$1
	[[ $CGRP_DIR = A2  ]] && CGRP_DIR=A1/A2
	[[ $CGRP_DIR = A3  ]] && CGRP_DIR=A1/A2/A3
	[[ $CGRP_DIR = c11 ]] && CGRP_DIR=p1/c11
	[[ $CGRP_DIR = c12 ]] && CGRP_DIR=p1/c12
	[[ $CGRP_DIR = c21 ]] && CGRP_DIR=p2/c21
	[[ $CGRP_DIR = c22 ]] && CGRP_DIR=p2/c22
}

#
# Check effective cpus
# $1 - check string, format: <cgroup>:<cpu-list>[|<cgroup>:<cpu-list>]*
#
check_effective_cpus()
{
	CHK_STR=$1
	for CHK in $(echo $CHK_STR | sed -e "s/|/ /g")
	do
		set -- $(echo $CHK | sed -e "s/:/ /g")
		CGRP=$1
		EXPECTED_CPUS=$2
		ACTUAL_CPUS=
		if [[ $CGRP = X* ]]
		then
			CGRP=${CGRP#X}
			FILE=cpuset.cpus.exclusive.effective
		else
			FILE=cpuset.cpus.effective
		fi
		set_cgroup_dir $CGRP
		[[ -e $CGRP_DIR/$FILE ]] || return 1
		ACTUAL_CPUS=$(cat $CGRP_DIR/$FILE)
		[[ $EXPECTED_CPUS = $ACTUAL_CPUS ]] || return 1
	done
}

#
# Check cgroup states
#  $1 - check string, format: <cgroup>:<state>[|<cgroup>:<state>]*
#
check_cgroup_states()
{
	CHK_STR=$1
	for CHK in $(echo $CHK_STR | sed -e "s/|/ /g")
	do
		set -- $(echo $CHK | sed -e "s/:/ /g")
		CGRP=$1
		EXPECTED_STATE=$2
		FILE=
		EVAL=$(expr substr $EXPECTED_STATE 2 2)

		set_cgroup_dir $CGRP
		case $EXPECTED_STATE in
			P*) FILE=$CGRP_DIR/cpuset.cpus.partition
			    ;;
			*)  echo "Unknown state: $EXPECTED_STATE!"
			    exit 1
			    ;;
		esac
		ACTUAL_STATE=$(cat $FILE)

		case "$ACTUAL_STATE" in
			member) VAL=0
				;;
			root)	VAL=1
				;;
			isolated)
				VAL=2
				;;
			"root invalid"*)
				VAL=-1
				;;
			"isolated invalid"*)
				VAL=-2
				;;
		esac
		[[ $EVAL != $VAL ]] && return 1

		#
		# For root partition, dump sched-domains info to console if
		# verbose mode set for manual comparison with sched debug info.
		#
		[[ $VAL -eq 1 && $VERBOSE -gt 0 ]] && {
			DOMS=$(cat $CGRP_DIR/cpuset.cpus.effective)
			[[ -n "$DOMS" ]] &&
				echo " [$CGRP_DIR] sched-domain: $DOMS" > $CONSOLE
		}
	done
	return 0
}

#
# Get isolated (including offline) CPUs by looking at
# /sys/kernel/debug/sched/domains and cpuset.cpus.isolated control file,
# if available, and compare that with the expected value.
#
# Note that isolated CPUs from the sched/domains context include offline
# CPUs as well as CPUs in non-isolated 1-CPU partition. Those CPUs may
# not be included in the cpuset.cpus.isolated control file which contains
# only CPUs in isolated partitions as well as those that are isolated at
# boot time.
#
# $1 - expected isolated cpu list(s) <isolcpus1>{,<isolcpus2>}
# <isolcpus1> - expected sched/domains value
# <isolcpus2> - cpuset.cpus.isolated value = <isolcpus1> if not defined
#
check_isolcpus()
{
	EXPECTED_ISOLCPUS=$1
	ISCPUS=${CGROUP2}/cpuset.cpus.isolated
	ISOLCPUS=$(cat $ISCPUS)
	LASTISOLCPU=
	SCHED_DOMAINS=/sys/kernel/debug/sched/domains
	if [[ $EXPECTED_ISOLCPUS = . ]]
	then
		EXPECTED_ISOLCPUS=
		EXPECTED_SDOMAIN=
	elif [[ $(expr $EXPECTED_ISOLCPUS : ".*|.*") > 0 ]]
	then
		set -- $(echo $EXPECTED_ISOLCPUS | sed -e "s/|/ /g")
		EXPECTED_ISOLCPUS=$2
		EXPECTED_SDOMAIN=$1
	else
		EXPECTED_SDOMAIN=$EXPECTED_ISOLCPUS
	fi

	#
	# Appending pre-isolated CPUs
	# Even though CPU #8 isn't used for testing, it can't be pre-isolated
	# to make appending those CPUs easier.
	#
	[[ -n "$BOOT_ISOLCPUS" ]] && {
		EXPECTED_ISOLCPUS=${EXPECTED_ISOLCPUS:+${EXPECTED_ISOLCPUS},}${BOOT_ISOLCPUS}
		EXPECTED_SDOMAIN=${EXPECTED_SDOMAIN:+${EXPECTED_SDOMAIN},}${BOOT_ISOLCPUS}
	}

	#
	# Check cpuset.cpus.isolated cpumask
	#
	[[ "$EXPECTED_ISOLCPUS" != "$ISOLCPUS" ]] && {
		# Take a 50ms pause and try again
		pause 0.05
		ISOLCPUS=$(cat $ISCPUS)
	}
	[[ "$EXPECTED_ISOLCPUS" != "$ISOLCPUS" ]] && return 1
	ISOLCPUS=
	EXPECTED_ISOLCPUS=$EXPECTED_SDOMAIN

	#
	# Use the sched domain in debugfs to check isolated CPUs, if available
	#
	[[ -d $SCHED_DOMAINS ]] || return 0

	for ((CPU=0; CPU < $NR_CPUS; CPU++))
	do
		[[ -n "$(ls ${SCHED_DOMAINS}/cpu$CPU)" ]] && continue

		if [[ -z "$LASTISOLCPU" ]]
		then
			ISOLCPUS=$CPU
			LASTISOLCPU=$CPU
		elif [[ "$LASTISOLCPU" -eq $((CPU - 1)) ]]
		then
			echo $ISOLCPUS | grep -q "\<$LASTISOLCPU\$"
			if [[ $? -eq 0 ]]
			then
				ISOLCPUS=${ISOLCPUS}-
			fi
			LASTISOLCPU=$CPU
		else
			if [[ $ISOLCPUS = *- ]]
			then
				ISOLCPUS=${ISOLCPUS}$LASTISOLCPU
			fi
			ISOLCPUS=${ISOLCPUS},$CPU
			LASTISOLCPU=$CPU
		fi
	done
	[[ "$ISOLCPUS" = *- ]] && ISOLCPUS=${ISOLCPUS}$LASTISOLCPU

	[[ "$EXPECTED_SDOMAIN" = "$ISOLCPUS" ]]
}

test_fail()
{
	TESTNUM=$1
	TESTTYPE=$2
	ADDINFO=$3
	echo "Test $TEST[$TESTNUM] failed $TESTTYPE check!"
	[[ -n "$ADDINFO" ]] && echo "*** $ADDINFO ***"
	eval echo \${$TEST[$I]}
	echo
	dump_states
	exit 1
}

#
# Check to see if there are unexpected isolated CPUs left beyond the boot
# time isolated ones.
#
null_isolcpus_check()
{
	[[ $VERBOSE -gt 0 ]] || return 0
	# Retry a few times before printing error
	RETRY=0
	while [[ $RETRY -lt 8 ]]
	do
		pause 0.02
		check_isolcpus "."
		[[ $? -eq 0 ]] && return 0
		((RETRY++))
	done
	echo "Unexpected isolated CPUs: $ISOLCPUS"
	dump_states
	exit 1
}

#
# Check state transition test result
#  $1 - Test number
#  $2 - Expected effective CPU values
#  $3 - Expected partition states
#  $4 - Expected isolated CPUs
#
check_test_results()
{
	_NR=$1
	_ECPUS="$2"
	_PSTATES="$3"
	_ISOLCPUS="$4"

	[[ -n "$_ECPUS" && "$_ECPUS" != . ]] && {
		check_effective_cpus $_ECPUS
		[[ $? -ne 0 ]] && test_fail $_NR "effective CPU" \
			 "Cgroup $CGRP: expected $EXPECTED_CPUS, got $ACTUAL_CPUS"
	}

	[[ -n "$_PSTATES" && "$_PSTATES" != . ]] && {
		check_cgroup_states $_PSTATES
		[[ $? -ne 0 ]] && test_fail $_NR states \
			"Cgroup $CGRP: expected $EXPECTED_STATE, got $ACTUAL_STATE"
	}

	# Compare the expected isolated CPUs with the actual ones,
	# if available
	[[ -n "$_ISOLCPUS" ]] && {
		check_isolcpus $_ISOLCPUS
		[[ $? -ne 0 ]] && {
			[[ -n "$BOOT_ISOLCPUS" ]] && _ISOLCPUS=${_ISOLCPUS},${BOOT_ISOLCPUS}
			test_fail $_NR "isolated CPU" \
				"Expect $_ISOLCPUS, get $ISOLCPUS instead"
		}
	}
	reset_cgroup_states
	#
	# Check to see if effective cpu list changes
	#
	_NEWLIST=$(cat $CGROUP2/cpuset.cpus.effective)
	RETRY=0
	while [[ $_NEWLIST != $CPULIST && $RETRY -lt 8 ]]
	do
		# Wait a bit longer & recheck a few times
		pause 0.02
		((RETRY++))
		_NEWLIST=$(cat $CGROUP2/cpuset.cpus.effective)
	done
	[[ $_NEWLIST != $CPULIST ]] && {
		echo "Effective cpus changed to $_NEWLIST after test $_NR!"
		exit 1
	}
	null_isolcpus_check
	[[ $VERBOSE -gt 0 ]] && echo "Test $I done."
}

#
# Run cpuset state transition test
#  $1 - test matrix name
#
# This test is somewhat fragile as delays (sleep x) are added in various
# places to make sure state changes are fully propagated before the next
# action. These delays may need to be adjusted if running in a slower machine.
#
run_state_test()
{
	TEST=$1
	CONTROLLER=cpuset
	CGROUP_LIST=". A1 A1/A2 A1/A2/A3 B1"
	RESET_LIST="A1/A2/A3 A1/A2 A1 B1"
	I=0
	eval CNT="\${#$TEST[@]}"

	reset_cgroup_states
	console_msg "Running state transition test ..."

	while [[ $I -lt $CNT ]]
	do
		echo "Running test $I ..." > $CONSOLE
		[[ $VERBOSE -gt 1 ]] && {
			echo ""
			eval echo \${$TEST[$I]}
		}
		eval set -- "\${$TEST[$I]}"
		OLD_A1=$1
		OLD_A2=$2
		OLD_A3=$3
		OLD_B1=$4
		NEW_A1=$5
		NEW_A2=$6
		NEW_A3=$7
		NEW_B1=$8
		RESULT=$9
		ECPUS=${10}
		STATES=${11}
		ICPUS=${12}

		set_ctrl_state_noerr A1       $OLD_A1
		set_ctrl_state_noerr A1/A2    $OLD_A2
		set_ctrl_state_noerr A1/A2/A3 $OLD_A3
		set_ctrl_state_noerr B1       $OLD_B1

		RETVAL=0
		set_ctrl_state A1       $NEW_A1; ((RETVAL += $?))
		set_ctrl_state A1/A2    $NEW_A2; ((RETVAL += $?))
		set_ctrl_state A1/A2/A3 $NEW_A3; ((RETVAL += $?))
		set_ctrl_state B1       $NEW_B1; ((RETVAL += $?))

		[[ $RETVAL -ne $RESULT ]] && test_fail $I result

		check_test_results $I "$ECPUS" "$STATES" "$ICPUS"
		((I++))
	done
	echo "All $I tests of $TEST PASSED."
}

#
# Run cpuset remote partition state transition test
#  $1 - test matrix name
#
run_remote_state_test()
{
	TEST=$1
	CONTROLLER=cpuset
	[[ -d rtest ]] || mkdir rtest
	cd rtest
	echo +cpuset > cgroup.subtree_control
	echo "1-7" > cpuset.cpus
	echo "1-7" > cpuset.cpus.exclusive
	CGROUP_LIST=".. . p1 p2 p1/c11 p1/c12 p2/c21 p2/c22"
	RESET_LIST="p1/c11 p1/c12 p2/c21 p2/c22 p1 p2"
	I=0
	eval CNT="\${#$TEST[@]}"

	reset_cgroup_states
	console_msg "Running remote partition state transition test ..."

	while [[ $I -lt $CNT ]]
	do
		echo "Running test $I ..." > $CONSOLE
		[[ $VERBOSE -gt 1 ]] && {
			echo ""
			eval echo \${$TEST[$I]}
		}
		eval set -- "\${$TEST[$I]}"
		OLD_p1=$1
		OLD_p2=$2
		OLD_c11=$3
		OLD_c12=$4
		OLD_c21=$5
		OLD_c22=$6
		NEW_p1=$7
		NEW_p2=$8
		NEW_c11=$9
		NEW_c12=${10}
		NEW_c21=${11}
		NEW_c22=${12}
		ECPUS=${13}
		STATES=${14}
		ICPUS=${15}

		set_ctrl_state_noerr p1     $OLD_p1
		set_ctrl_state_noerr p2     $OLD_p2
		set_ctrl_state_noerr p1/c11 $OLD_c11
		set_ctrl_state_noerr p1/c12 $OLD_c12
		set_ctrl_state_noerr p2/c21 $OLD_c21
		set_ctrl_state_noerr p2/c22 $OLD_c22

		RETVAL=0
		set_ctrl_state p1     $NEW_p1 ; ((RETVAL += $?))
		set_ctrl_state p2     $NEW_p2 ; ((RETVAL += $?))
		set_ctrl_state p1/c11 $NEW_c11; ((RETVAL += $?))
		set_ctrl_state p1/c12 $NEW_c12; ((RETVAL += $?))
		set_ctrl_state p2/c21 $NEW_c21; ((RETVAL += $?))
		set_ctrl_state p2/c22 $NEW_c22; ((RETVAL += $?))

		[[ $RETVAL -ne 0 ]] && test_fail $I result

		check_test_results $I "$ECPUS" "$STATES" "$ICPUS"
		((I++))
	done
	cd ..
	rmdir rtest
	echo "All $I tests of $TEST PASSED."
}

#
# Testing the new "isolated" partition root type
#
test_isolated()
{
	cd $CGROUP2/test
	echo 2-3 > cpuset.cpus
	TYPE=$(cat cpuset.cpus.partition)
	[[ $TYPE = member ]] || echo member > cpuset.cpus.partition

	console_msg "Change from member to root"
	test_partition root

	console_msg "Change from root to isolated"
	test_partition isolated

	console_msg "Change from isolated to member"
	test_partition member

	console_msg "Change from member to isolated"
	test_partition isolated

	console_msg "Change from isolated to root"
	test_partition root

	console_msg "Change from root to member"
	test_partition member

	#
	# Testing partition root with no cpu
	#
	console_msg "Distribute all cpus to child partition"
	echo +cpuset > cgroup.subtree_control
	test_partition root

	mkdir A1
	cd A1
	echo 2-3 > cpuset.cpus
	test_partition root
	test_effective_cpus 2-3
	cd ..
	test_effective_cpus ""

	console_msg "Moving task to partition test"
	test_add_proc "No space left"
	cd A1
	test_add_proc ""
	cd ..

	console_msg "Shrink and expand child partition"
	cd A1
	echo 2 > cpuset.cpus
	cd ..
	test_effective_cpus 3
	cd A1
	echo 2-3 > cpuset.cpus
	cd ..
	test_effective_cpus ""

	# Cleaning up
	console_msg "Cleaning up"
	echo $$ > $CGROUP2/cgroup.procs
	[[ -d A1 ]] && rmdir A1
	null_isolcpus_check
	pause 0.05
}

#
# Wait for inotify event for the given file and read it
# $1: cgroup file to wait for
# $2: file to store the read result
#
wait_inotify()
{
	CGROUP_FILE=$1
	OUTPUT_FILE=$2

	$WAIT_INOTIFY $CGROUP_FILE
	cat $CGROUP_FILE > $OUTPUT_FILE
}

#
# Test if inotify events are properly generated when going into and out of
# invalid partition state.
#
test_inotify()
{
	ERR=0
	PRS=/tmp/.prs_$$
	cd $CGROUP2/test
	[[ -f $WAIT_INOTIFY ]] || {
		echo "wait_inotify not found, inotify test SKIPPED."
		return
	}

	pause 0.01
	echo 1 > cpuset.cpus
	echo 0 > cgroup.procs
	echo root > cpuset.cpus.partition
	pause 0.01
	rm -f $PRS
	wait_inotify $PWD/cpuset.cpus.partition $PRS &
	pause 0.01
	set_ctrl_state . "O1=0"
	pause 0.01
	check_cgroup_states ".:P-1"
	if [[ $? -ne 0 ]]
	then
		echo "FAILED: Inotify test - partition not invalid"
		ERR=1
	elif [[ ! -f $PRS ]]
	then
		echo "FAILED: Inotify test - event not generated"
		ERR=1
		kill %1
	elif [[ $(cat $PRS) != "root invalid"* ]]
	then
		echo "FAILED: Inotify test - incorrect state"
		cat $PRS
		ERR=1
	fi
	online_cpus
	echo member > cpuset.cpus.partition
	echo 0 > ../cgroup.procs
	if [[ $ERR -ne 0 ]]
	then
		exit 1
	else
		echo "Inotify test PASSED"
	fi
	echo member > cpuset.cpus.partition
	echo "" > cpuset.cpus
}

trap cleanup 0 2 3 6
run_state_test TEST_MATRIX
run_remote_state_test REMOTE_TEST_MATRIX
test_isolated
test_inotify
echo "All tests PASSED."
