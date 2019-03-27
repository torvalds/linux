/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Ian Dowse.  All rights reserved.
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
 *
 * $FreeBSD$
 */

#ifndef _SYS_SYSCALLSUBR_H_
#define _SYS_SYSCALLSUBR_H_

#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/mac.h>
#include <sys/mount.h>
#include <sys/_cpuset.h>
#include <sys/_domainset.h>
#include <sys/_uio.h>

struct __wrusage;
struct file;
struct filecaps;
enum idtype;
struct itimerval;
struct image_args;
struct jail;
struct kevent;
struct kevent_copyops;
struct kld_file_stat;
struct ksiginfo;
struct mbuf;
struct msghdr;
struct msqid_ds;
struct pollfd;
struct ogetdirentries_args;
struct rlimit;
struct rusage;
struct sched_param;
union semun;
struct sockaddr;
struct stat;
struct thr_param;
struct uio;

int	kern___getcwd(struct thread *td, char *buf, enum uio_seg bufseg,
	    size_t buflen, size_t path_max);
int	kern_accept(struct thread *td, int s, struct sockaddr **name,
	    socklen_t *namelen, struct file **fp);
int	kern_accept4(struct thread *td, int s, struct sockaddr **name,
	    socklen_t *namelen, int flags, struct file **fp);
int	kern_accessat(struct thread *td, int fd, const char *path,
	    enum uio_seg pathseg, int flags, int mode);
int	kern_adjtime(struct thread *td, struct timeval *delta,
	    struct timeval *olddelta);
int	kern_alternate_path(struct thread *td, const char *prefix, const char *path,
	    enum uio_seg pathseg, char **pathbuf, int create, int dirfd);
int	kern_bindat(struct thread *td, int dirfd, int fd, struct sockaddr *sa);
int	kern_break(struct thread *td, uintptr_t *addr);
int	kern_cap_ioctls_limit(struct thread *td, int fd, u_long *cmds,
	    size_t ncmds);
int	kern_cap_rights_limit(struct thread *td, int fd, cap_rights_t *rights);
int	kern_chdir(struct thread *td, const char *path, enum uio_seg pathseg);
int	kern_clock_getcpuclockid2(struct thread *td, id_t id, int which,
	    clockid_t *clk_id);
int	kern_clock_getres(struct thread *td, clockid_t clock_id,
	    struct timespec *ts);
int	kern_clock_gettime(struct thread *td, clockid_t clock_id,
	    struct timespec *ats);
int	kern_clock_nanosleep(struct thread *td, clockid_t clock_id, int flags,
	    const struct timespec *rqtp, struct timespec *rmtp);
int	kern_clock_settime(struct thread *td, clockid_t clock_id,
	    struct timespec *ats);
int	kern_close(struct thread *td, int fd);
int	kern_connectat(struct thread *td, int dirfd, int fd,
	    struct sockaddr *sa);
int	kern_cpuset_getaffinity(struct thread *td, cpulevel_t level,
	    cpuwhich_t which, id_t id, size_t cpusetsize, cpuset_t *maskp);
int	kern_cpuset_setaffinity(struct thread *td, cpulevel_t level,
	    cpuwhich_t which, id_t id, size_t cpusetsize,
	    const cpuset_t *maskp);
int	kern_cpuset_getdomain(struct thread *td, cpulevel_t level,
	    cpuwhich_t which, id_t id, size_t domainsetsize,
	    domainset_t *maskp, int *policyp);
int	kern_cpuset_setdomain(struct thread *td, cpulevel_t level,
	    cpuwhich_t which, id_t id, size_t domainsetsize,
	    const domainset_t *maskp, int policy);
int	kern_cpuset_getid(struct thread *td, cpulevel_t level,
	    cpuwhich_t which, id_t id, cpusetid_t *setid);
int	kern_cpuset_setid(struct thread *td, cpuwhich_t which,
	    id_t id, cpusetid_t setid);
int	kern_dup(struct thread *td, u_int mode, int flags, int old, int new);
int	kern_execve(struct thread *td, struct image_args *args,
	    struct mac *mac_p);
int	kern_fchmodat(struct thread *td, int fd, const char *path,
	    enum uio_seg pathseg, mode_t mode, int flag);
