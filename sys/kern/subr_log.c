/*	$OpenBSD: subr_log.c,v 1.81 2025/06/03 00:20:31 dlg Exp $	*/
/*	$NetBSD: subr_log.c,v 1.11 1996/03/30 22:24:44 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)subr_log.c	8.1 (Berkeley) 6/10/93
 */

/*
 * Error log buffer for kernel printf's.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/ioctl.h>
#include <sys/msgbuf.h>
#include <sys/file.h>
#include <sys/tty.h>
#include <sys/signalvar.h>
#include <sys/syslog.h>
#include <sys/malloc.h>
#include <sys/filedesc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/mutex.h>
#include <sys/timeout.h>

#ifdef KTRACE
#include <sys/ktrace.h>
#endif

#include <sys/mount.h>
#include <sys/syscallargs.h>

#include <dev/cons.h>

#define LOG_RDPRI	(PZERO + 1)
#define LOG_TICK	50		/* log tick interval in msec */

#define LOG_ASYNC	0x04
#define LOG_RDWAIT	0x08

/*
 * Locking:
 *	L	log_mtx
 *	Q	log_kq_mtx
 */
struct logsoftc {
	int	sc_state;		/* [L] see above for possibilities */
	struct	klist sc_klist;		/* [Q] process waiting on kevent call */
	struct	sigio_ref sc_sigio;	/* async I/O registration */
	int	sc_need_wakeup;		/* if set, wake up waiters */
	struct timeout sc_tick;		/* wakeup poll timeout */
} logsoftc;

int	log_open;			/* also used in log() */
int	msgbufmapped;			/* is the message buffer mapped */
struct	msgbuf *msgbufp;		/* the mapped buffer, itself. */
struct	msgbuf *consbufp;		/* console message buffer. */

struct	file *syslogf;
struct	rwlock syslogf_rwlock = RWLOCK_INITIALIZER("syslogf");

/*
 * Lock that serializes access to log message buffers.
 * This should be kept as a leaf lock in order not to constrain where
 * printf(9) can be used.
 */
struct	mutex log_mtx =
    MUTEX_INITIALIZER_FLAGS(IPL_HIGH, "logmtx", MTX_NOWITNESS);
struct	mutex log_kq_mtx =
    MUTEX_INITIALIZER_FLAGS(IPL_HIGH, "logkqmtx", MTX_NOWITNESS);

void filt_logrdetach(struct knote *kn);
int filt_logread(struct knote *kn, long hint);
int filt_logmodify(struct kevent *, struct knote *);
int filt_logprocess(struct knote *, struct kevent *);

const struct filterops logread_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_logrdetach,
	.f_event	= filt_logread,
	.f_modify	= filt_logmodify,
	.f_process	= filt_logprocess,
};

int dosendsyslog(struct proc *, const char *, size_t, int, enum uio_seg);
void logtick(void *);
size_t msgbuf_getlen(struct msgbuf *);
void msgbuf_putchar_locked(struct msgbuf *, const char);

void
initmsgbuf(caddr_t buf, size_t bufsize)
{
	struct msgbuf *mbp;
	long new_bufs;

	/* Sanity-check the given size. */
	if (bufsize < sizeof(struct msgbuf))
		return;

	mbp = msgbufp = (struct msgbuf *)buf;

	new_bufs = bufsize - offsetof(struct msgbuf, msg_bufc);
	if ((mbp->msg_magic != MSG_MAGIC) || (mbp->msg_bufs != new_bufs) ||
	    (mbp->msg_bufr < 0) || (mbp->msg_bufr >= mbp->msg_bufs) ||
	    (mbp->msg_bufx < 0) || (mbp->msg_bufx >= mbp->msg_bufs)) {
		/*
		 * If the buffer magic number is wrong, has changed
		 * size (which shouldn't happen often), or is
		 * internally inconsistent, initialize it.
		 */

		memset(buf, 0, bufsize);
		mbp->msg_magic = MSG_MAGIC;
		mbp->msg_bufs = new_bufs;
	}

	/*
	 * Always start new buffer data on a new line.
	 * Avoid using log_mtx because mutexes do not work during early boot
	 * on some architectures.
	 */
	if (mbp->msg_bufx > 0 && mbp->msg_bufc[mbp->msg_bufx - 1] != '\n')
		msgbuf_putchar_locked(mbp, '\n');

	/* mark it as ready for use. */
	msgbufmapped = 1;
}

