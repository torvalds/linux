/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
 *
 * $FreeBSD$
 */

#ifndef _GDB_GDB_INT_H_
#define	_GDB_GDB_INT_H_

extern struct gdb_dbgport *gdb_cur;

extern int gdb_listening;
void gdb_consinit(void);

extern char *gdb_rxp;
extern size_t gdb_rxsz;
extern char *gdb_txp;

int gdb_rx_begin(void);
int gdb_rx_equal(const char *);
int gdb_rx_mem(unsigned char *, size_t);
int gdb_rx_varhex(uintmax_t *);

static __inline int
gdb_rx_char(void)
{
	int c;

	if (gdb_rxsz > 0) {
		c = *gdb_rxp++;
		gdb_rxsz--;
	} else
		c = -1;
	return (c);
}

void gdb_tx_begin(char);
int gdb_tx_end(void);
int gdb_tx_mem(const unsigned char *, size_t);
void gdb_tx_reg(int);
int gdb_rx_bindata(unsigned char *data, size_t datalen, size_t *amt);
int gdb_search_mem(const unsigned char *addr, size_t size,
    const unsigned char *pat, size_t patlen, const unsigned char **found);

static __inline void
gdb_tx_char(char c)
{
	*gdb_txp++ = c;
}

static __inline int
gdb_tx_empty(void)
{
	gdb_tx_begin('\0');
	return (gdb_tx_end());
}

static __inline void
gdb_tx_hex(uintmax_t n, int sz)
{
	gdb_txp += sprintf(gdb_txp, "%0*jx", sz, n);
}

static __inline int
gdb_tx_err(int err)
{
	gdb_tx_begin('E');
	gdb_tx_hex(err, 2);
	return (gdb_tx_end());
}

static __inline int
gdb_tx_ok(void)
{
	gdb_tx_begin('O');
	gdb_tx_char('K');
	return (gdb_tx_end());
}

static __inline void
gdb_tx_str(const char *s)
{
	while (*s)
		*gdb_txp++ = *s++;
}

static __inline void
gdb_tx_varhex(uintmax_t n)
{
	gdb_txp += sprintf(gdb_txp, "%jx", n);
}

#endif /* !_GDB_GDB_INT_H_ */
