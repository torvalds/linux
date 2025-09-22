/*	$OpenBSD: tty_subr.c,v 1.36 2022/08/14 01:58:28 jsg Exp $	*/
/*	$NetBSD: tty_subr.c,v 1.13 1996/02/09 19:00:43 christos Exp $	*/

/*
 * Copyright (c) 1993, 1994 Theo de Raadt
 * All rights reserved.
 *
 * Per Lindqvist <pgd@compuram.bbt.se> supplied an almost fully working
 * set of true clist functions that this is very loosely based on.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/malloc.h>

/*
 * If TTY_QUOTE functionality isn't required by a line discipline,
 * it can free c_cq and set it to NULL. This speeds things up,
 * and also does not use any extra memory. This is useful for (say)
 * a SLIP line discipline that wants a 32K ring buffer for data
 * but doesn't need quoting.
 */
#define QMEM(n)		((((n)-1)/NBBY)+1)

void	clrbits(u_char *, int, int);

/*
 * Initialize a particular clist. Ok, they are really ring buffers,
 * of the specified length, with/without quoting support.
 */
void
clalloc(struct clist *clp, int size, int quot)
{

	clp->c_cs = malloc(size, M_TTYS, M_WAITOK|M_ZERO);

	if (quot)
		clp->c_cq = malloc(QMEM(size), M_TTYS, M_WAITOK|M_ZERO);
	else
		clp->c_cq = NULL;

	clp->c_cf = clp->c_cl = NULL;
	clp->c_ce = clp->c_cs + size;
	clp->c_cn = size;
	clp->c_cc = 0;
}

void
clfree(struct clist *clp)
{
	if (clp->c_cs) {
		explicit_bzero(clp->c_cs, clp->c_cn);
		free(clp->c_cs, M_TTYS, clp->c_cn);
	}
	if (clp->c_cq) {
		explicit_bzero(clp->c_cq, QMEM(clp->c_cn));
		free(clp->c_cq, M_TTYS, QMEM(clp->c_cn));
	}
	clp->c_cs = clp->c_cq = NULL;
}


/*
 * Get a character from a clist.
 */
int
getc(struct clist *clp)
{
	int c = -1;
	int s;

	s = spltty();
	if (clp->c_cc == 0)
		goto out;

	c = *clp->c_cf & 0xff;
	*clp->c_cf = 0;
	if (clp->c_cq) {
		if (isset(clp->c_cq, clp->c_cf - clp->c_cs))
			c |= TTY_QUOTE;
		clrbit(clp->c_cq, clp->c_cf - clp->c_cs);
	}
	if (++clp->c_cf == clp->c_ce)
		clp->c_cf = clp->c_cs;
	if (--clp->c_cc == 0)
		clp->c_cf = clp->c_cl = NULL;
out:
	splx(s);
	return c;
}

/*
 * Copy clist to buffer.
 * Return number of bytes moved.
 */
int
q_to_b(struct clist *clp, u_char *cp, int count)
{
	int cc;
	u_char *p = cp;
	int s;

	s = spltty();
	/* optimize this while loop */
	while (count > 0 && clp->c_cc > 0) {
		cc = clp->c_cl - clp->c_cf;
		if (clp->c_cf >= clp->c_cl)
			cc = clp->c_ce - clp->c_cf;
		if (cc > count)
			cc = count;
		memcpy(p, clp->c_cf, cc);
		memset(clp->c_cf, 0, cc);
		if (clp->c_cq)
			clrbits(clp->c_cq, clp->c_cf - clp->c_cs, cc);
		count -= cc;
		p += cc;
		clp->c_cc -= cc;
		clp->c_cf += cc;
		if (clp->c_cf == clp->c_ce)
			clp->c_cf = clp->c_cs;
	}
	if (clp->c_cc == 0)
		clp->c_cf = clp->c_cl = NULL;
	splx(s);
	return p - cp;
}

/*
 * Return count of contiguous characters in clist.
 * Stop counting if flag&character is non-null.
 */
