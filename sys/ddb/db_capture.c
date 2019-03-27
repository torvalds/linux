/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Robert N. M. Watson
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

/*
 * DDB capture support: capture kernel debugger output into a fixed-size
 * buffer for later dumping to disk or extraction from user space.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/kerneldump.h>
#include <sys/malloc.h>
#include <sys/msgbuf.h>
#include <sys/priv.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <ddb/ddb.h>
#include <ddb/db_lex.h>

/*
 * While it would be desirable to use a small block-sized buffer and dump
 * incrementally to disk in fixed-size blocks, it's not possible to enter
 * kernel dumper routines without restarting the kernel, which is undesirable
 * in the midst of debugging.  Instead, we maintain a large static global
 * buffer that we fill from DDB's output routines.
 *
 * We enforce an invariant at runtime that buffer sizes are even multiples of
 * the textdump block size, which is a design choice that we might want to
 * reconsider.
 */
static MALLOC_DEFINE(M_DDB_CAPTURE, "ddb_capture", "DDB capture buffer");

#ifndef DDB_CAPTURE_DEFAULTBUFSIZE
#define	DDB_CAPTURE_DEFAULTBUFSIZE	48*1024
#endif
#ifndef DDB_CAPTURE_MAXBUFSIZE
#define	DDB_CAPTURE_MAXBUFSIZE	5*1024*1024
#endif
#define	DDB_CAPTURE_FILENAME	"ddb.txt"	/* Captured DDB output. */

static char *db_capture_buf;
static u_int db_capture_bufsize = DDB_CAPTURE_DEFAULTBUFSIZE;
static u_int db_capture_maxbufsize = DDB_CAPTURE_MAXBUFSIZE; /* Read-only. */
static u_int db_capture_bufoff;		/* Next location to write in buffer. */
static u_int db_capture_bufpadding;	/* Amount of zero padding. */
static int db_capture_inpager;		/* Suspend capture in pager. */
static int db_capture_inprogress;	/* DDB capture currently in progress. */

struct sx db_capture_sx;		/* Lock against user thread races. */
SX_SYSINIT(db_capture_sx, &db_capture_sx, "db_capture_sx");

static SYSCTL_NODE(_debug_ddb, OID_AUTO, capture, CTLFLAG_RW, 0,
    "DDB capture options");

SYSCTL_UINT(_debug_ddb_capture, OID_AUTO, bufoff, CTLFLAG_RD,
    &db_capture_bufoff, 0, "Bytes of data in DDB capture buffer");

SYSCTL_UINT(_debug_ddb_capture, OID_AUTO, maxbufsize, CTLFLAG_RD,
    &db_capture_maxbufsize, 0,
    "Maximum value for debug.ddb.capture.bufsize");

SYSCTL_INT(_debug_ddb_capture, OID_AUTO, inprogress, CTLFLAG_RD,
    &db_capture_inprogress, 0, "DDB output capture in progress");

/*
 * Boot-time allocation of the DDB capture buffer, if any.  Force all buffer
 * sizes, including the maximum size, to be rounded to block sizes.
 */
static void
db_capture_sysinit(__unused void *dummy)
{

	TUNABLE_INT_FETCH("debug.ddb.capture.bufsize", &db_capture_bufsize);
	db_capture_maxbufsize = roundup(db_capture_maxbufsize,
	    TEXTDUMP_BLOCKSIZE);
	db_capture_bufsize = roundup(db_capture_bufsize, TEXTDUMP_BLOCKSIZE);
	if (db_capture_bufsize > db_capture_maxbufsize)
		db_capture_bufsize = db_capture_maxbufsize;
	if (db_capture_bufsize != 0)
		db_capture_buf = malloc(db_capture_bufsize, M_DDB_CAPTURE,
		    M_WAITOK);
}
SYSINIT(db_capture, SI_SUB_DDB_SERVICES, SI_ORDER_ANY, db_capture_sysinit,
    NULL);

/*
 * Run-time adjustment of the capture buffer.
 */
