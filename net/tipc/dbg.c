/*
 * net/tipc/dbg.c: TIPC print buffer routines for debugging
 * 
 * Copyright (c) 1996-2006, Ericsson AB
 * Copyright (c) 2005-2006, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "core.h"
#include "config.h"
#include "dbg.h"

static char print_string[TIPC_PB_MAX_STR];
static DEFINE_SPINLOCK(print_lock);

static struct print_buf null_buf = { NULL, 0, NULL, NULL };
struct print_buf *TIPC_NULL = &null_buf;

static struct print_buf cons_buf = { NULL, 0, NULL, NULL };
struct print_buf *TIPC_CONS = &cons_buf;

static struct print_buf log_buf = { NULL, 0, NULL, NULL };
struct print_buf *TIPC_LOG = &log_buf;


#define FORMAT(PTR,LEN,FMT) \
{\
       va_list args;\
       va_start(args, FMT);\
       LEN = vsprintf(PTR, FMT, args);\
       va_end(args);\
       *(PTR + LEN) = '\0';\
}

/*
 * Locking policy when using print buffers.
 *
 * The following routines use 'print_lock' for protection:
 * 1) tipc_printf()  - to protect its print buffer(s) and 'print_string'
 * 2) TIPC_TEE()     - to protect its print buffer(s)
 * 3) tipc_dump()    - to protect its print buffer(s) and 'print_string'
 * 4) tipc_log_XXX() - to protect TIPC_LOG
 *
 * All routines of the form tipc_printbuf_XXX() rely on the caller to prevent
 * simultaneous use of the print buffer(s) being manipulated.
 */

/**
 * tipc_printbuf_init - initialize print buffer to empty
 * @pb: pointer to print buffer structure
 * @raw: pointer to character array used by print buffer
 * @size: size of character array
 *
 * Makes the print buffer a null device that discards anything written to it
 * if the character array is too small (or absent).
 */

void tipc_printbuf_init(struct print_buf *pb, char *raw, u32 size)
{
	pb->buf = raw;
	pb->crs = raw;
	pb->size = size;
	pb->next = NULL;

	if (size < TIPC_PB_MIN_SIZE) {
		pb->buf = NULL;
	} else if (raw) {
		pb->buf[0] = 0;
		pb->buf[size-1] = ~0;
	}
}

/**
 * tipc_printbuf_reset - reinitialize print buffer to empty state
 * @pb: pointer to print buffer structure
 */

void tipc_printbuf_reset(struct print_buf *pb)
{
	tipc_printbuf_init(pb, pb->buf, pb->size);
}

/**
 * tipc_printbuf_empty - test if print buffer is in empty state
 * @pb: pointer to print buffer structure
 *
 * Returns non-zero if print buffer is empty.
 */

int tipc_printbuf_empty(struct print_buf *pb)
{
	return (!pb->buf || (pb->crs == pb->buf));
}

/**
 * tipc_printbuf_validate - check for print buffer overflow
 * @pb: pointer to print buffer structure
 * 
 * Verifies that a print buffer has captured all data written to it. 
 * If data has been lost, linearize buffer and prepend an error message
 * 
 * Returns length of print buffer data string (including trailing NUL)
 */

int tipc_printbuf_validate(struct print_buf *pb)
{
        char *err = "\n\n*** PRINT BUFFER OVERFLOW ***\n\n";
        char *cp_buf;
        struct print_buf cb;

	if (!pb->buf)
		return 0;

	if (pb->buf[pb->size - 1] == 0) {
                cp_buf = kmalloc(pb->size, GFP_ATOMIC);
                if (cp_buf != NULL){
                        tipc_printbuf_init(&cb, cp_buf, pb->size);
                        tipc_printbuf_move(&cb, pb);
                        tipc_printbuf_move(pb, &cb);
                        kfree(cp_buf);
                        memcpy(pb->buf, err, strlen(err));
                } else {
                        tipc_printbuf_reset(pb);
                        tipc_printf(pb, err);
                }
	}
	return (pb->crs - pb->buf + 1);
}