int	kern_fchownat(struct thread *td, int fd, const char *path,
	    enum uio_seg pathseg, int uid, int gid, int flag);
int	kern_fcntl(struct thread *td, int fd, int cmd, intptr_t arg);
int	kern_fcntl_freebsd(struct thread *td, int fd, int cmd, long arg);
int	kern_fhstat(struct thread *td, fhandle_t fh, struct stat *buf);
int	kern_fhstatfs(struct thread *td, fhandle_t fh, struct statfs *buf);
int	kern_fpathconf(struct thread *td, int fd, int name, long *valuep);
int	kern_fstat(struct thread *td, int fd, struct stat *sbp);
int	kern_fstatfs(struct thread *td, int fd, struct statfs *buf);
int	kern_fsync(struct thread *td, int fd, bool fullsync);
int	kern_ftruncate(struct thread *td, int fd, off_t length);
int	kern_futimes(struct thread *td, int fd, struct timeval *tptr,
	    enum uio_seg tptrseg);
int	kern_futimens(struct thread *td, int fd, struct timespec *tptr,
	    enum uio_seg tptrseg);
int	kern_getdirentries(struct thread *td, int fd, char *buf, size_t count,
	    off_t *basep, ssize_t *residp, enum uio_seg bufseg);
int	kern_getfsstat(struct thread *td, struct statfs **buf, size_t bufsize,
	    size_t *countp, enum uio_seg bufseg, int mode);
int	kern_getitimer(struct thread *, u_int, struct itimerval *);
int	kern_getppid(struct thread *);
int	kern_getpeername(struct thread *td, int fd, struct sockaddr **sa,
	    socklen_t *alen);
int	kern_getrusage(struct thread *td, int who, struct rusage *rup);
int	kern_getsockname(struct thread *td, int fd, struct sockaddr **sa,
	    socklen_t *alen);
int	kern_getsockopt(struct thread *td, int s, int level, int name,
	    void *optval, enum uio_seg valseg, socklen_t *valsize);
int	kern_ioctl(struct thread *td, int fd, u_long com, caddr_t data);
int	kern_jail(struct thread *td, struct jail *j);
int	kern_jail_get(struct thread *td, struct uio *options, int flags);
int	kern_jail_set(struct thread *td, struct uio *options, int flags);
int	kern_kevent(struct thread *td, int fd, int nchanges, int nevents,
	    struct kevent_copyops *k_ops, const struct timespec *timeout);
int	kern_kevent_anonymous(struct thread *td, int nevents,
	    struct kevent_copyops *k_ops);
int	kern_kevent_fp(struct thread *td, struct file *fp, int nchanges,
	    int nevents, struct kevent_copyops *k_ops,
	    const struct timespec *timeout);
int	kern_kqueue(struct thread *td, int flags, struct filecaps *fcaps);
int	kern_kldload(struct thread *td, const char *file, int *fileid);
int	kern_kldstat(struct thread *td, int fileid, struct kld_file_stat *stat);
int	kern_kldunload(struct thread *td, int fileid, int flags);
int	kern_linkat(struct thread *td, int fd1, int fd2, const char *path1,
	    const char *path2, enum uio_seg segflg, int follow);
int	kern_listen(struct thread *td, int s, int backlog);
int	kern_lseek(struct thread *td, int fd, off_t offset, int whence);
int	kern_lutimes(struct thread *td, const char *path, enum uio_seg pathseg,
	    struct timeval *tptr, enum uio_seg tptrseg);
int	kern_madvise(struct thread *td, uintptr_t addr, size_t len, int behav);
int	kern_mincore(struct thread *td, uintptr_t addr, size_t len, char *vec);
int	kern_mkdirat(struct thread *td, int fd, const char *path,
	    enum uio_seg segflg, int mode);
int	kern_mkfifoat(struct thread *td, int fd, const char *path,
	    enum uio_seg pathseg, int mode);
int	kern_mknodat(struct thread *td, int fd, const char *path,
	    enum uio_seg pathseg, int mode, dev_t dev);
int	kern_mlock(struct proc *proc, struct ucred *cred, uintptr_t addr,
	    size_t len);
int	kern_mmap(struct thread *td, uintptr_t addr, size_t size, int prot,
	    int flags, int fd, off_t pos);
