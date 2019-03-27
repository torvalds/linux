/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015  Peter Grehan <grehan@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
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
 * Guest firmware interface. Uses i/o ports x510/x511 as Qemu does,
 * but with a request/response messaging protocol.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/uio.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bhyverun.h"
#include "inout.h"
#include "fwctl.h"

/*
 * Messaging protocol base operations
 */
#define	OP_NULL		1
#define	OP_ECHO		2
#define	OP_GET		3
#define	OP_GET_LEN	4
#define	OP_SET		5
#define	OP_MAX		OP_SET

/* I/O ports */
#define	FWCTL_OUT	0x510
#define	FWCTL_IN	0x511

/*
 * Back-end state-machine
 */
enum state {
	DORMANT,
	IDENT_WAIT,
	IDENT_SEND,
	REQ,
	RESP
} be_state = DORMANT;

static uint8_t sig[] = { 'B', 'H', 'Y', 'V' };
static u_int ident_idx;

struct op_info {
	int op;
	int  (*op_start)(uint32_t len);
	void (*op_data)(uint32_t data, uint32_t len);
	int  (*op_result)(struct iovec **data);
	void (*op_done)(struct iovec *data);
};
static struct op_info *ops[OP_MAX+1];

/* Return 0-padded uint32_t */
static uint32_t
fwctl_send_rest(uint32_t *data, size_t len)
{
	union {
		uint8_t c[4];
		uint32_t w;
	} u;
	uint8_t *cdata;
	int i;

	cdata = (uint8_t *) data;
	u.w = 0;	

	for (i = 0, u.w = 0; i < len; i++)
		u.c[i] = *cdata++;

	return (u.w);
}

/*
 * error op dummy proto - drop all data sent and return an error
*/
static int errop_code;

static void
errop_set(int err)
{

	errop_code = err;
}

static int
errop_start(uint32_t len)
{
	errop_code = ENOENT;

	/* accept any length */
	return (errop_code);
}

static void
errop_data(uint32_t data, uint32_t len)
{

	/* ignore */
}

static int
errop_result(struct iovec **data)
{

	/* no data to send back; always successful */
	*data = NULL;
	return (errop_code);
}

static void
errop_done(struct iovec *data)
{

	/* assert data is NULL */
}

static struct op_info errop_info = {
	.op_start  = errop_start,
	.op_data   = errop_data,
	.op_result = errop_result,
	.op_done   = errop_done
};

/* OID search */
SET_DECLARE(ctl_set, struct ctl);

CTL_NODE("hw.ncpu", &guest_ncpus, sizeof(guest_ncpus));

static struct ctl *
ctl_locate(const char *str, int maxlen)
{
	struct ctl *cp, **cpp;

	SET_FOREACH(cpp, ctl_set)  {
		cp = *cpp;
		if (!strncmp(str, cp->c_oid, maxlen))
			return (cp);
	}

	return (NULL);
}

/* uefi-sysctl get-len */
#define FGET_STRSZ	80
static struct iovec fget_biov[2];
static char fget_str[FGET_STRSZ];
static struct {
	size_t f_sz;
	uint32_t f_data[1024];
} fget_buf;
static int fget_cnt;
static size_t fget_size;

static int
fget_start(uint32_t len)
{

	if (len > FGET_STRSZ)
		return(E2BIG);

	fget_cnt = 0;

	return (0);
}

static void
fget_data(uint32_t data, uint32_t len)
{

	*((uint32_t *) &fget_str[fget_cnt]) = data;
	fget_cnt += sizeof(uint32_t);
}

static int
fget_result(struct iovec **data, int val)
{
	struct ctl *cp;
	int err;

	err = 0;

	/* Locate the OID */
	cp = ctl_locate(fget_str, fget_cnt);
	if (cp == NULL) {
		*data = NULL;
		err = ENOENT;
	} else {
		if (val) {
			/* For now, copy the len/data into a buffer */
			memset(&fget_buf, 0, sizeof(fget_buf));
			fget_buf.f_sz = cp->c_len;
			memcpy(fget_buf.f_data, cp->c_data, cp->c_len);
			fget_biov[0].iov_base = (char *)&fget_buf;
			fget_biov[0].iov_len  = sizeof(fget_buf.f_sz) +
				cp->c_len;
		} else {
			fget_size = cp->c_len;
			fget_biov[0].iov_base = (char *)&fget_size;
			fget_biov[0].iov_len  = sizeof(fget_size);
		}

		fget_biov[1].iov_base = NULL;
		fget_biov[1].iov_len  = 0;
		*data = fget_biov;
	}

	return (err);
}

static void
fget_done(struct iovec *data)
{

	/* nothing needs to be freed */
}

static int
fget_len_result(struct iovec **data)
{
	return (fget_result(data, 0));
}

static int
fget_val_result(struct iovec **data)
{
	return (fget_result(data, 1));
}

static struct op_info fgetlen_info = {
	.op_start  = fget_start,
	.op_data   = fget_data,
	.op_result = fget_len_result,
	.op_done   = fget_done
};

static struct op_info fgetval_info = {
	.op_start  = fget_start,
	.op_data   = fget_data,
	.op_result = fget_val_result,
	.op_done   = fget_done
};

static struct req_info {
	int      req_error;
	u_int    req_count;
	uint32_t req_size;
	uint32_t req_type;
	uint32_t req_txid;
	struct op_info *req_op;
	int	 resp_error;
	int	 resp_count;
	size_t	 resp_size;
	size_t	 resp_off;
	struct iovec *resp_biov;
} rinfo;

