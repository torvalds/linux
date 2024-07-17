#!/bin/sh -eu
# SPDX-License-Identifier: GPL-2.0
#
# Helper script for the Linux Kernel GPIO sloppy logic analyzer
#
# Copyright (C) Wolfram Sang <wsa@sang-engineering.com>
# Copyright (C) Renesas Electronics Corporation

samplefreq=1000000
numsamples=250000
cpusetdefaultdir='/sys/fs/cgroup'
cpusetprefix='cpuset.'
debugdir='/sys/kernel/debug'
ladirname='gpio-sloppy-logic-analyzer'
outputdir="$PWD"
neededcmds='taskset zip'
max_chans=8
duration=
initcpu=
listinstances=0
lainstance=
lasysfsdir=
triggerdat=
trigger_bindat=
progname="${0##*/}"
print_help()
{
	cat << EOF
$progname - helper script for the Linux Kernel Sloppy GPIO Logic Analyzer
Available options:
	-c|--cpu <n>: which CPU to isolate for sampling. Only needed once. Default <1>.
		      Remember that a more powerful CPU gives you higher sampling speeds.
		      Also CPU0 is not recommended as it usually does extra bookkeeping.
	-d|--duration-us <SI-n>: number of microseconds to sample. Overrides -n, no default value.
	-h|--help: print this help
	-i|--instance <str>: name of the logic analyzer in case you have multiple instances. Default
			     to first instance found
	-k|--kernel-debug-dir <str>: path to the kernel debugfs mountpoint. Default: <$debugdir>
	-l|--list-instances: list all available instances
	-n|--num_samples <SI-n>: number of samples to acquire. Default <$numsamples>
	-o|--output-dir <str>: directory to put the result files. Default: current dir
	-s|--sample_freq <SI-n>: desired sampling frequency. Might be capped if too large.
				 Default: <1000000>
	-t|--trigger <str>: pattern to use as trigger. <str> consists of two-char pairs. First
			    char is channel number starting at "1". Second char is trigger level:
			    "L" - low; "H" - high; "R" - rising; "F" - falling
			    These pairs can be combined with "+", so "1H+2F" triggers when probe 1
			    is high while probe 2 has a falling edge. You can have multiple triggers
			    combined with ",". So, "1H+2F,1H+2R" is like the example before but it
			    waits for a rising edge on probe 2 while probe 1 is still high after the
			    first trigger has been met.
			    Trigger data will only be used for the next capture and then be erased.

<SI-n> is an integer value where SI units "T", "G", "M", "K" are recognized, e.g. '1M500K' is 1500000.

Examples:
Samples $numsamples values at 1MHz with an already prepared CPU or automatically prepares CPU1 if needed,
use the first logic analyzer instance found:
	'$progname'
Samples 50us at 2MHz waiting for a falling edge on channel 2. CPU and instance as above:
	'$progname -d 50 -s 2M -t "2F"'

Note that the process exits after checking all parameters but a sub-process still works in
the background. The result is only available once the sub-process finishes.

Result is a .sr file to be consumed with PulseView from the free Sigrok project. It is
a zip file which also contains the binary sample data which may be consumed by others.
The filename is the logic analyzer instance name plus a since-epoch timestamp.
EOF
}

fail()
{
	echo "$1"
	exit 1
}

parse_si()
{
	conv_si="$(printf $1 | sed 's/[tT]+\?/*1000G+/g; s/[gG]+\?/*1000M+/g; s/[mM]+\?/*1000K+/g; s/[kK]+\?/*1000+/g; s/+$//')"
	si_val="$((conv_si))"
}
set_newmask()
{
	for f in $(find "$1" -iname "$2"); do echo "$newmask" > "$f" 2>/dev/null || true; done
}