/**
 * tipc_printbuf_move - move print buffer contents to another print buffer
 * @pb_to: pointer to destination print buffer structure
 * @pb_from: pointer to source print buffer structure
 * 
 * Current contents of destination print buffer (if any) are discarded.
 * Source print buffer becomes empty if a successful move occurs.
 */

void tipc_printbuf_move(struct print_buf *pb_to, struct print_buf *pb_from)
{
	int len;

	/* Handle the cases where contents can't be moved */

	if (!pb_to->buf)
		return;

	if (!pb_from->buf) {
		tipc_printbuf_reset(pb_to);
		return;
	}

	if (pb_to->size < pb_from->size) {
		tipc_printbuf_reset(pb_to);
		tipc_printf(pb_to, "*** PRINT BUFFER MOVE ERROR ***");
		return;
	}

	/* Copy data from char after cursor to end (if used) */

	len = pb_from->buf + pb_from->size - pb_from->crs - 2;
	if ((pb_from->buf[pb_from->size-1] == 0) && (len > 0)) {
		strcpy(pb_to->buf, pb_from->crs + 1);
		pb_to->crs = pb_to->buf + len;
	} else
		pb_to->crs = pb_to->buf;

	/* Copy data from start to cursor (always) */

	len = pb_from->crs - pb_from->buf;
	strcpy(pb_to->crs, pb_from->buf);
	pb_to->crs += len;

	tipc_printbuf_reset(pb_from);
}

/**
 * tipc_printf - append formatted output to print buffer chain
 * @pb: pointer to chain of print buffers (may be NULL)
 * @fmt: formatted info to be printed
 */

void tipc_printf(struct print_buf *pb, const char *fmt, ...)
{
	int chars_to_add;
	int chars_left;
	char save_char;
	struct print_buf *pb_next;

	spin_lock_bh(&print_lock);
	FORMAT(print_string, chars_to_add, fmt);
	if (chars_to_add >= TIPC_PB_MAX_STR)
		strcpy(print_string, "*** PRINT BUFFER STRING TOO LONG ***");

	while (pb) {
		if (pb == TIPC_CONS)
			printk(print_string);
		else if (pb->buf) {
			chars_left = pb->buf + pb->size - pb->crs - 1;
			if (chars_to_add <= chars_left) {
				strcpy(pb->crs, print_string);
				pb->crs += chars_to_add;
			} else if (chars_to_add >= (pb->size - 1)) {
				strcpy(pb->buf, print_string + chars_to_add + 1
				       - pb->size);
				pb->crs = pb->buf + pb->size - 1;
			} else {
				strcpy(pb->buf, print_string + chars_left);
                                save_char = print_string[chars_left];
                                print_string[chars_left] = 0;
                                strcpy(pb->crs, print_string);
                                print_string[chars_left] = save_char;
                                pb->crs = pb->buf + chars_to_add - chars_left;
                        }
                }
		pb_next = pb->next;
		pb->next = NULL;
		pb = pb_next;
	}
	spin_unlock_bh(&print_lock);
}

/**
 * TIPC_TEE - perform next output operation on both print buffers  
 * @b0: pointer to chain of print buffers (may be NULL)
 * @b1: pointer to print buffer to add to chain
 *
 * Returns pointer to print buffer chain.
 */

struct print_buf *TIPC_TEE(struct print_buf *b0, struct print_buf *b1)
{
	struct print_buf *pb = b0;

	if (!b0 || (b0 == b1))
		return b1;

	spin_lock_bh(&print_lock);
	while (pb->next) {
		if ((pb->next == b1) || (pb->next == b0))
			pb->next = pb->next->next;
		else
			pb = pb->next;
	}
	pb->next = b1;
	spin_unlock_bh(&print_lock);
	return b0;
}

/**
 * print_to_console - write string of bytes to console in multiple chunks
 */

static void print_to_console(char *crs, int len)
{
	int rest = len;

	while (rest > 0) {
		int sz = rest < TIPC_PB_MAX_STR ? rest : TIPC_PB_MAX_STR;
		char c = crs[sz];

		crs[sz] = 0;
		printk((const char *)crs);
		crs[sz] = c;
		rest -= sz;
		crs += sz;
	}
}

/**
 * printbuf_dump - write print buffer contents to console
 */