int	kern_mprotect(struct thread *td, uintptr_t addr, size_t size, int prot);
int	kern_msgctl(struct thread *, int, int, struct msqid_ds *);
int	kern_msgrcv(struct thread *, int, void *, size_t, long, int, long *);
int	kern_msgsnd(struct thread *, int, const void *, size_t, int, long);
int	kern_msync(struct thread *td, uintptr_t addr, size_t size, int flags);
int	kern_munlock(struct thread *td, uintptr_t addr, size_t size);
int	kern_munmap(struct thread *td, uintptr_t addr, size_t size);
int     kern_nanosleep(struct thread *td, struct timespec *rqt,
	    struct timespec *rmt);
int	kern_ogetdirentries(struct thread *td, struct ogetdirentries_args *uap,
	    long *ploff);
int	kern_openat(struct thread *td, int fd, const char *path,
	    enum uio_seg pathseg, int flags, int mode);
int	kern_pathconf(struct thread *td, const char *path,
	    enum uio_seg pathseg, int name, u_long flags, long *valuep);
int	kern_pipe(struct thread *td, int fildes[2], int flags,
	    struct filecaps *fcaps1, struct filecaps *fcaps2);
int	kern_poll(struct thread *td, struct pollfd *fds, u_int nfds,
	    struct timespec *tsp, sigset_t *uset);
int	kern_posix_error(struct thread *td, int error);
int	kern_posix_fadvise(struct thread *td, int fd, off_t offset, off_t len,
	    int advice);
int	kern_posix_fallocate(struct thread *td, int fd, off_t offset,
	    off_t len);
int	kern_procctl(struct thread *td, enum idtype idtype, id_t id, int com,
	    void *data);
int	kern_pread(struct thread *td, int fd, void *buf, size_t nbyte,
	    off_t offset);
int	kern_preadv(struct thread *td, int fd, struct uio *auio, off_t offset);
int	kern_pselect(struct thread *td, int nd, fd_set *in, fd_set *ou,
	    fd_set *ex, struct timeval *tvp, sigset_t *uset, int abi_nfdbits);
int	kern_ptrace(struct thread *td, int req, pid_t pid, void *addr,
	    int data);
int	kern_pwrite(struct thread *td, int fd, const void *buf, size_t nbyte,
	    off_t offset);
int	kern_pwritev(struct thread *td, int fd, struct uio *auio, off_t offset);
int	kern_readlinkat(struct thread *td, int fd, const char *path,
	    enum uio_seg pathseg, char *buf, enum uio_seg bufseg, size_t count);
int	kern_readv(struct thread *td, int fd, struct uio *auio);
int	kern_recvit(struct thread *td, int s, struct msghdr *mp,
	    enum uio_seg fromseg, struct mbuf **controlp);
int	kern_renameat(struct thread *td, int oldfd, const char *old, int newfd,
	    const char *new, enum uio_seg pathseg);
int	kern_rmdirat(struct thread *td, int fd, const char *path,
	    enum uio_seg pathseg, int flag);
int	kern_sched_getparam(struct thread *td, struct thread *targettd,
	    struct sched_param *param);
int	kern_sched_getscheduler(struct thread *td, struct thread *targettd,
	    int *policy);
int	kern_sched_setparam(struct thread *td, struct thread *targettd,
	    struct sched_param *param);
int	kern_sched_setscheduler(struct thread *td, struct thread *targettd,
	    int policy, struct sched_param *param);
int	kern_sched_rr_get_interval(struct thread *td, pid_t pid,
	    struct timespec *ts);
int	kern_sched_rr_get_interval_td(struct thread *td, struct thread *targettd,
	    struct timespec *ts);
int	kern_semctl(struct thread *td, int semid, int semnum, int cmd,
	    union semun *arg, register_t *rval);
int	kern_select(struct thread *td, int nd, fd_set *fd_in, fd_set *fd_ou,
	    fd_set *fd_ex, struct timeval *tvp, int abi_nfdbits);
int	kern_sendit(struct thread *td, int s, struct msghdr *mp, int flags,
	    struct mbuf *control, enum uio_seg segflg);
int	kern_setgroups(struct thread *td, u_int ngrp, gid_t *groups);
int	kern_setitimer(struct thread *, u_int, struct itimerval *,
	    struct itimerval *);
