===============
bpftool-feature
===============
-------------------------------------------------------------------------------
tool for inspection of eBPF-related parameters for Linux kernel or net device
-------------------------------------------------------------------------------

:Manual section: 8

SYNOPSIS
========

	**bpftool** [*OPTIONS*] **feature** *COMMAND*

	*OPTIONS* := { { **-j** | **--json** } [{ **-p** | **--pretty** }] }

	*COMMANDS* := { **probe** | **help** }

MAP COMMANDS
=============

|	**bpftool** **feature probe** [**kernel**] [**macros** [**prefix** *PREFIX*]]
|	**bpftool** **feature help**

DESCRIPTION
===========
	**bpftool feature probe** [**kernel**] [**macros** [**prefix** *PREFIX*]]
		  Probe the running kernel and dump a number of eBPF-related
		  parameters, such as availability of the **bpf()** system call.

		  If the **macros** keyword (but not the **-j** option) is
		  passed, a subset of the output is dumped as a list of
		  **#define** macros that are ready to be included in a C
		  header file, for example. If, additionally, **prefix** is
		  used to define a *PREFIX*, the provided string will be used
		  as a prefix to the names of the macros: this can be used to
		  avoid conflicts on macro names when including the output of
		  this command as a header file.

		  Keyword **kernel** can be omitted.

		  Note that when probed, some eBPF helpers (e.g.
		  **bpf_trace_printk**\ () or **bpf_probe_write_user**\ ()) may
		  print warnings to kernel logs.

	**bpftool feature help**
		  Print short help message.

OPTIONS
=======
	-h, --help
		  Print short generic help message (similar to **bpftool help**).

	-v, --version
		  Print version number (similar to **bpftool version**).

	-j, --json
		  Generate JSON output. For commands that cannot produce JSON, this
		  option has no effect.

	-p, --pretty
		  Generate human-readable JSON output. Implies **-j**.

SEE ALSO
========
	**bpf**\ (2),
	**bpf-helpers**\ (7),
	**bpftool**\ (8),
	**bpftool-prog**\ (8),
	**bpftool-map**\ (8),
	**bpftool-cgroup**\ (8),
	**bpftool-net**\ (8),
	**bpftool-perf**\ (8)
