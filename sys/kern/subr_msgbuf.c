/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Ian Dowse.  All rights reserved.
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

/*
 * Generic message buffer support routines.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/mutex.h>
#include <sys/msgbuf.h>
#include <sys/sysctl.h>

/*
 * Maximum number conversion buffer length: uintmax_t in base 2, plus <>
 * around the priority, and a terminating NUL.
 */
#define	MAXPRIBUF	(sizeof(intmax_t) * NBBY + 3)

/* Read/write sequence numbers are modulo a multiple of the buffer size. */
#define SEQMOD(size) ((size) * 16)

static u_int msgbuf_cksum(struct msgbuf *mbp);

/*
 * Timestamps in msgbuf are useful when trying to diagnose when core dumps
 * or other actions occurred.
 */
static int msgbuf_show_timestamp = 0;
SYSCTL_INT(_kern, OID_AUTO, msgbuf_show_timestamp, CTLFLAG_RWTUN,
    &msgbuf_show_timestamp, 0, "Show timestamp in msgbuf");

/*
 * Initialize a message buffer of the specified size at the specified
 * location. This also zeros the buffer area.
 */
void
msgbuf_init(struct msgbuf *mbp, void *ptr, int size)
{

	mbp->msg_ptr = ptr;
	mbp->msg_size = size;
	mbp->msg_seqmod = SEQMOD(size);
	msgbuf_clear(mbp);
	mbp->msg_magic = MSG_MAGIC;
	mbp->msg_lastpri = -1;
	mbp->msg_flags = 0;
	bzero(&mbp->msg_lock, sizeof(mbp->msg_lock));
	mtx_init(&mbp->msg_lock, "msgbuf", NULL, MTX_SPIN);
}

/*
 * Reinitialize a message buffer, retaining its previous contents if
 * the size and checksum are correct. If the old contents cannot be
 * recovered, the message buffer is cleared.
 */
void
msgbuf_reinit(struct msgbuf *mbp, void *ptr, int size)
{
	u_int cksum;

	if (mbp->msg_magic != MSG_MAGIC || mbp->msg_size != size) {
		msgbuf_init(mbp, ptr, size);
		return;
	}
	mbp->msg_seqmod = SEQMOD(size);
	mbp->msg_wseq = MSGBUF_SEQNORM(mbp, mbp->msg_wseq);
	mbp->msg_rseq = MSGBUF_SEQNORM(mbp, mbp->msg_rseq);
        mbp->msg_ptr = ptr;
	cksum = msgbuf_cksum(mbp);
	if (cksum != mbp->msg_cksum) {
		if (bootverbose) {
			printf("msgbuf cksum mismatch (read %x, calc %x)\n",
			    mbp->msg_cksum, cksum);
			printf("Old msgbuf not recovered\n");
		}
		msgbuf_clear(mbp);
	}

	mbp->msg_lastpri = -1;
	/* Assume that the old message buffer didn't end in a newline. */
	mbp->msg_flags |= MSGBUF_NEEDNL;
	bzero(&mbp->msg_lock, sizeof(mbp->msg_lock));
	mtx_init(&mbp->msg_lock, "msgbuf", NULL, MTX_SPIN);
}

/*
 * Clear the message buffer.
 */
void
msgbuf_clear(struct msgbuf *mbp)
{

	bzero(mbp->msg_ptr, mbp->msg_size);
	mbp->msg_wseq = 0;
	mbp->msg_rseq = 0;
	mbp->msg_cksum = 0;
}

/*
 * Get a count of the number of unread characters in the message buffer.
 */
int
msgbuf_getcount(struct msgbuf *mbp)
{
	u_int len;

	len = MSGBUF_SEQSUB(mbp, mbp->msg_wseq, mbp->msg_rseq);
	if (len > mbp->msg_size)
		len = mbp->msg_size;
	return (len);
}

/*
 * Add a character into the message buffer, and update the checksum and
 * sequence number.
 *
 * The caller should hold the message buffer spinlock.
 */

static void
msgbuf_do_addchar(struct msgbuf * const mbp, u_int * const seq, const int c)
{
	u_int pos;

	/* Make sure we properly wrap the sequence number. */
	pos = MSGBUF_SEQ_TO_POS(mbp, *seq);
	mbp->msg_cksum += (u_int)(u_char)c -
	    (u_int)(u_char)mbp->msg_ptr[pos];
	mbp->msg_ptr[pos] = c;
	*seq = MSGBUF_SEQNORM(mbp, *seq + 1);
}

