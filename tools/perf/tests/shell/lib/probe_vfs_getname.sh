# Arnaldo Carvalho de Melo <acme@kernel.org>, 2017

perf probe -l 2>&1 | grep -q probe:vfs_getname
had_vfs_getname=$?

cleanup_probe_vfs_getname() {
	if [ $had_vfs_getname -eq 1 ] ; then
		perf probe -q -d probe:vfs_getname*
	fi
}

add_probe_vfs_getname() {
	local verbose=$1
	if [ $had_vfs_getname -eq 1 ] ; then
		line=$(perf probe -L getname_flags 2>&1 | grep -E 'result.*=.*filename;' | sed -r 's/[[:space:]]+([[:digit:]]+)[[:space:]]+result->uptr.*/\1/')
		perf probe -q       "vfs_getname=getname_flags:${line} pathname=result->name:string" || \
		perf probe $verbose "vfs_getname=getname_flags:${line} pathname=filename:ustring"
	fi
}

skip_if_no_debuginfo() {
	add_probe_vfs_getname -v 2>&1 | grep -E -q "^(Failed to find the path for the kernel|Debuginfo-analysis is not supported)|(file has no debug information)" && return 2
	return 1
}
