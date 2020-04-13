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

FEATURE COMMANDS
================

|	**bpftool** **feature probe** [*COMPONENT*] [**full**] [**macros** [**prefix** *PREFIX*]]
|	**bpftool** **feature help**
|
|	*COMPONENT* := { **kernel** | **dev** *NAME* }

DESCRIPTION
===========
	**bpftool feature probe** [**kernel**] [**full**] [**macros** [**prefix** *PREFIX*]]
		  Probe the running kernel and dump a number of eBPF-related
		  parameters, such as availability of the **bpf()** system call,
		  JIT status, eBPF program types availability, eBPF helper
		  functions availability, and more.

		  By default, bpftool **does not run probes** for
		  **bpf_probe_write_user**\ () and **bpf_trace_printk**\()
		  helpers which print warnings to kernel logs. To enable them
		  and run all probes, the **full** keyword should be used.

		  If the **macros** keyword (but not the **-j** option) is
		  passed, a subset of the output is dumped as a list of
		  **#define** macros that are ready to be included in a C
		  header file, for example. If, additionally, **prefix** is
		  used to define a *PREFIX*, the provided string will be used
		  as a prefix to the names of the macros: this can be used to
		  avoid conflicts on macro names when including the output of
		  this command as a header file.

		  Keyword **kernel** can be omitted. If no probe target is
		  specified, probing the kernel is the default behaviour.

	**bpftool feature probe dev** *NAME* [**full**] [**macros** [**prefix** *PREFIX*]]
		  Probe network device for supported eBPF features and dump
		  results to the console.

		  The keywords **full**, **macros** and **prefix** have the
		  same role as when probing the kernel.

	**bpftool feature help**
		  Print short help message.

OPTIONS
=======
	-h, --help
		  Print short generic help message (similar to **bpftool help**).

	-V, --version
		  Print version number (similar to **bpftool version**).

	-j, --json
		  Generate JSON output. For commands that cannot produce JSON, this
		  option has no effect.

	-p, --pretty
		  Generate human-readable JSON output. Implies **-j**.

	-d, --debug
		  Print all logs available from libbpf, including debug-level
		  information.

SEE ALSO
========
	**bpf**\ (2),
	**bpf-helpers**\ (7),
	**bpftool**\ (8),
	**bpftool-prog**\ (8),
	**bpftool-map**\ (8),
	**bpftool-cgroup**\ (8),
	**bpftool-net**\ (8),
	**bpftool-perf**\ (8),
	**bpftool-btf**\ (8)