/*
 * Append a character to a message buffer.
 */
void
msgbuf_addchar(struct msgbuf *mbp, int c)
{
	mtx_lock_spin(&mbp->msg_lock);

	msgbuf_do_addchar(mbp, &mbp->msg_wseq, c);

	mtx_unlock_spin(&mbp->msg_lock);
}

/*
 * Append a NUL-terminated string with a priority to a message buffer.
 * Filter carriage returns if the caller requests it.
 *
 * XXX The carriage return filtering behavior is present in the
 * msglogchar() API, however testing has shown that we don't seem to send
 * carriage returns down this path.  So do we still need it?
 */
void
msgbuf_addstr(struct msgbuf *mbp, int pri, const char *str, int filter_cr)
{
	u_int seq;
	size_t len, prefix_len;
	char prefix[MAXPRIBUF];
	char buf[32];
	int i, j, needtime;

	len = strlen(str);
	prefix_len = 0;

	/* If we have a zero-length string, no need to do anything. */
	if (len == 0)
		return;

	mtx_lock_spin(&mbp->msg_lock);

	/*
	 * If this is true, we may need to insert a new priority sequence,
	 * so prepare the prefix.
	 */
	if (pri != -1)
		prefix_len = sprintf(prefix, "<%d>", pri);

	/*
	 * Starting write sequence number.
	 */
	seq = mbp->msg_wseq;

	/*
	 * Whenever there is a change in priority, we have to insert a
	 * newline, and a priority prefix if the priority is not -1.  Here
	 * we detect whether there was a priority change, and whether we
	 * did not end with a newline.  If that is the case, we need to
	 * insert a newline before this string.
	 */
	if (mbp->msg_lastpri != pri && (mbp->msg_flags & MSGBUF_NEEDNL) != 0) {

		msgbuf_do_addchar(mbp, &seq, '\n');
		mbp->msg_flags &= ~MSGBUF_NEEDNL;
	}

	needtime = 1;
	for (i = 0; i < len; i++) {
		/*
		 * If we just had a newline, and the priority is not -1
		 * (and therefore prefix_len != 0), then we need a priority
		 * prefix for this line.
		 */
		if ((mbp->msg_flags & MSGBUF_NEEDNL) == 0 && prefix_len != 0) {
			int j;

			for (j = 0; j < prefix_len; j++)
				msgbuf_do_addchar(mbp, &seq, prefix[j]);
		}

		if (msgbuf_show_timestamp && needtime == 1 &&
		    (mbp->msg_flags & MSGBUF_NEEDNL) == 0) {

			snprintf(buf, sizeof(buf), "[%jd] ",
			    (intmax_t)time_uptime);
			for (j = 0; buf[j] != '\0'; j++)
				msgbuf_do_addchar(mbp, &seq, buf[j]);
			needtime = 0;
		}

		/*
		 * Don't copy carriage returns if the caller requested
		 * filtering.
		 * 
		 * XXX This matches the behavior of msglogchar(), but is it
		 * necessary?  Testing has shown that we don't seem to get
		 * carriage returns here.
		 */
		if ((filter_cr != 0) && (str[i] == '\r'))
			continue;

		/*
		 * Clear this flag if we see a newline.  This affects whether
		 * we need to insert a new prefix or insert a newline later.
		 */
		if (str[i] == '\n')
			mbp->msg_flags &= ~MSGBUF_NEEDNL;
		else
			mbp->msg_flags |= MSGBUF_NEEDNL;

		msgbuf_do_addchar(mbp, &seq, str[i]);
	}
	/*
	 * Update the write sequence number for the actual number of
	 * characters we put in the message buffer.  (Depends on whether
	 * carriage returns are filtered.)
	 */
	mbp->msg_wseq = seq;

	/*
	 * Set the last priority.
	 */
	mbp->msg_lastpri = pri;

	mtx_unlock_spin(&mbp->msg_lock);

}

/*
 * Read and mark as read a character from a message buffer.
 * Returns the character, or -1 if no characters are available.
 */