void
initconsbuf(void)
{
	/* Set up a buffer to collect /dev/console output */
	consbufp = malloc(CONSBUFSIZE, M_TTYS, M_WAITOK | M_ZERO);
	consbufp->msg_magic = MSG_MAGIC;
	consbufp->msg_bufs = CONSBUFSIZE - offsetof(struct msgbuf, msg_bufc);
}

void
msgbuf_putchar(struct msgbuf *mbp, const char c)
{
	if (mbp->msg_magic != MSG_MAGIC)
		/* Nothing we can do */
		return;

	mtx_enter(&log_mtx);
	msgbuf_putchar_locked(mbp, c);
	mtx_leave(&log_mtx);
}

void
msgbuf_putchar_locked(struct msgbuf *mbp, const char c)
{
	mbp->msg_bufc[mbp->msg_bufx++] = c;
	if (mbp->msg_bufx < 0 || mbp->msg_bufx >= mbp->msg_bufs)
		mbp->msg_bufx = 0;
	/* If the buffer is full, keep the most recent data. */
	if (mbp->msg_bufr == mbp->msg_bufx) {
		if (++mbp->msg_bufr >= mbp->msg_bufs)
			mbp->msg_bufr = 0;
		mbp->msg_bufd++;
	}
}

size_t
msgbuf_getlen(struct msgbuf *mbp)
{
	long len;

	len = mbp->msg_bufx - mbp->msg_bufr;
	if (len < 0)
		len += mbp->msg_bufs;
	return (len);
}

int
logopen(dev_t dev, int flags, int mode, struct proc *p)
{
	if (log_open)
		return (EBUSY);
	log_open = 1;
	klist_init_mutex(&logsoftc.sc_klist, &log_kq_mtx);
	sigio_init(&logsoftc.sc_sigio);
	timeout_set(&logsoftc.sc_tick, logtick, NULL);
	timeout_add_msec(&logsoftc.sc_tick, LOG_TICK);
	return (0);
}

int
logclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct file *fp;

	rw_enter_write(&syslogf_rwlock);
	fp = syslogf;
	syslogf = NULL;
	rw_exit(&syslogf_rwlock);

	if (fp)
		FRELE(fp, p);
	log_open = 0;
	timeout_del(&logsoftc.sc_tick);

	klist_invalidate(&logsoftc.sc_klist);
	klist_free(&logsoftc.sc_klist);

	logsoftc.sc_state = 0;
	sigio_free(&logsoftc.sc_sigio);
	return (0);
}

int
logread(dev_t dev, struct uio *uio, int flag)
{
	struct msgbuf *mbp = msgbufp;
	size_t l, rpos;
	int error = 0;

	mtx_enter(&log_mtx);
	while (mbp->msg_bufr == mbp->msg_bufx) {
		if (flag & IO_NDELAY) {
			error = EWOULDBLOCK;
			goto out;
		}
		logsoftc.sc_state |= LOG_RDWAIT;
		mtx_leave(&log_mtx);
		/*
		 * Set up and enter sleep manually instead of using msleep()
		 * to keep log_mtx as a leaf lock.
		 */
		sleep_setup(mbp, LOG_RDPRI | PCATCH, "klog");
		error = sleep_finish(INFSLP, logsoftc.sc_state & LOG_RDWAIT);
		mtx_enter(&log_mtx);
		if (error)
			goto out;
	}

	if (mbp->msg_bufd > 0) {
		char buf[64];
		long ndropped;

		ndropped = mbp->msg_bufd;
		mtx_leave(&log_mtx);
		l = snprintf(buf, sizeof(buf),
		    "<%d>klog: dropped %ld byte%s, message buffer full\n",
		    LOG_KERN|LOG_WARNING, ndropped,
		    ndropped == 1 ? "" : "s");
		error = uiomove(buf, ulmin(l, sizeof(buf) - 1), uio);
		mtx_enter(&log_mtx);
		if (error)
			goto out;
		mbp->msg_bufd -= ndropped;
	}

	while (uio->uio_resid > 0) {
		if (mbp->msg_bufx >= mbp->msg_bufr)
			l = mbp->msg_bufx - mbp->msg_bufr;
		else
			l = mbp->msg_bufs - mbp->msg_bufr;
		l = ulmin(l, uio->uio_resid);
		if (l == 0)
			break;
		rpos = mbp->msg_bufr;
		mtx_leave(&log_mtx);
		/* Ignore that concurrent readers may consume the same data. */
		error = uiomove(&mbp->msg_bufc[rpos], l, uio);
		mtx_enter(&log_mtx);
		if (error)
			break;
		mbp->msg_bufr += l;
		if (mbp->msg_bufr < 0 || mbp->msg_bufr >= mbp->msg_bufs)
			mbp->msg_bufr = 0;
	}
 out:
	mtx_leave(&log_mtx);
	return (error);
}