static int
sysctl_debug_ddb_capture_bufsize(SYSCTL_HANDLER_ARGS)
{
	u_int len, size;
	char *buf;
	int error;

	size = db_capture_bufsize;
	error = sysctl_handle_int(oidp, &size, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	size = roundup(size, TEXTDUMP_BLOCKSIZE);
	if (size > db_capture_maxbufsize)
		return (EINVAL);
	sx_xlock(&db_capture_sx);
	if (size != 0) {
		/*
		 * Potentially the buffer is quite large, so if we can't
		 * allocate it, fail rather than waiting.
		 */
		buf = malloc(size, M_DDB_CAPTURE, M_NOWAIT);
		if (buf == NULL) {
			sx_xunlock(&db_capture_sx);
			return (ENOMEM);
		}
		len = min(db_capture_bufoff, size);
	} else {
		buf = NULL;
		len = 0;
	}
	if (db_capture_buf != NULL && buf != NULL)
		bcopy(db_capture_buf, buf, len);
	if (db_capture_buf != NULL)
		free(db_capture_buf, M_DDB_CAPTURE);
	db_capture_bufoff = len;
	db_capture_buf = buf;
	db_capture_bufsize = size;
	sx_xunlock(&db_capture_sx);

	KASSERT(db_capture_bufoff <= db_capture_bufsize,
	    ("sysctl_debug_ddb_capture_bufsize: bufoff > bufsize"));
	KASSERT(db_capture_bufsize <= db_capture_maxbufsize,
	    ("sysctl_debug_ddb_capture_maxbufsize: bufsize > maxbufsize"));

	return (0);
}
SYSCTL_PROC(_debug_ddb_capture, OID_AUTO, bufsize, CTLTYPE_UINT|CTLFLAG_RW,
    0, 0, sysctl_debug_ddb_capture_bufsize, "IU",
    "Size of DDB capture buffer");

/*
 * Sysctl to read out the capture buffer from userspace.  We require
 * privilege as sensitive process/memory information may be accessed.
 */
static int
sysctl_debug_ddb_capture_data(SYSCTL_HANDLER_ARGS)
{
	int error;
	char ch;

	error = priv_check(req->td, PRIV_DDB_CAPTURE);
	if (error)
		return (error);

	sx_slock(&db_capture_sx);
	error = SYSCTL_OUT(req, db_capture_buf, db_capture_bufoff);
	sx_sunlock(&db_capture_sx);
	if (error)
		return (error);
	ch = '\0';
	return (SYSCTL_OUT(req, &ch, sizeof(ch)));
}
SYSCTL_PROC(_debug_ddb_capture, OID_AUTO, data, CTLTYPE_STRING | CTLFLAG_RD,
    NULL, 0, sysctl_debug_ddb_capture_data, "A", "DDB capture data");

/*
 * Routines for capturing DDB output into a fixed-size buffer.  These are
 * invoked from DDB's input and output routines.  If we hit the limit on the
 * buffer, we simply drop further data.
 */
void
db_capture_write(char *buffer, u_int buflen)
{
	u_int len;

	if (db_capture_inprogress == 0 || db_capture_inpager)
		return;
	len = min(buflen, db_capture_bufsize - db_capture_bufoff);
	bcopy(buffer, db_capture_buf + db_capture_bufoff, len);
	db_capture_bufoff += len;

	KASSERT(db_capture_bufoff <= db_capture_bufsize,
	    ("db_capture_write: bufoff > bufsize"));
}

void
db_capture_writech(char ch)
{

	return (db_capture_write(&ch, sizeof(ch)));
}

void
db_capture_enterpager(void)
{

	db_capture_inpager = 1;
}

void
db_capture_exitpager(void)
{

	db_capture_inpager = 0;
}

/*
 * Zero out any bytes left in the last block of the DDB capture buffer.  This
 * is run shortly before writing the blocks to disk, rather than when output
 * capture is stopped, in order to avoid injecting nul's into the middle of
 * output.
 */
static void
db_capture_zeropad(void)
{
	u_int len;

	len = min(TEXTDUMP_BLOCKSIZE, (db_capture_bufsize -
	    db_capture_bufoff) % TEXTDUMP_BLOCKSIZE);
	bzero(db_capture_buf + db_capture_bufoff, len);
	db_capture_bufpadding = len;
}

/*
 * Reset capture state, which flushes buffers.
 */
static void
db_capture_reset(void)
{

	db_capture_inprogress = 0;
	db_capture_bufoff = 0;
	db_capture_bufpadding = 0;
}

/*
 * Start capture.  Only one session is allowed at any time, but we may
 * continue a previous session, so the buffer isn't reset.
 */
static void
db_capture_start(void)
{

	if (db_capture_inprogress) {
		db_printf("Capture already started\n");
		return;
	}
	db_capture_inprogress = 1;
}

/*
 * Terminate DDB output capture--real work is deferred to db_capture_dump,
 * which executes outside of the DDB context.  We don't zero pad here because
 * capture may be started again before the dump takes place.
 */
static void
db_capture_stop(void)
{

	if (db_capture_inprogress == 0) {
		db_printf("Capture not started\n");
		return;
	}
	db_capture_inprogress = 0;
}

/*
 * Dump DDB(4) captured output (and resets capture buffers).
 */
void
db_capture_dump(struct dumperinfo *di)
{
	u_int offset;

	if (db_capture_bufoff == 0)
		return;

	db_capture_zeropad();
	textdump_mkustar(textdump_block_buffer, DDB_CAPTURE_FILENAME,
	    db_capture_bufoff);
	(void)textdump_writenextblock(di, textdump_block_buffer);
	for (offset = 0; offset < db_capture_bufoff + db_capture_bufpadding;
	    offset += TEXTDUMP_BLOCKSIZE)
		(void)textdump_writenextblock(di, db_capture_buf + offset);
	db_capture_bufoff = 0;
	db_capture_bufpadding = 0;
}

/*-
 * DDB(4) command to manage capture:
 *
 * capture on          - start DDB output capture
 * capture off         - stop DDB output capture
 * capture reset       - reset DDB capture buffer (also stops capture)
 * capture status      - print DDB output capture status
 */
static void
db_capture_usage(void)
{

	db_error("capture [on|off|reset|status]\n");
}

void
db_capture_cmd(db_expr_t addr, bool have_addr, db_expr_t count, char *modif)
{
	int t;

	t = db_read_token();
	if (t != tIDENT) {
		db_capture_usage();
		return;
	}
	if (db_read_token() != tEOL)
		db_error("?\n");
	if (strcmp(db_tok_string, "on") == 0)
		db_capture_start();
	else if (strcmp(db_tok_string, "off") == 0)
		db_capture_stop();
	else if (strcmp(db_tok_string, "reset") == 0)
		db_capture_reset();
	else if (strcmp(db_tok_string, "status") == 0) {
		db_printf("%u/%u bytes used\n", db_capture_bufoff,
		    db_capture_bufsize);
		if (db_capture_inprogress)
			db_printf("capture is on\n");
		else
			db_printf("capture is off\n");
	} else
		db_capture_usage();
}