int	kern_setrlimit(struct thread *, u_int, struct rlimit *);
int	kern_setsockopt(struct thread *td, int s, int level, int name,
	    const void *optval, enum uio_seg valseg, socklen_t valsize);
int	kern_settimeofday(struct thread *td, struct timeval *tv,
	    struct timezone *tzp);
int	kern_shm_open(struct thread *td, const char *userpath, int flags,
	    mode_t mode, struct filecaps *fcaps);
int	kern_shmat(struct thread *td, int shmid, const void *shmaddr,
	    int shmflg);
int	kern_shmctl(struct thread *td, int shmid, int cmd, void *buf,
	    size_t *bufsz);
int	kern_shutdown(struct thread *td, int s, int how);
int	kern_sigaction(struct thread *td, int sig, const struct sigaction *act,
	    struct sigaction *oact, int flags);
int	kern_sigaltstack(struct thread *td, stack_t *ss, stack_t *oss);
int	kern_sigprocmask(struct thread *td, int how,
	    sigset_t *set, sigset_t *oset, int flags);
int	kern_sigsuspend(struct thread *td, sigset_t mask);
int	kern_sigtimedwait(struct thread *td, sigset_t waitset,
	    struct ksiginfo *ksi, struct timespec *timeout);
int	kern_sigqueue(struct thread *td, pid_t pid, int signum,
	    union sigval *value);
int	kern_socket(struct thread *td, int domain, int type, int protocol);
int	kern_statat(struct thread *td, int flag, int fd, const char *path,
	    enum uio_seg pathseg, struct stat *sbp,
	    void (*hook)(struct vnode *vp, struct stat *sbp));
int	kern_statfs(struct thread *td, const char *path, enum uio_seg pathseg,
	    struct statfs *buf);
int	kern_symlinkat(struct thread *td, const char *path1, int fd,
	    const char *path2, enum uio_seg segflg);
int	kern_ktimer_create(struct thread *td, clockid_t clock_id,
	    struct sigevent *evp, int *timerid, int preset_id);
int	kern_ktimer_delete(struct thread *, int);
int	kern_ktimer_settime(struct thread *td, int timer_id, int flags,
	    struct itimerspec *val, struct itimerspec *oval);
int	kern_ktimer_gettime(struct thread *td, int timer_id,
	    struct itimerspec *val);
int	kern_ktimer_getoverrun(struct thread *td, int timer_id);
int	kern_thr_alloc(struct proc *, int pages, struct thread **);
int	kern_thr_exit(struct thread *td);
int	kern_thr_new(struct thread *td, struct thr_param *param);
int	kern_thr_suspend(struct thread *td, struct timespec *tsp);
int	kern_truncate(struct thread *td, const char *path,
	    enum uio_seg pathseg, off_t length);
int	kern_unlinkat(struct thread *td, int fd, const char *path,
	    enum uio_seg pathseg, int flag, ino_t oldinum);
int	kern_utimesat(struct thread *td, int fd, const char *path,
	    enum uio_seg pathseg, struct timeval *tptr, enum uio_seg tptrseg);
int	kern_utimensat(struct thread *td, int fd, const char *path,
	    enum uio_seg pathseg, struct timespec *tptr, enum uio_seg tptrseg,
	    int follow);
int	kern_wait(struct thread *td, pid_t pid, int *status, int options,
	    struct rusage *rup);
int	kern_wait6(struct thread *td, enum idtype idtype, id_t id, int *status,
	    int options, struct __wrusage *wrup, siginfo_t *sip);
int	kern_writev(struct thread *td, int fd, struct uio *auio);
int	kern_socketpair(struct thread *td, int domain, int type, int protocol,
	    int *rsv);

/* flags for kern_sigaction */
#define	KSA_OSIGSET	0x0001	/* uses osigact_t */
#define	KSA_FREEBSD4	0x0002	/* uses ucontext4 */

struct freebsd11_dirent;

int	freebsd11_kern_getdirentries(struct thread *td, int fd, char *ubuf, u_int
	    count, long *basep, void (*func)(struct freebsd11_dirent *));

#endif /* !_SYS_SYSCALLSUBR_H_ */