int
logkqfilter(dev_t dev, struct knote *kn)
{
	struct klist *klist;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		klist = &logsoftc.sc_klist;
		kn->kn_fop = &logread_filtops;
		break;
	default:
		return (EINVAL);
	}

	kn->kn_hook = (void *)msgbufp;
	klist_insert(klist, kn);

	return (0);
}

void
filt_logrdetach(struct knote *kn)
{
	klist_remove(&logsoftc.sc_klist, kn);
}

int
filt_logread(struct knote *kn, long hint)
{
	struct msgbuf *mbp = kn->kn_hook;

	mtx_enter(&log_mtx);
	kn->kn_data = msgbuf_getlen(mbp);
	mtx_leave(&log_mtx);
	return (kn->kn_data != 0);
}

int
filt_logmodify(struct kevent *kev, struct knote *kn)
{
	int active;

	mtx_enter(&log_kq_mtx);
	active = knote_modify(kev, kn);
	mtx_leave(&log_kq_mtx);

	return (active);
}

int
filt_logprocess(struct knote *kn, struct kevent *kev)
{
	int active;

	mtx_enter(&log_kq_mtx);
	active = knote_process(kn, kev);
	mtx_leave(&log_kq_mtx);

	return (active);
}

void
logwakeup(void)
{
	/*
	 * The actual wakeup has to be deferred because logwakeup() can be
	 * called in very varied contexts.
	 * Keep the print routines usable in as many situations as possible
	 * by not using locking here.
	 */

	/*
	 * Ensure that preceding stores become visible to other CPUs
	 * before the flag.
	 */
	membar_producer();

	logsoftc.sc_need_wakeup = 1;
}

void
logtick(void *arg)
{
	int state;

	if (!log_open)
		return;

	if (!logsoftc.sc_need_wakeup)
		goto out;
	logsoftc.sc_need_wakeup = 0;

	/*
	 * sc_need_wakeup has to be cleared before handling the wakeup.
	 * Visiting log_mtx ensures the proper order.
	 */

	mtx_enter(&log_mtx);
	state = logsoftc.sc_state;
	if (logsoftc.sc_state & LOG_RDWAIT)
		logsoftc.sc_state &= ~LOG_RDWAIT;
	mtx_leave(&log_mtx);

	knote(&logsoftc.sc_klist, 0);

	if (state & LOG_ASYNC)
		pgsigio(&logsoftc.sc_sigio, SIGIO, 0);
	if (state & LOG_RDWAIT)
		wakeup(msgbufp);
out:
	timeout_add_msec(&logsoftc.sc_tick, LOG_TICK);
}

int
logioctl(dev_t dev, u_long com, caddr_t data, int flag, struct proc *p)
{
	struct file *fp, *newfp;
	int error;

	switch (com) {

	/* return number of characters immediately available */
	case FIONREAD:
		mtx_enter(&log_mtx);
		*(int *)data = (int)msgbuf_getlen(msgbufp);
		mtx_leave(&log_mtx);
		break;

	case FIOASYNC:
		mtx_enter(&log_mtx);
		if (*(int *)data)
			logsoftc.sc_state |= LOG_ASYNC;
		else
			logsoftc.sc_state &= ~LOG_ASYNC;
		mtx_leave(&log_mtx);
		break;

	case FIOSETOWN:
	case TIOCSPGRP:
		return (sigio_setown(&logsoftc.sc_sigio, com, data));

	case FIOGETOWN:
	case TIOCGPGRP:
		sigio_getown(&logsoftc.sc_sigio, com, data);
		break;

	case LIOCSFD:
		if ((error = suser(p)) != 0)
			return (error);
		if ((error = getsock(p, *(int *)data, &newfp)) != 0)
			return (error);

		rw_enter_write(&syslogf_rwlock);
		fp = syslogf;
		syslogf = newfp;
		rw_exit(&syslogf_rwlock);

		if (fp)
			FRELE(fp, p);
		break;

	default:
		return (ENOTTY);
	}
	return (0);
}

/*
 * If syslogd is not running, temporarily store a limited amount of messages
 * in kernel.  After log stash is full, drop messages and count them.  When
 * syslogd is available again, next log message will flush the stashed
 * messages and insert a message with drop count.  Calls to malloc(9) and
 * copyin(9) may sleep, protect data structures with rwlock.
 */

