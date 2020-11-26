-h, --help
	  Print short help message (similar to **bpftool help**).

-V, --version
	  Print version number (similar to **bpftool version**), and optional
	  features that were included when bpftool was compiled. Optional
	  features include linking against libbfd to provide the disassembler
	  for JIT-ted programs (**bpftool prog dump jited**) and usage of BPF
	  skeletons (some features like **bpftool prog profile** or showing
	  pids associated to BPF objects may rely on it).

-j, --json
	  Generate JSON output. For commands that cannot produce JSON, this
	  option has no effect.

-p, --pretty
	  Generate human-readable JSON output. Implies **-j**.

-d, --debug
	  Print all logs available, even debug-level information. This includes
	  logs from libbpf as well as from the verifier, when attempting to
	  load programs.