int
ndqb(struct clist *clp, int flag)
{
	int count = 0;
	int i;
	int cc;
	int s;

	s = spltty();
	if ((cc = clp->c_cc) == 0)
		goto out;

	if (flag == 0) {
		count = clp->c_cl - clp->c_cf;
		if (count <= 0)
			count = clp->c_ce - clp->c_cf;
		goto out;
	}

	i = clp->c_cf - clp->c_cs;
	if (flag & TTY_QUOTE) {
		while (cc-- > 0 && !(clp->c_cs[i++] & (flag & ~TTY_QUOTE) ||
		    isset(clp->c_cq, i))) {
			count++;
			if (i == clp->c_cn)
				break;
		}
	} else {
		while (cc-- > 0 && !(clp->c_cs[i++] & flag)) {
			count++;
			if (i == clp->c_cn)
				break;
		}
	}
out:
	splx(s);
	return count;
}

/*
 * Flush count bytes from clist.
 */
void
ndflush(struct clist *clp, int count)
{
	int cc;
	int s;

	s = spltty();
	if (count == clp->c_cc) {
		clp->c_cc = 0;
		clp->c_cf = clp->c_cl = NULL;
		goto out;
	}
	/* optimize this while loop */
	while (count > 0 && clp->c_cc > 0) {
		cc = clp->c_cl - clp->c_cf;
		if (clp->c_cf >= clp->c_cl)
			cc = clp->c_ce - clp->c_cf;
		if (cc > count)
			cc = count;
		count -= cc;
		clp->c_cc -= cc;
		clp->c_cf += cc;
		if (clp->c_cf == clp->c_ce)
			clp->c_cf = clp->c_cs;
	}
	if (clp->c_cc == 0)
		clp->c_cf = clp->c_cl = NULL;
out:
	splx(s);
}

/*
 * Put a character into the output queue.
 */
int
putc(int c, struct clist *clp)
{
	int i;
	int s;

	s = spltty();
	if (clp->c_cc == clp->c_cn) {
		splx(s);
		return -1;
	}

	if (clp->c_cc == 0) {
		if (!clp->c_cs)
			panic("%s: tty has no clist", __func__);
		clp->c_cf = clp->c_cl = clp->c_cs;
	}

	*clp->c_cl = c & 0xff;
	i = clp->c_cl - clp->c_cs;
	if (clp->c_cq) {
		if (c & TTY_QUOTE)
			setbit(clp->c_cq, i);
		else
			clrbit(clp->c_cq, i);
	}
	clp->c_cc++;
	clp->c_cl++;
	if (clp->c_cl == clp->c_ce)
		clp->c_cl = clp->c_cs;
	splx(s);
	return 0;
}

/*
 * optimized version of
 *
 * for (i = 0; i < len; i++)
 *	clrbit(cp, off + i);
 */
void
clrbits(u_char *cp, int off, int len)
{
	int sby, sbi, eby, ebi;
	int i;
	u_char mask;

	if (len==1) {
		clrbit(cp, off);
		return;
	}

	sby = off / NBBY;
	sbi = off % NBBY;
	eby = (off+len) / NBBY;
	ebi = (off+len) % NBBY;
	if (sby == eby) {
		mask = ((1 << (ebi - sbi)) - 1) << sbi;
		cp[sby] &= ~mask;
	} else {
		mask = (1<<sbi) - 1;
		cp[sby++] &= mask;

		for (i = sby; i < eby; i++)
			cp[i] = 0x00;

		mask = (1<<ebi) - 1;
		if (mask)	/* if no mask, eby may be 1 too far */
			cp[eby] &= ~mask;

	}
}

/*
 * Copy buffer to clist.
 * Return number of bytes not transferred.
 */