#define LOGSTASH_SIZE	100
struct logstash_message {
	char	*lgs_buffer;
	size_t	 lgs_size;
} logstash_messages[LOGSTASH_SIZE];

struct	logstash_message *logstash_in = &logstash_messages[0];
struct	logstash_message *logstash_out = &logstash_messages[0];

struct	rwlock logstash_rwlock = RWLOCK_INITIALIZER("logstash");

int	logstash_dropped, logstash_error, logstash_pid;

int	logstash_insert(const char *, size_t, int, pid_t);
void	logstash_remove(void);
int	logstash_sendsyslog(struct proc *);

static inline int
logstash_full(void)
{
	rw_assert_anylock(&logstash_rwlock);

	return logstash_out->lgs_buffer != NULL &&
	    logstash_in == logstash_out;
}

static inline void
logstash_increment(struct logstash_message **msg)
{
	rw_assert_wrlock(&logstash_rwlock);

	KASSERT((*msg) >= &logstash_messages[0]);
	KASSERT((*msg) < &logstash_messages[LOGSTASH_SIZE]);
	if ((*msg) == &logstash_messages[LOGSTASH_SIZE - 1])
		(*msg) = &logstash_messages[0];
	else
		(*msg)++;
}

int
logstash_insert(const char *buf, size_t nbyte, int logerror, pid_t pid)
{
	int error;

	rw_enter_write(&logstash_rwlock);

	if (logstash_full()) {
		if (logstash_dropped == 0) {
			logstash_error = logerror;
			logstash_pid = pid;
		}
		logstash_dropped++;

		rw_exit(&logstash_rwlock);
		return (0);
	}

	logstash_in->lgs_buffer = malloc(nbyte, M_LOG, M_WAITOK);
	error = copyin(buf, logstash_in->lgs_buffer, nbyte);
	if (error) {
		free(logstash_in->lgs_buffer, M_LOG, nbyte);
		logstash_in->lgs_buffer = NULL;

		rw_exit(&logstash_rwlock);
		return (error);
	}
	logstash_in->lgs_size = nbyte;
	logstash_increment(&logstash_in);

	rw_exit(&logstash_rwlock);
	return (0);
}

void
logstash_remove(void)
{
	rw_assert_wrlock(&logstash_rwlock);

	KASSERT(logstash_out->lgs_buffer != NULL);
	free(logstash_out->lgs_buffer, M_LOG, logstash_out->lgs_size);
	logstash_out->lgs_buffer = NULL;
	logstash_increment(&logstash_out);

	/* Insert dropped message in sequence where messages were dropped. */
	if (logstash_dropped) {
		size_t l, nbyte;
		char buf[80];

		l = snprintf(buf, sizeof(buf),
		    "<%d>sendsyslog: dropped %d message%s, error %d, pid %d",
		    LOG_KERN|LOG_WARNING, logstash_dropped,
		    logstash_dropped == 1 ? "" : "s",
		    logstash_error, logstash_pid);
		logstash_dropped = 0;
		logstash_error = 0;
		logstash_pid = 0;

		/* Cannot fail, we have just freed a slot. */
		KASSERT(!logstash_full());
		nbyte = ulmin(l, sizeof(buf) - 1);
		logstash_in->lgs_buffer = malloc(nbyte, M_LOG, M_WAITOK);
		memcpy(logstash_in->lgs_buffer, buf, nbyte);
		logstash_in->lgs_size = nbyte;
		logstash_increment(&logstash_in);
	}
}

int
logstash_sendsyslog(struct proc *p)
{
	int error;

	rw_enter_write(&logstash_rwlock);

	while (logstash_out->lgs_buffer != NULL) {
		error = dosendsyslog(p, logstash_out->lgs_buffer,
		    logstash_out->lgs_size, 0, UIO_SYSSPACE);
		if (error) {
			rw_exit(&logstash_rwlock);
			return (error);
		}
		logstash_remove();
	}

	rw_exit(&logstash_rwlock);
	return (0);
}

/*
 * Send syslog(3) message from userland to socketpair(2) created by syslogd(8).
 * Store message in kernel log stash for later if syslogd(8) is not available
 * or sending fails.  Send to console if LOG_CONS is set and syslogd(8) socket
 * does not exist.
 */