init_cpu()
{
	isol_cpu="$1"

	[ -d "$lacpusetdir" ] || mkdir "$lacpusetdir"

	cur_cpu=$(cat "${lacpusetfile}cpus")
	[ "$cur_cpu" = "$isol_cpu" ] && return
	[ -z "$cur_cpu" ] || fail "CPU$isol_cpu requested but CPU$cur_cpu already isolated"

	echo "$isol_cpu" > "${lacpusetfile}cpus" || fail "Could not isolate CPU$isol_cpu. Does it exist?"
	echo 1 > "${lacpusetfile}cpu_exclusive"
	echo 0 > "${lacpusetfile}mems"

	oldmask=$(cat /proc/irq/default_smp_affinity)
	newmask=$(printf "%x" $((0x$oldmask & ~(1 << isol_cpu))))

	set_newmask '/proc/irq' '*smp_affinity'
	set_newmask '/sys/devices/virtual/workqueue/' 'cpumask'

	# Move tasks away from isolated CPU
	for p in $(ps -o pid | tail -n +2); do
		mask=$(taskset -p "$p") || continue
		# Ignore tasks with a custom mask, i.e. not equal $oldmask
		[ "${mask##*: }" = "$oldmask" ] || continue
		taskset -p "$newmask" "$p" || continue
	done 2>/dev/null >/dev/null

	# Big hammer! Working with 'rcu_momentary_dyntick_idle()' for a more fine-grained solution
	# still printed warnings. Same for re-enabling the stall detector after sampling.
	echo 1 > /sys/module/rcupdate/parameters/rcu_cpu_stall_suppress

	cpufreqgov="/sys/devices/system/cpu/cpu$isol_cpu/cpufreq/scaling_governor"
	[ -w "$cpufreqgov" ] && echo 'performance' > "$cpufreqgov" || true
}

parse_triggerdat()
{
	oldifs="$IFS"
	IFS=','; for trig in $1; do
		mask=0; val1=0; val2=0
		IFS='+'; for elem in $trig; do
			chan=${elem%[lhfrLHFR]}
			mode=${elem#$chan}
			# Check if we could parse something and the channel number fits
			[ "$chan" != "$elem" ] && [ "$chan" -le $max_chans ] || fail "Trigger syntax error: $elem"
			bit=$((1 << (chan - 1)))
			mask=$((mask | bit))
			case $mode in
				[hH]) val1=$((val1 | bit)); val2=$((val2 | bit));;
				[fF]) val1=$((val1 | bit));;
				[rR]) val2=$((val2 | bit));;
			esac
		done
		trigger_bindat="$trigger_bindat$(printf '\\%o\\%o' $mask $val1)"
		[ $val1 -ne $val2 ] && trigger_bindat="$trigger_bindat$(printf '\\%o\\%o' $mask $val2)"
	done
	IFS="$oldifs"
}