static void printbuf_dump(struct print_buf *pb)
{
	int len;

	if (!pb->buf) {
		printk("*** PRINT BUFFER NOT ALLOCATED ***");
		return;
	}

	/* Dump print buffer from char after cursor to end (if used) */

	len = pb->buf + pb->size - pb->crs - 2;
	if ((pb->buf[pb->size - 1] == 0) && (len > 0))
		print_to_console(pb->crs + 1, len);

	/* Dump print buffer from start to cursor (always) */

	len = pb->crs - pb->buf;
	print_to_console(pb->buf, len);
}

/**
 * tipc_dump - dump non-console print buffer(s) to console
 * @pb: pointer to chain of print buffers
 */

void tipc_dump(struct print_buf *pb, const char *fmt, ...)
{
	struct print_buf *pb_next;
	int len;

	spin_lock_bh(&print_lock);
	FORMAT(print_string, len, fmt);
	printk(print_string);

	for (; pb; pb = pb->next) {
		if (pb != TIPC_CONS) {
			printk("\n---- Start of %s log dump ----\n\n",
			       (pb == TIPC_LOG) ? "global" : "local");
			printbuf_dump(pb);
			tipc_printbuf_reset(pb);
			printk("\n---- End of dump ----\n");
		}
		pb_next = pb->next;
		pb->next = NULL;
		pb = pb_next;
	}
	spin_unlock_bh(&print_lock);
}

/**
 * tipc_log_stop - free up TIPC log print buffer 
 */

void tipc_log_stop(void)
{
	spin_lock_bh(&print_lock);
	if (TIPC_LOG->buf) {
		kfree(TIPC_LOG->buf);
		TIPC_LOG->buf = NULL;
	}
	spin_unlock_bh(&print_lock);
}

/**
 * tipc_log_reinit - (re)initialize TIPC log print buffer
 * @log_size: print buffer size to use
 */

void tipc_log_reinit(int log_size)
{
	tipc_log_stop();

	if (log_size) {
		if (log_size < TIPC_PB_MIN_SIZE)
			log_size = TIPC_PB_MIN_SIZE;
		spin_lock_bh(&print_lock);
		tipc_printbuf_init(TIPC_LOG, kmalloc(log_size, GFP_ATOMIC),
				   log_size);
		spin_unlock_bh(&print_lock);
	}
}

/**
 * tipc_log_resize - reconfigure size of TIPC log buffer
 */

struct sk_buff *tipc_log_resize(const void *req_tlv_area, int req_tlv_space)
{
	u32 value;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_UNSIGNED))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	value = *(u32 *)TLV_DATA(req_tlv_area);
	value = ntohl(value);
	if (value != delimit(value, 0, 32768))
		return tipc_cfg_reply_error_string(TIPC_CFG_INVALID_VALUE
						   " (log size must be 0-32768)");
	tipc_log_reinit(value);
	return tipc_cfg_reply_none();
}

/**
 * tipc_log_dump - capture TIPC log buffer contents in configuration message
 */

struct sk_buff *tipc_log_dump(void)
{
	struct sk_buff *reply;

	spin_lock_bh(&print_lock);
	if (!TIPC_LOG->buf)
		reply = tipc_cfg_reply_ultra_string("log not activated\n");
	else if (tipc_printbuf_empty(TIPC_LOG))
		reply = tipc_cfg_reply_ultra_string("log is empty\n");
	else {
		struct tlv_desc *rep_tlv;
		struct print_buf pb;
		int str_len;

		str_len = min(TIPC_LOG->size, 32768u);
		reply = tipc_cfg_reply_alloc(TLV_SPACE(str_len));
		if (reply) {
			rep_tlv = (struct tlv_desc *)reply->data;
			tipc_printbuf_init(&pb, TLV_DATA(rep_tlv), str_len);
			tipc_printbuf_move(&pb, TIPC_LOG);
			str_len = strlen(TLV_DATA(rep_tlv)) + 1;
			skb_put(reply, TLV_SPACE(str_len));
			TLV_SET(rep_tlv, TIPC_TLV_ULTRA_STRING, NULL, str_len);
		}
	}
	spin_unlock_bh(&print_lock);
	return reply;
}