int
sys_sendsyslog(struct proc *p, void *v, register_t *retval)
{
	struct sys_sendsyslog_args /* {
		syscallarg(const char *) buf;
		syscallarg(size_t) nbyte;
		syscallarg(int) flags;
	} */ *uap = v;
	size_t nbyte;
	int error;

	nbyte = SCARG(uap, nbyte);
	if (nbyte > LOG_MAXLINE)
		nbyte = LOG_MAXLINE;

	logstash_sendsyslog(p);
	error = dosendsyslog(p, SCARG(uap, buf), nbyte, SCARG(uap, flags),
	    UIO_USERSPACE);
	if (error && error != EFAULT)
		logstash_insert(SCARG(uap, buf), nbyte, error, p->p_p->ps_pid);
	return (error);
}

int
dosendsyslog(struct proc *p, const char *buf, size_t nbyte, int flags,
    enum uio_seg sflg)
{
#ifdef KTRACE
	struct iovec ktriov;
#endif
	struct file *fp;
	char pri[6], *kbuf;
	struct iovec aiov;
	struct uio auio;
	size_t i, len;
	int error;

	/* Global variable syslogf may change during sleep, use local copy. */
	rw_enter_read(&syslogf_rwlock);
	fp = syslogf;
	if (fp)
		FREF(fp);
	rw_exit(&syslogf_rwlock);

	if (fp == NULL) {
		if (!ISSET(flags, LOG_CONS))
			return (ENOTCONN);
		/*
		 * Strip off syslog priority when logging to console.
		 * LOG_PRIMASK | LOG_FACMASK is 0x03ff, so at most 4
		 * decimal digits may appear in priority as <1023>.
		 */
		len = MIN(nbyte, sizeof(pri));
		if (sflg == UIO_USERSPACE) {
			if ((error = copyin(buf, pri, len)))
				return (error);
		} else
			memcpy(pri, buf, len);
		if (0 < len && pri[0] == '<') {
			for (i = 1; i < len; i++) {
				if (pri[i] < '0' || pri[i] > '9')
					break;
			}
			if (i < len && pri[i] == '>') {
				i++;
				/* There must be at least one digit <0>. */
				if (i >= 3) {
					buf += i;
					nbyte -= i;
				}
			}
		}
	}

	aiov.iov_base = (char *)buf;
	aiov.iov_len = nbyte;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_segflg = sflg;
	auio.uio_rw = UIO_WRITE;
	auio.uio_procp = p;
	auio.uio_offset = 0;
	auio.uio_resid = aiov.iov_len;
#ifdef KTRACE
	if (sflg == UIO_USERSPACE && KTRPOINT(p, KTR_GENIO))
		ktriov = aiov;
	else
		ktriov.iov_len = 0;
#endif

	len = auio.uio_resid;
	if (fp) {
		int flags = (fp->f_flag & FNONBLOCK) ? MSG_DONTWAIT : 0;
		error = sosend(fp->f_data, NULL, &auio, NULL, NULL, flags);
		if (error == 0)
			len -= auio.uio_resid;
	} else {
		KERNEL_LOCK();
		if (constty || cn_devvp) {
			error = cnwrite(0, &auio, 0);
			if (error == 0)
				len -= auio.uio_resid;
			aiov.iov_base = "\r\n";
			aiov.iov_len = 2;
			auio.uio_iov = &aiov;
			auio.uio_iovcnt = 1;
			auio.uio_segflg = UIO_SYSSPACE;
			auio.uio_rw = UIO_WRITE;
			auio.uio_procp = p;
			auio.uio_offset = 0;
			auio.uio_resid = aiov.iov_len;
			cnwrite(0, &auio, 0);
		} else {
			/* XXX console redirection breaks down... */
			if (sflg == UIO_USERSPACE) {
				kbuf = malloc(len, M_TEMP, M_WAITOK);
				error = copyin(aiov.iov_base, kbuf, len);
			} else {
				kbuf = aiov.iov_base;
				error = 0;
			}
			if (error == 0)
				for (i = 0; i < len; i++) {
					if (kbuf[i] == '\0')
						break;
					cnputc(kbuf[i]);
					auio.uio_resid--;
				}
			if (sflg == UIO_USERSPACE)
				free(kbuf, M_TEMP, len);
			if (error == 0)
				len -= auio.uio_resid;
			cnputc('\n');
		}
		KERNEL_UNLOCK();
	}

#ifdef KTRACE
	if (error == 0 && ktriov.iov_len != 0)
		ktrgenio(p, -1, UIO_WRITE, &ktriov, len);
#endif
	if (fp)
		FRELE(fp, p);
	else if (error != EFAULT)
		error = ENOTCONN;
	return (error);
}
