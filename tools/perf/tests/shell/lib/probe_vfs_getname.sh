#!/bin/bash
# Arnaldo Carvalho de Melo <acme@kernel.org>, 2017

# Scoped to the pid for parallel runs, and not "vfs_getname*" so that perf
# trace's probe:vfs_getname* wildcard doesn't open, and so pin, the probe.
: "${vfs_getname:=getname_flags_$$}"

# The probes added, including _1, _2... for inlined copies of getname_flags.
probes_vfs_getname() {
	perf probe -l 2>/dev/null | awk '{print $1}' |
		grep -E "^probe:${vfs_getname}(_[[:digit:]]+)?$"
}

probes_vfs_getname > /dev/null
had_vfs_getname=$?

cleanup_probe_vfs_getname() {
	if [ $had_vfs_getname -eq 1 ] ; then
		local probe
		for probe in $(probes_vfs_getname); do
			perf probe -q -d "$probe"
		done
	fi
}

# A pid scoped probe is never reused, so remove it however the test exits.
trap cleanup_probe_vfs_getname exit
trap 'exit 1' term int

add_probe_vfs_getname() {
	add_probe_verbose=$1
	if [ $had_vfs_getname -eq 1 ] ; then
		local func=getname_flags
		result_initname_re="[[:space:]]+([[:digit:]]+)[[:space:]]+initname.*"
		line=$(perf probe -L getname_flags 2>&1 | grep -E "$result_initname_re" | sed -r "s/$result_initname_re/\1/")

		# Search the old regular expressions so that this will
		# pass on older kernels as well.
		if [ -z "$line" ] ; then
			result_filename_re="[[:space:]]+([[:digit:]]+)[[:space:]]+result->uptr.*"
			line=$(perf probe -L getname_flags 2>&1 | grep -E "$result_filename_re" | sed -r "s/$result_filename_re/\1/")
		fi

		if [ -z "$line" ] ; then
			result_aname_re="[[:space:]]+([[:digit:]]+)[[:space:]]+result->aname = NULL;"
			line=$(perf probe -L getname_flags 2>&1 | grep -E "$result_aname_re" | sed -r "s/$result_aname_re/\1/")
		fi

		# Since v7.0 getname_flags() is a wrapper around do_getname().
		if [ -z "$line" ] ; then
			func=do_getname
			line=$(perf probe -L $func 2>&1 | grep -E "$result_initname_re" |
				sed -r "s/$result_initname_re/\1/")
		fi

		if [ -z "$line" ] ; then
			echo "Could not find probeable line"
			return 2
		fi

		perf probe -q       "${vfs_getname}=${func}:${line} pathname=result->name:string" || \
		perf probe $add_probe_verbose "${vfs_getname}=${func}:${line} pathname=filename:ustring" || return 1
	fi
}

skip_if_no_debuginfo() {
	add_probe_vfs_getname -v 2>&1 | grep -E -q "^(Failed to find the path for the kernel|Debuginfo-analysis is not supported)|(file has no debug information)" && return 2
	return 1
}

# check if perf is compiled with libtraceevent support
skip_no_probe_record_support() {
	if [ $had_vfs_getname -eq 1 ] ; then
		perf check feature -q libtraceevent && return 1
		return 2
	fi
}