do_capture()
{
	taskset "$1" echo 1 > "$lasysfsdir"/capture || fail "Capture error! Check kernel log"

	srtmp=$(mktemp -d)
	echo 1 > "$srtmp"/version
	cp "$lasysfsdir"/sample_data "$srtmp"/logic-1-1
	cat > "$srtmp"/metadata << EOF
[global]
sigrok version=0.2.0

[device 1]
capturefile=logic-1
total probes=$(wc -l < "$lasysfsdir"/meta_data)
samplerate=${samplefreq}Hz
unitsize=1
EOF
	cat "$lasysfsdir"/meta_data >> "$srtmp"/metadata

	zipname="$outputdir/${lasysfsdir##*/}-$(date +%s).sr"
	zip -jq "$zipname" "$srtmp"/*
	rm -rf "$srtmp"
	delay_ack=$(cat "$lasysfsdir"/delay_ns_acquisition)
	[ "$delay_ack" -eq 0 ] && delay_ack=1
	echo "Logic analyzer done. Saved '$zipname'"
	echo "Max sample frequency this time: $((1000000000 / delay_ack))Hz."
}

rep=$(getopt -a -l cpu:,duration-us:,help,instance:,list-instances,kernel-debug-dir:,num_samples:,output-dir:,sample_freq:,trigger: -o c:d:hi:k:ln:o:s:t: -- "$@") || exit 1
eval set -- "$rep"
while true; do
	case "$1" in
	-c|--cpu) initcpu="$2"; shift;;
	-d|--duration-us) parse_si $2; duration=$si_val; shift;;
	-h|--help) print_help; exit 0;;
	-i|--instance) lainstance="$2"; shift;;
	-k|--kernel-debug-dir) debugdir="$2"; shift;;
	-l|--list-instances) listinstances=1;;
	-n|--num_samples) parse_si $2; numsamples=$si_val; shift;;
	-o|--output-dir) outputdir="$2"; shift;;
	-s|--sample_freq) parse_si $2; samplefreq=$si_val; shift;;
	-t|--trigger) triggerdat="$2"; shift;;
	--) break;;
	*) fail "error parsing command line: $*";;
	esac
	shift
done

for f in $neededcmds; do
	command -v "$f" >/dev/null || fail "Command '$f' not found"
done

# print cpuset mountpoint if any, errorcode > 0 if noprefix option was found
cpusetdir=$(awk '$3 == "cgroup" && $4 ~ /cpuset/ { print $2; exit (match($4, /noprefix/) > 0) }' /proc/self/mounts) || cpusetprefix=''
if [ -z "$cpusetdir" ]; then
	cpusetdir="$cpusetdefaultdir"
	[ -d $cpusetdir ] || mkdir $cpusetdir
	mount -t cgroup -o cpuset none $cpusetdir || fail "Couldn't mount cpusets. Not in kernel or already in use?"
fi

lacpusetdir="$cpusetdir/$ladirname"
lacpusetfile="$lacpusetdir/$cpusetprefix"
sysfsdir="$debugdir/$ladirname"

[ "$samplefreq" -ne 0 ] || fail "Invalid sample frequency"

[ -d "$sysfsdir" ] || fail "Could not find logic analyzer root dir '$sysfsdir'. Module loaded?"
[ -x "$sysfsdir" ] || fail "Could not access logic analyzer root dir '$sysfsdir'. Need root?"

[ $listinstances -gt 0 ] && find "$sysfsdir" -mindepth 1 -type d | sed 's|.*/||' && exit 0

if [ -n "$lainstance" ]; then
	lasysfsdir="$sysfsdir/$lainstance"
else
	lasysfsdir=$(find "$sysfsdir" -mindepth 1 -type d -print -quit)
fi
[ -d "$lasysfsdir" ] || fail "Logic analyzer directory '$lasysfsdir' not found!"
[ -d "$outputdir" ] || fail "Output directory '$outputdir' not found!"

[ -n "$initcpu" ] && init_cpu "$initcpu"
[ -d "$lacpusetdir" ] || { echo "Auto-Isolating CPU1"; init_cpu 1; }

ndelay=$((1000000000 / samplefreq))
echo "$ndelay" > "$lasysfsdir"/delay_ns

[ -n "$duration" ] && numsamples=$((samplefreq * duration / 1000000))
echo $numsamples > "$lasysfsdir"/buf_size

if [ -n "$triggerdat" ]; then
	parse_triggerdat "$triggerdat"
	printf "$trigger_bindat" > "$lasysfsdir"/trigger 2>/dev/null || fail "Trigger data '$triggerdat' rejected"
fi

workcpu=$(cat "${lacpusetfile}effective_cpus")
[ -n "$workcpu" ] || fail "No isolated CPU found"
cpumask=$(printf '%x' $((1 << workcpu)))
instance=${lasysfsdir##*/}
echo "Setting up '$instance': $numsamples samples at ${samplefreq}Hz with ${triggerdat:-no} trigger using CPU$workcpu"
do_capture "$cpumask" &
