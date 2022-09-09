#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Create an awk script that takes as input numbers of CPUs and outputs
# lists of CPUs, one per line in both cases.
#
# Usage: kvm-get-cpus-script.sh /path/to/cpu/arrays /path/to/put/script [ /path/to/state ]
#
# The CPU arrays are output by kvm-assign-cpus.sh, and are valid awk
# statements initializing the variables describing the system's topology.
#
# The optional state is input by this script (if the file exists and is
# non-empty), and can also be output by this script.

cpuarrays="${1-/sys/devices/system/node}"
scriptfile="${2}"
statefile="${3}"

if ! test -f "$cpuarrays"
then
	echo "File not found: $cpuarrays" 1>&2
	exit 1
fi
scriptdir="`dirname "$scriptfile"`"
if ! test -d "$scriptdir" || ! test -x "$scriptdir" || ! test -w "$scriptdir"
then
	echo "Directory not usable for script output: $scriptdir"
	exit 1
fi

cat << '___EOF___' > "$scriptfile"
BEGIN {
___EOF___
cat "$cpuarrays" >> "$scriptfile"
if test -r "$statefile"
then
	cat "$statefile" >> "$scriptfile"
fi
cat << '___EOF___' >> "$scriptfile"
}

# Do we have the system architecture to guide CPU affinity?
function gotcpus()
{
	return numnodes != "";
}

# Return a comma-separated list of the next n CPUs.
function nextcpus(n,  i, s)
{
	for (i = 0; i < n; i++) {
		if (nodecpus[curnode] == "")
			curnode = 0;
		if (cpu[curnode][curcpu[curnode]] == "")
			curcpu[curnode] = 0;
		if (s != "")
			s = s ",";
		s = s cpu[curnode][curcpu[curnode]];
		curcpu[curnode]++;
		curnode++
	}
	return s;
}

# Dump out the current node/CPU state so that a later invocation of this
# script can continue where this one left off.  Of course, this only works
# when a state file was specified and where there was valid sysfs state.
# Returns 1 if the state was dumped, 0 otherwise.
#
# Dumping the state for one system configuration and loading it into
# another isn't likely to do what you want, whatever that might be.
function dumpcpustate(  i, fn)
{
___EOF___
echo '	fn = "'"$statefile"'";' >> $scriptfile
cat << '___EOF___' >> "$scriptfile"
	if (fn != "" && gotcpus()) {
		print "curnode = " curnode ";" > fn;
		for (i = 0; i < numnodes; i++)
			if (curcpu[i] != "")
				print "curcpu[" i "] = " curcpu[i] ";" >> fn;
		return 1;
	}
	if (fn != "")
		print "# No CPU state to dump." > fn;
	return 0;
}
___EOF___
