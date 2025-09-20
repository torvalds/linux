SCHED_EXT EXAMPLE SCHEDULERS
============================

# Introduction

This directory contains a number of example sched_ext schedulers. These
schedulers are meant to provide examples of different types of schedulers
that can be built using sched_ext, and illustrate how various features of
sched_ext can be used.

Some of the examples are performant, production-ready schedulers. That is, for
the correct workload and with the correct tuning, they may be deployed in a
production environment with acceptable or possibly even improved performance.
Others are just examples that in practice, would not provide acceptable
performance (though they could be improved to get there).

This README will describe these example schedulers, including describing the
types of workloads or scenarios they're designed to accommodate, and whether or
not they're production ready. For more details on any of these schedulers,
please see the header comment in their .bpf.c file.


# Compiling the examples

There are a few toolchain dependencies for compiling the example schedulers.

## Toolchain dependencies

1. clang >= 16.0.0

The schedulers are BPF programs, and therefore must be compiled with clang. gcc
is actively working on adding a BPF backend compiler as well, but are still
missing some features such as BTF type tags which are necessary for using
kptrs.

2. pahole >= 1.25

You may need pahole in order to generate BTF from DWARF.

3. rust >= 1.70.0

Rust schedulers uses features present in the rust toolchain >= 1.70.0. You
should be able to use the stable build from rustup, but if that doesn't
work, try using the rustup nightly build.

There are other requirements as well, such as make, but these are the main /
non-trivial ones.

## Compiling the kernel

In order to run a sched_ext scheduler, you'll have to run a kernel compiled
with the patches in this repository, and with a minimum set of necessary
Kconfig options:

```
CONFIG_BPF=y
CONFIG_SCHED_CLASS_EXT=y
CONFIG_BPF_SYSCALL=y
CONFIG_BPF_JIT=y
CONFIG_DEBUG_INFO_BTF=y
```

It's also recommended that you also include the following Kconfig options:

```
CONFIG_BPF_JIT_ALWAYS_ON=y
CONFIG_BPF_JIT_DEFAULT_ON=y
CONFIG_PAHOLE_HAS_SPLIT_BTF=y
CONFIG_PAHOLE_HAS_BTF_TAG=y
```

There is a `Kconfig` file in this directory whose contents you can append to
your local `.config` file, as long as there are no conflicts with any existing
options in the file.

## Getting a vmlinux.h file

You may notice that most of the example schedulers include a "vmlinux.h" file.
This is a large, auto-generated header file that contains all of the types
defined in some vmlinux binary that was compiled with
[BTF](https://docs.kernel.org/bpf/btf.html) (i.e. with the BTF-related Kconfig
options specified above).

The header file is created using `bpftool`, by passing it a vmlinux binary
compiled with BTF as follows:

```bash
$ bpftool btf dump file /path/to/vmlinux format c > vmlinux.h
```

`bpftool` analyzes all of the BTF encodings in the binary, and produces a
header file that can be included by BPF programs to access those types.  For
example, using vmlinux.h allows a scheduler to access fields defined directly
in vmlinux as follows:

```c
#include "vmlinux.h"
// vmlinux.h is also implicitly included by scx_common.bpf.h.
#include "scx_common.bpf.h"

/*
 * vmlinux.h provides definitions for struct task_struct and
 * struct scx_enable_args.
 */
void BPF_STRUCT_OPS(example_enable, struct task_struct *p,
		    struct scx_enable_args *args)
{
	bpf_printk("Task %s enabled in example scheduler", p->comm);
}

// vmlinux.h provides the definition for struct sched_ext_ops.
SEC(".struct_ops.link")
struct sched_ext_ops example_ops {
	.enable	= (void *)example_enable,
	.name	= "example",
}
```

The scheduler build system will generate this vmlinux.h file as part of the
scheduler build pipeline. It looks for a vmlinux file in the following
dependency order:

1. If the O= environment variable is defined, at `$O/vmlinux`
2. If the KBUILD_OUTPUT= environment variable is defined, at
   `$KBUILD_OUTPUT/vmlinux`
3. At `../../vmlinux` (i.e. at the root of the kernel tree where you're
   compiling the schedulers)
3. `/sys/kernel/btf/vmlinux`
4. `/boot/vmlinux-$(uname -r)`

In other words, if you have compiled a kernel in your local repo, its vmlinux
file will be used to generate vmlinux.h. Otherwise, it will be the vmlinux of
the kernel you're currently running on. This means that if you're running on a
kernel with sched_ext support, you may not need to compile a local kernel at
all.

### Aside on CO-RE