int
b_to_q(u_char *cp, int count, struct clist *clp)
{
	int cc;
	u_char *p = cp;
	int s;

	if (count <= 0)
		return 0;

	s = spltty();
	if (clp->c_cc == clp->c_cn)
		goto out;

	if (clp->c_cc == 0) {
		if (!clp->c_cs)
			panic("%s: tty has no clist", __func__);
		clp->c_cf = clp->c_cl = clp->c_cs;
	}

	/* optimize this while loop */
	while (count > 0 && clp->c_cc < clp->c_cn) {
		cc = clp->c_ce - clp->c_cl;
		if (clp->c_cf > clp->c_cl)
			cc = clp->c_cf - clp->c_cl;
		if (cc > count)
			cc = count;
		memcpy(clp->c_cl, p, cc);
		if (clp->c_cq)
			clrbits(clp->c_cq, clp->c_cl - clp->c_cs, cc);
		p += cc;
		count -= cc;
		clp->c_cc += cc;
		clp->c_cl += cc;
		if (clp->c_cl == clp->c_ce)
			clp->c_cl = clp->c_cs;
	}
out:
	splx(s);
	return count;
}

/*
 * Given a non-NULL pointer into the clist return the pointer
 * to the next character in the list or return NULL if no more chars.
 *
 * Callers must not allow getc's to happen between firstc's and nextc's
 * so that the pointer becomes invalid.  Note that interrupts are NOT
 * masked.
 */
u_char *
nextc(struct clist *clp, u_char *cp, int *c, int *ccp)
{

	if (clp->c_cf == cp) {
		/*
		 * First time initialization.
		 */
		*ccp = clp->c_cc;
	}
	if (*ccp == 0 || cp == NULL)
		return NULL;
	if (--(*ccp) == 0)
		return NULL;
	if (++cp == clp->c_ce)
		cp = clp->c_cs;
	*c = *cp & 0xff;
	if (clp->c_cq) {
		if (isset(clp->c_cq, cp - clp->c_cs))
			*c |= TTY_QUOTE;
	}
	return cp;
}

/*
 * Given a non-NULL pointer into the clist return the pointer
 * to the first character in the list or return NULL if no more chars.
 *
 * Callers must not allow getc's to happen between firstc's and nextc's
 * so that the pointer becomes invalid.  Note that interrupts are NOT
 * masked.
 *
 * *c is set to the NEXT character
 */
u_char *
firstc(struct clist *clp, int *c, int *ccp)
{
	u_char *cp;

	*ccp = clp->c_cc;
	if (*ccp == 0)
		return NULL;
	cp = clp->c_cf;
	*c = *cp & 0xff;
	if (clp->c_cq) {
		if (isset(clp->c_cq, cp - clp->c_cs))
			*c |= TTY_QUOTE;
	}
	return clp->c_cf;
}

/*
 * Remove the last character in the clist and return it.
 */
int
unputc(struct clist *clp)
{
	unsigned int c = -1;
	int s;

	s = spltty();
	if (clp->c_cc == 0)
		goto out;

	if (clp->c_cl == clp->c_cs)
		clp->c_cl = clp->c_ce - 1;
	else
		--clp->c_cl;
	clp->c_cc--;

	c = *clp->c_cl & 0xff;
	*clp->c_cl = 0;
	if (clp->c_cq) {
		if (isset(clp->c_cq, clp->c_cl - clp->c_cs))
			c |= TTY_QUOTE;
		clrbit(clp->c_cq, clp->c_cl - clp->c_cs);
	}
	if (clp->c_cc == 0)
		clp->c_cf = clp->c_cl = NULL;
out:
	splx(s);
	return c;
}

/*
 * Put the chars in the from queue on the end of the to queue.
 */
void
catq(struct clist *from, struct clist *to)
{
	int c;
	int s;

	s = spltty();
	if (from->c_cc == 0) {	/* nothing to move */
		splx(s);
		return;
	}

	/*
	 * if `to' queue is empty and the queues are the same max size,
	 * it is more efficient to just swap the clist structures.
	 */
	if (to->c_cc == 0 && from->c_cn == to->c_cn) {
		struct clist tmp;

		tmp = *from;
		*from = *to;
		*to = tmp;
		splx(s);
		return;
	}
	splx(s);

	while ((c = getc(from)) != -1)
		putc(c, to);
}
