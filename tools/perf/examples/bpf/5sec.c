// SPDX-License-Identifier: GPL-2.0
/*
    Description:

    . Disable strace like syscall tracing (--no-syscalls), or try tracing
      just some (-e *sleep).

    . Attach a filter function to a kernel function, returning when it should
      be considered, i.e. appear on the output.

    . Run it system wide, so that any sleep of >= 5 seconds and < than 6
      seconds gets caught.

    . Ask for callgraphs using DWARF info, so that userspace can be unwound

    . While this is running, run something like "sleep 5s".

    . If we decide to add tv_nsec as well, then it becomes:

      int probe(hrtimer_nanosleep, rqtp->tv_sec rqtp->tv_nsec)(void *ctx, int err, long sec, long nsec)

      I.e. add where it comes from (rqtp->tv_nsec) and where it will be
      accessible in the function body (nsec)

    # perf trace --no-syscalls -e tools/perf/examples/bpf/5sec.c/call-graph=dwarf/
         0.000 perf_bpf_probe:func:(ffffffff9811b5f0) tv_sec=5
                                           hrtimer_nanosleep ([kernel.kallsyms])
                                           __x64_sys_nanosleep ([kernel.kallsyms])
                                           do_syscall_64 ([kernel.kallsyms])
                                           entry_SYSCALL_64 ([kernel.kallsyms])
                                           __GI___nanosleep (/usr/lib64/libc-2.26.so)
                                           rpl_nanosleep (/usr/bin/sleep)
                                           xnanosleep (/usr/bin/sleep)
                                           main (/usr/bin/sleep)
                                           __libc_start_main (/usr/lib64/libc-2.26.so)
                                           _start (/usr/bin/sleep)
    ^C#

   Copyright (C) 2018 Red Hat, Inc., Arnaldo Carvalho de Melo <acme@redhat.com>
*/

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

#define NSEC_PER_SEC	1000000000L

SEC("hrtimer_nanosleep=hrtimer_nanosleep rqtp")
int hrtimer_nanosleep(void *ctx, int err, long long sec)
{
	return sec / NSEC_PER_SEC == 5ULL;
}

char _license[] SEC("license") = "GPL";
