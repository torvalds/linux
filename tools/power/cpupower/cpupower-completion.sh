# -*- shell-script -*-
# bash completion script for cpupower
# Taken from git.git's completion script.

_cpupower_commands="frequency-info frequency-set idle-info idle-set set info monitor"

_frequency_info ()
{
	local flags="-f -w -l -d -p -g -a -s -y -o -m -n --freq --hwfreq --hwlimits --driver --policy --governors --related-cpus --affected-cpus --stats --latency --proc --human --no-rounding"
	local prev="${COMP_WORDS[COMP_CWORD-1]}"
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$prev" in
		frequency-info) COMPREPLY=($(compgen -W "$flags" -- "$cur")) ;;
	esac
}

_frequency_set ()
{
	local flags="-f -g --freq --governor -d --min -u --max -r --related"
	local prev="${COMP_WORDS[COMP_CWORD-1]}"
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$prev" in
		-f| --freq | -d | --min | -u | --max)
		if [ -d /sys/devices/system/cpu/cpufreq/ ] ; then
			COMPREPLY=($(compgen -W '$(cat $(ls -d /sys/devices/system/cpu/cpufreq/policy* | head -1)/scaling_available_frequencies)' -- "$cur"))
		fi ;;
		-g| --governor)
		if [ -d /sys/devices/system/cpu/cpufreq/ ] ; then
			COMPREPLY=($(compgen -W '$(cat $(ls -d /sys/devices/system/cpu/cpufreq/policy* | head -1)/scaling_available_governors)' -- "$cur"))
		fi;;
		frequency-set) COMPREPLY=($(compgen -W "$flags" -- "$cur")) ;;
	esac
}

_idle_info()
{
	local flags="-f --silent"
	local prev="${COMP_WORDS[COMP_CWORD-1]}"
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$prev" in
		idle-info) COMPREPLY=($(compgen -W "$flags" -- "$cur")) ;;
	esac
}

_idle_set()
{
	local flags="-d --disable -e --enable -D --disable-by-latency -E --enable-all"
	local prev="${COMP_WORDS[COMP_CWORD-1]}"
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$prev" in
		idle-set) COMPREPLY=($(compgen -W "$flags" -- "$cur")) ;;
	esac
}

_set()
{
	local flags="--perf-bias, -b"
	local prev="${COMP_WORDS[COMP_CWORD-1]}"
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$prev" in
		set) COMPREPLY=($(compgen -W "$flags" -- "$cur")) ;;
	esac
}

_monitor()
{
	local flags="-l -m -i -c -v"
	local prev="${COMP_WORDS[COMP_CWORD-1]}"
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$prev" in
		monitor) COMPREPLY=($(compgen -W "$flags" -- "$cur")) ;;
	esac
}

_taskset()
{
	local prev_to_prev="${COMP_WORDS[COMP_CWORD-2]}"
	local prev="${COMP_WORDS[COMP_CWORD-1]}"
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$prev_to_prev" in
		-c|--cpu) COMPREPLY=($(compgen -W "$_cpupower_commands" -- "$cur")) ;;
	esac
	case "$prev" in
		frequency-info) _frequency_info ;;
		frequency-set) _frequency_set ;;
		idle-info) _idle_info ;;
		idle-set) _idle_set ;;
		set) _set ;;
		monitor) _monitor ;;
	esac

}

_cpupower ()
{
	local i
	local c=1
	local command

	while test $c -lt $COMP_CWORD; do
		if test $c == 1; then
			command="${COMP_WORDS[c]}"
		fi
		c=$((++c))
	done

	# Complete name of subcommand if the user has not finished typing it yet.
	if test $c -eq $COMP_CWORD -a -z "$command"; then
		COMPREPLY=($(compgen -W "help -v --version -c --cpu $_cpupower_commands" -- "${COMP_WORDS[COMP_CWORD]}"))
		return
	fi

	# Complete arguments to subcommands.
	case "$command" in
		-v|--version) return ;;
		-c|--cpu) _taskset ;;
		help) COMPREPLY=($(compgen -W "$_cpupower_commands" -- "${COMP_WORDS[COMP_CWORD]}")) ;;
		frequency-info) _frequency_info ;;
		frequency-set) _frequency_set ;;
		idle-info) _idle_info ;;
		idle-set) _idle_set ;;
		set) _set ;;
		monitor) _monitor ;;
	esac
}

complete -o bashdefault -o default -F _cpupower cpupower 2>/dev/null \
    || complete -o default -F _cpupower cpupower