static void
fwctl_response_done(void)
{

	(*rinfo.req_op->op_done)(rinfo.resp_biov);

	/* reinit the req data struct */
	memset(&rinfo, 0, sizeof(rinfo));
}

static void
fwctl_request_done(void)
{

	rinfo.resp_error = (*rinfo.req_op->op_result)(&rinfo.resp_biov);

	/* XXX only a single vector supported at the moment */
	rinfo.resp_off = 0;
	if (rinfo.resp_biov == NULL) {
		rinfo.resp_size = 0;
	} else {
		rinfo.resp_size = rinfo.resp_biov[0].iov_len;
	}
}

static int
fwctl_request_start(void)
{
	int err;

	/* Data size doesn't include header */
	rinfo.req_size -= 12;

	rinfo.req_op = &errop_info;
	if (rinfo.req_type <= OP_MAX && ops[rinfo.req_type] != NULL)
		rinfo.req_op = ops[rinfo.req_type];

	err = (*rinfo.req_op->op_start)(rinfo.req_size);

	if (err) {
		errop_set(err);
		rinfo.req_op = &errop_info;
	}

	/* Catch case of zero-length message here */
	if (rinfo.req_size == 0) {
		fwctl_request_done();
		return (1);
	}

	return (0);
}

static int
fwctl_request_data(uint32_t value)
{

	/* Make sure remaining size is >= 0 */
	if (rinfo.req_size <= sizeof(uint32_t))
		rinfo.req_size = 0;
	else
		rinfo.req_size -= sizeof(uint32_t);

	(*rinfo.req_op->op_data)(value, rinfo.req_size);

	if (rinfo.req_size < sizeof(uint32_t)) {
		fwctl_request_done();
		return (1);
	}

	return (0);
}

static int
fwctl_request(uint32_t value)
{

	int ret;

	ret = 0;

	switch (rinfo.req_count) {
	case 0:
		/* Verify size */
		if (value < 12) {
			printf("msg size error");
			exit(4);
		}
		rinfo.req_size = value;
		rinfo.req_count = 1;
		break;
	case 1:
		rinfo.req_type = value;
		rinfo.req_count++;
		break;
	case 2:
		rinfo.req_txid = value;
		rinfo.req_count++;
		ret = fwctl_request_start();
		break;
	default:
		ret = fwctl_request_data(value);
		break;
	}

	return (ret);
}

static int
fwctl_response(uint32_t *retval)
{
	uint32_t *dp;
	ssize_t remlen;

	switch(rinfo.resp_count) {
	case 0:
		/* 4 x u32 header len + data */
		*retval = 4*sizeof(uint32_t) +
		    roundup(rinfo.resp_size, sizeof(uint32_t));
		rinfo.resp_count++;
		break;
	case 1:
		*retval = rinfo.req_type;
		rinfo.resp_count++;
		break;
	case 2:
		*retval = rinfo.req_txid;
		rinfo.resp_count++;
		break;
	case 3:
		*retval = rinfo.resp_error;
		rinfo.resp_count++;
		break;
	default:
		remlen = rinfo.resp_size - rinfo.resp_off;
		dp = (uint32_t *)
		    ((uint8_t *)rinfo.resp_biov->iov_base + rinfo.resp_off);
		if (remlen >= sizeof(uint32_t)) {
			*retval = *dp;
		} else if (remlen > 0) {
			*retval = fwctl_send_rest(dp, remlen);
		}
		rinfo.resp_off += sizeof(uint32_t);
		break;
	}

	if (rinfo.resp_count > 3 &&
	    rinfo.resp_off >= rinfo.resp_size) {
		fwctl_response_done();
		return (1);
	}

	return (0);
}


/*
 * i/o port handling.
 */
static uint8_t
fwctl_inb(void)
{
	uint8_t retval;

	retval = 0xff;

	switch (be_state) {
	case IDENT_SEND:
		retval = sig[ident_idx++];
		if (ident_idx >= sizeof(sig))
			be_state = REQ;
		break;
	default:
		break;
	}

	return (retval);
}

static void
fwctl_outw(uint16_t val)
{
	switch (be_state) {
	case IDENT_WAIT:
		if (val == 0) {
			be_state = IDENT_SEND;
			ident_idx = 0;
		}
		break;
	default:
		/* ignore */
		break;
	}
}

static uint32_t
fwctl_inl(void)
{
	uint32_t retval;

	switch (be_state) {
	case RESP:
		if (fwctl_response(&retval))
			be_state = REQ;
		break;
	default:
		retval = 0xffffffff;
		break;
	}

	return (retval);
}

static void
fwctl_outl(uint32_t val)
{

	switch (be_state) {
	case REQ:
		if (fwctl_request(val))
			be_state = RESP;
	default:
		break;
	}

}

static int
fwctl_handler(struct vmctx *ctx, int vcpu, int in, int port, int bytes,
    uint32_t *eax, void *arg)
{

	if (in) {
		if (bytes == 1)
			*eax = fwctl_inb();
		else if (bytes == 4)
			*eax = fwctl_inl();
		else
			*eax = 0xffff;
	} else {
		if (bytes == 2)
			fwctl_outw(*eax);
		else if (bytes == 4)
			fwctl_outl(*eax);
	}

	return (0);
}
INOUT_PORT(fwctl_wreg, FWCTL_OUT, IOPORT_F_INOUT, fwctl_handler);
INOUT_PORT(fwctl_rreg, FWCTL_IN,  IOPORT_F_IN,    fwctl_handler);

void
fwctl_init(void)
{

	ops[OP_GET_LEN] = &fgetlen_info;
	ops[OP_GET]     = &fgetval_info;

	be_state = IDENT_WAIT;
}