One of the cooler features of BPF is that it supports
[CO-RE](https://nakryiko.com/posts/bpf-core-reference-guide/) (Compile Once Run
Everywhere). This feature allows you to reference fields inside of structs with
types defined internal to the kernel, and not have to recompile if you load the
BPF program on a different kernel with the field at a different offset. In our
example above, we print out a task name with `p->comm`. CO-RE would perform
relocations for that access when the program is loaded to ensure that it's
referencing the correct offset for the currently running kernel.

## Compiling the schedulers

Once you have your toolchain setup, and a vmlinux that can be used to generate
a full vmlinux.h file, you can compile the schedulers using `make`:

```bash
$ make -j($nproc)
```

# Example schedulers

This directory contains the following example schedulers. These schedulers are
for testing and demonstrating different aspects of sched_ext. While some may be
useful in limited scenarios, they are not intended to be practical.

For more scheduler implementations, tools and documentation, visit
https://github.com/sched-ext/scx.

## scx_simple

A simple scheduler that provides an example of a minimal sched_ext scheduler.
scx_simple can be run in either global weighted vtime mode, or FIFO mode.

Though very simple, in limited scenarios, this scheduler can perform reasonably
well on single-socket systems with a unified L3 cache.

## scx_qmap

Another simple, yet slightly more complex scheduler that provides an example of
a basic weighted FIFO queuing policy. It also provides examples of some common
useful BPF features, such as sleepable per-task storage allocation in the
`ops.prep_enable()` callback, and using the `BPF_MAP_TYPE_QUEUE` map type to
enqueue tasks. It also illustrates how core-sched support could be implemented.

## scx_central

A "central" scheduler where scheduling decisions are made from a single CPU.
This scheduler illustrates how scheduling decisions can be dispatched from a
single CPU, allowing other cores to run with infinite slices, without timer
ticks, and without having to incur the overhead of making scheduling decisions.

The approach demonstrated by this scheduler may be useful for any workload that
benefits from minimizing scheduling overhead and timer ticks. An example of
where this could be particularly useful is running VMs, where running with
infinite slices and no timer ticks allows the VM to avoid unnecessary expensive
vmexits.

## scx_flatcg

A flattened cgroup hierarchy scheduler. This scheduler implements hierarchical
weight-based cgroup CPU control by flattening the cgroup hierarchy into a single
layer, by compounding the active weight share at each level. The effect of this
is a much more performant CPU controller, which does not need to descend down
cgroup trees in order to properly compute a cgroup's share.

Similar to scx_simple, in limited scenarios, this scheduler can perform
reasonably well on single socket-socket systems with a unified L3 cache and show
significantly lowered hierarchical scheduling overhead.


# Troubleshooting

There are a number of common issues that you may run into when building the
schedulers. We'll go over some of the common ones here.

## Build Failures

### Old version of clang

```
error: static assertion failed due to requirement 'SCX_DSQ_FLAG_BUILTIN': bpftool generated vmlinux.h is missing high bits for 64bit enums, upgrade clang and pahole
        _Static_assert(SCX_DSQ_FLAG_BUILTIN,
                       ^~~~~~~~~~~~~~~~~~~~
1 error generated.
```

This means you built the kernel or the schedulers with an older version of
clang than what's supported (i.e. older than 16.0.0). To remediate this:

1. `which clang` to make sure you're using a sufficiently new version of clang.

2. `make fullclean` in the root path of the repository, and rebuild the kernel
   and schedulers.

3. Rebuild the kernel, and then your example schedulers.

The schedulers are also cleaned if you invoke `make mrproper` in the root
directory of the tree.

### Stale kernel build / incomplete vmlinux.h file

As described above, you'll need a `vmlinux.h` file that was generated from a
vmlinux built with BTF, and with sched_ext support enabled. If you don't,
you'll see errors such as the following which indicate that a type being
referenced in a scheduler is unknown:

```
/path/to/sched_ext/tools/sched_ext/user_exit_info.h:25:23: note: forward declaration of 'struct scx_exit_info'

const struct scx_exit_info *ei)

^
```

In order to resolve this, please follow the steps above in
[Getting a vmlinux.h file](#getting-a-vmlinuxh-file) in order to ensure your
schedulers are using a vmlinux.h file that includes the requisite types.

## Misc

### llvm: [OFF]

You may see the following output when building the schedulers:

```
Auto-detecting system features:
...                         clang-bpf-co-re: [ on  ]
...                                    llvm: [ OFF ]
...                                  libcap: [ on  ]
...                                  libbfd: [ on  ]
```

Seeing `llvm: [ OFF ]` here is not an issue. You can safely ignore.
