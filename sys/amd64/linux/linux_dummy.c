/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Dmitry Chagin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/sdt.h>
#include <sys/systm.h>
#include <sys/proc.h>

#include <amd64/linux/linux.h>
#include <amd64/linux/linux_proto.h>
#include <compat/linux/linux_dtrace.h>
#include <compat/linux/linux_util.h>

/* DTrace init */
LIN_SDT_PROVIDER_DECLARE(LINUX_DTRACE);

UNIMPLEMENTED(afs_syscall);
UNIMPLEMENTED(create_module);	/* Added in Linux 1.0 removed in 2.6. */
UNIMPLEMENTED(epoll_ctl_old);
UNIMPLEMENTED(epoll_wait_old);
UNIMPLEMENTED(get_kernel_syms);	/* Added in Linux 1.0 removed in 2.6. */
UNIMPLEMENTED(get_thread_area);
UNIMPLEMENTED(getpmsg);
UNIMPLEMENTED(nfsservctl);	/* Added in Linux 2.2 removed in 3.1. */
UNIMPLEMENTED(putpmsg);
UNIMPLEMENTED(query_module);	/* Added in Linux 2.2 removed in 2.6. */
UNIMPLEMENTED(security);
UNIMPLEMENTED(set_thread_area);
UNIMPLEMENTED(tuxcall);
UNIMPLEMENTED(uselib);
UNIMPLEMENTED(vserver);

DUMMY(sendfile);
DUMMY(syslog);
DUMMY(setfsuid);
DUMMY(setfsgid);
DUMMY(sysfs);
DUMMY(vhangup);
DUMMY(pivot_root);
DUMMY(adjtimex);
DUMMY(swapoff);
DUMMY(init_module);
DUMMY(delete_module);
DUMMY(quotactl);
DUMMY(lookup_dcookie);
DUMMY(remap_file_pages);
DUMMY(semtimedop);
DUMMY(mbind);
DUMMY(get_mempolicy);
DUMMY(set_mempolicy);
DUMMY(mq_open);
DUMMY(mq_unlink);
DUMMY(mq_timedsend);
DUMMY(mq_timedreceive);
DUMMY(mq_notify);
DUMMY(mq_getsetattr);
DUMMY(kexec_load);
/* Linux 2.6.11: */
DUMMY(add_key);
DUMMY(request_key);
DUMMY(keyctl);
/* Linux 2.6.13: */
DUMMY(ioprio_set);
DUMMY(ioprio_get);
DUMMY(inotify_init);
DUMMY(inotify_add_watch);
DUMMY(inotify_rm_watch);
/* Linux 2.6.16: */
DUMMY(migrate_pages);
DUMMY(unshare);
/* Linux 2.6.17: */
DUMMY(splice);
DUMMY(tee);
DUMMY(sync_file_range);
DUMMY(vmsplice);
/* Linux 2.6.18: */
DUMMY(move_pages);
/* Linux 2.6.22: */
DUMMY(signalfd);
/* Linux 2.6.27: */
DUMMY(signalfd4);
DUMMY(inotify_init1);
/* Linux 2.6.31: */
DUMMY(perf_event_open);
/* Linux 2.6.38: */
DUMMY(fanotify_init);
DUMMY(fanotify_mark);
/* Linux 2.6.39: */
DUMMY(name_to_handle_at);
DUMMY(open_by_handle_at);
DUMMY(clock_adjtime);
/* Linux 3.0: */
DUMMY(setns);
DUMMY(getcpu);
/* Linux 3.2: */
DUMMY(process_vm_readv);
DUMMY(process_vm_writev);
/* Linux 3.5: */
DUMMY(kcmp);
/* Linux 3.8: */
DUMMY(finit_module);
DUMMY(sched_setattr);
DUMMY(sched_getattr);
/* Linux 3.14: */
DUMMY(renameat2);
/* Linux 3.15: */
DUMMY(seccomp);
DUMMY(memfd_create);
DUMMY(kexec_file_load);
/* Linux 3.18: */
DUMMY(bpf);
/* Linux 3.19: */
DUMMY(execveat);
/* Linux 4.2: */
DUMMY(userfaultfd);
/* Linux 4.3: */
DUMMY(membarrier);
/* Linux 4.4: */
DUMMY(mlock2);
/* Linux 4.5: */
DUMMY(copy_file_range);
/* Linux 4.6: */
DUMMY(preadv2);
DUMMY(pwritev2);
/* Linux 4.8: */
DUMMY(pkey_mprotect);
DUMMY(pkey_alloc);
DUMMY(pkey_free);
/* Linux 4.11: */
DUMMY(statx);
/* Linux 4.18: */
DUMMY(io_pgetevents);
DUMMY(rseq);
/* Linux 5.0: */
DUMMY(pidfd_send_signal);
DUMMY(io_uring_setup);
DUMMY(io_uring_enter);
DUMMY(io_uring_register);

#define DUMMY_XATTR(s)						\
int								\
linux_ ## s ## xattr(						\
    struct thread *td, struct linux_ ## s ## xattr_args *arg)	\
{								\
								\
	return (EOPNOTSUPP);					\
}
DUMMY_XATTR(set);
DUMMY_XATTR(lset);
DUMMY_XATTR(fset);
DUMMY_XATTR(get);
DUMMY_XATTR(lget);
DUMMY_XATTR(fget);
DUMMY_XATTR(list);
DUMMY_XATTR(llist);
DUMMY_XATTR(flist);
DUMMY_XATTR(remove);
DUMMY_XATTR(lremove);
DUMMY_XATTR(fremove);