int
msgbuf_getchar(struct msgbuf *mbp)
{
	u_int len, wseq;
	int c;

	mtx_lock_spin(&mbp->msg_lock);

	wseq = mbp->msg_wseq;
	len = MSGBUF_SEQSUB(mbp, wseq, mbp->msg_rseq);
	if (len == 0) {
		mtx_unlock_spin(&mbp->msg_lock);
		return (-1);
	}
	if (len > mbp->msg_size)
		mbp->msg_rseq = MSGBUF_SEQNORM(mbp, wseq - mbp->msg_size);
	c = (u_char)mbp->msg_ptr[MSGBUF_SEQ_TO_POS(mbp, mbp->msg_rseq)];
	mbp->msg_rseq = MSGBUF_SEQNORM(mbp, mbp->msg_rseq + 1);

	mtx_unlock_spin(&mbp->msg_lock);

	return (c);
}

/*
 * Read and mark as read a number of characters from a message buffer.
 * Returns the number of characters that were placed in `buf'.
 */
int
msgbuf_getbytes(struct msgbuf *mbp, char *buf, int buflen)
{
	u_int len, pos, wseq;

	mtx_lock_spin(&mbp->msg_lock);

	wseq = mbp->msg_wseq;
	len = MSGBUF_SEQSUB(mbp, wseq, mbp->msg_rseq);
	if (len == 0) {
		mtx_unlock_spin(&mbp->msg_lock);
		return (0);
	}
	if (len > mbp->msg_size) {
		mbp->msg_rseq = MSGBUF_SEQNORM(mbp, wseq - mbp->msg_size);
		len = mbp->msg_size;
	}
	pos = MSGBUF_SEQ_TO_POS(mbp, mbp->msg_rseq);
	len = min(len, mbp->msg_size - pos);
	len = min(len, (u_int)buflen);

	bcopy(&mbp->msg_ptr[pos], buf, len);
	mbp->msg_rseq = MSGBUF_SEQNORM(mbp, mbp->msg_rseq + len);

	mtx_unlock_spin(&mbp->msg_lock);

	return (len);
}

/*
 * Peek at the full contents of a message buffer without marking any
 * data as read. `seqp' should point to an unsigned integer that
 * msgbuf_peekbytes() can use to retain state between calls so that
 * the whole message buffer can be read in multiple short reads.
 * To initialise this variable to the start of the message buffer,
 * call msgbuf_peekbytes() with a NULL `buf' parameter.
 *
 * Returns the number of characters that were placed in `buf'.
 */
int
msgbuf_peekbytes(struct msgbuf *mbp, char *buf, int buflen, u_int *seqp)
{
	u_int len, pos, wseq;

	mtx_lock_spin(&mbp->msg_lock);

	if (buf == NULL) {
		/* Just initialise *seqp. */
		*seqp = MSGBUF_SEQNORM(mbp, mbp->msg_wseq - mbp->msg_size);
		mtx_unlock_spin(&mbp->msg_lock);
		return (0);
	}

	wseq = mbp->msg_wseq;
	len = MSGBUF_SEQSUB(mbp, wseq, *seqp);
	if (len == 0) {
		mtx_unlock_spin(&mbp->msg_lock);
		return (0);
	}
	if (len > mbp->msg_size) {
		*seqp = MSGBUF_SEQNORM(mbp, wseq - mbp->msg_size);
		len = mbp->msg_size;
	}
	pos = MSGBUF_SEQ_TO_POS(mbp, *seqp);
	len = min(len, mbp->msg_size - pos);
	len = min(len, (u_int)buflen);
	bcopy(&mbp->msg_ptr[MSGBUF_SEQ_TO_POS(mbp, *seqp)], buf, len);
	*seqp = MSGBUF_SEQNORM(mbp, *seqp + len);

	mtx_unlock_spin(&mbp->msg_lock);

	return (len);
}

/*
 * Compute the checksum for the complete message buffer contents.
 */
static u_int
msgbuf_cksum(struct msgbuf *mbp)
{
	u_int i, sum;

	sum = 0;
	for (i = 0; i < mbp->msg_size; i++)
		sum += (u_char)mbp->msg_ptr[i];
	return (sum);
}

/*
 * Copy from one message buffer to another.
 */
void
msgbuf_copy(struct msgbuf *src, struct msgbuf *dst)
{
	int c;

	while ((c = msgbuf_getchar(src)) >= 0)
		msgbuf_addchar(dst, c);
}
