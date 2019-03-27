/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017-2018 John H. Baldwin <jhb@FreeBSD.org>
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
#ifndef WITHOUT_CAPSICUM
#include <sys/capsicum.h>
#endif
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <machine/atomic.h>
#include <machine/specialreg.h>
#include <machine/vmm.h>
#include <netinet/in.h>
#include <assert.h>
#ifndef WITHOUT_CAPSICUM
#include <capsicum_helpers.h>
#endif
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <pthread_np.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <vmmapi.h>

#include "bhyverun.h"
#include "mem.h"
#include "mevent.h"

/*
 * GDB_SIGNAL_* numbers are part of the GDB remote protocol.  Most stops
 * use SIGTRAP.
 */
#define	GDB_SIGNAL_TRAP		5

static void gdb_resume_vcpus(void);
static void check_command(int fd);

static struct mevent *read_event, *write_event;

static cpuset_t vcpus_active, vcpus_suspended, vcpus_waiting;
static pthread_mutex_t gdb_lock;
static pthread_cond_t idle_vcpus;
static bool stop_pending, first_stop;
static int stepping_vcpu, stopped_vcpu;

/*
 * An I/O buffer contains 'capacity' bytes of room at 'data'.  For a
 * read buffer, 'start' is unused and 'len' contains the number of
 * valid bytes in the buffer.  For a write buffer, 'start' is set to
 * the index of the next byte in 'data' to send, and 'len' contains
 * the remaining number of valid bytes to send.
 */
struct io_buffer {
	uint8_t *data;
	size_t capacity;
	size_t start;
	size_t len;
};

static struct io_buffer cur_comm, cur_resp;
static uint8_t cur_csum;
static int cur_vcpu;
static struct vmctx *ctx;
static int cur_fd = -1;

const int gdb_regset[] = {
	VM_REG_GUEST_RAX,
	VM_REG_GUEST_RBX,
	VM_REG_GUEST_RCX,
	VM_REG_GUEST_RDX,
	VM_REG_GUEST_RSI,
	VM_REG_GUEST_RDI,
	VM_REG_GUEST_RBP,
	VM_REG_GUEST_RSP,
	VM_REG_GUEST_R8,
	VM_REG_GUEST_R9,
	VM_REG_GUEST_R10,
	VM_REG_GUEST_R11,
	VM_REG_GUEST_R12,
	VM_REG_GUEST_R13,
	VM_REG_GUEST_R14,
	VM_REG_GUEST_R15,
	VM_REG_GUEST_RIP,
	VM_REG_GUEST_RFLAGS,
	VM_REG_GUEST_CS,
	VM_REG_GUEST_SS,
	VM_REG_GUEST_DS,
	VM_REG_GUEST_ES,
	VM_REG_GUEST_FS,
	VM_REG_GUEST_GS
};

const int gdb_regsize[] = {
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	8,
	4,
	4,
	4,
	4,
	4,
	4,
	4
};

#ifdef GDB_LOG
#include <stdarg.h>
#include <stdio.h>

static void __printflike(1, 2)
debug(const char *fmt, ...)
{
	static FILE *logfile;
	va_list ap;

	if (logfile == NULL) {
		logfile = fopen("/tmp/bhyve_gdb.log", "w");
		if (logfile == NULL)
			return;
#ifndef WITHOUT_CAPSICUM
		if (caph_limit_stream(fileno(logfile), CAPH_WRITE) == -1) {
			fclose(logfile);
			logfile = NULL;
			return;
		}
#endif
		setlinebuf(logfile);
	}
	va_start(ap, fmt);
	vfprintf(logfile, fmt, ap);
	va_end(ap);
}
#else
#define debug(...)
#endif

static int
guest_paging_info(int vcpu, struct vm_guest_paging *paging)
{
	uint64_t regs[4];
	const int regset[4] = {
		VM_REG_GUEST_CR0,
		VM_REG_GUEST_CR3,
		VM_REG_GUEST_CR4,
		VM_REG_GUEST_EFER
	};

	if (vm_get_register_set(ctx, vcpu, nitems(regset), regset, regs) == -1)
		return (-1);

	/*
	 * For the debugger, always pretend to be the kernel (CPL 0),
	 * and if long-mode is enabled, always parse addresses as if
	 * in 64-bit mode.
	 */
	paging->cr3 = regs[1];
	paging->cpl = 0;
	if (regs[3] & EFER_LMA)
		paging->cpu_mode = CPU_MODE_64BIT;
	else if (regs[0] & CR0_PE)
		paging->cpu_mode = CPU_MODE_PROTECTED;
	else
		paging->cpu_mode = CPU_MODE_REAL;
	if (!(regs[0] & CR0_PG))
		paging->paging_mode = PAGING_MODE_FLAT;
	else if (!(regs[2] & CR4_PAE))
		paging->paging_mode = PAGING_MODE_32;
	else if (regs[3] & EFER_LME)
		paging->paging_mode = PAGING_MODE_64;
	else
		paging->paging_mode = PAGING_MODE_PAE;
	return (0);
}

/*
 * Map a guest virtual address to a physical address (for a given vcpu).
 * If a guest virtual address is valid, return 1.  If the address is
 * not valid, return 0.  If an error occurs obtaining the mapping,
 * return -1.
 */
static int
guest_vaddr2paddr(int vcpu, uint64_t vaddr, uint64_t *paddr)
{
	struct vm_guest_paging paging;
	int fault;

	if (guest_paging_info(vcpu, &paging) == -1)
		return (-1);

	/*
	 * Always use PROT_READ.  We really care if the VA is
	 * accessible, not if the current vCPU can write.
	 */
	if (vm_gla2gpa_nofault(ctx, vcpu, &paging, vaddr, PROT_READ, paddr,
	    &fault) == -1)
		return (-1);
	if (fault)
		return (0);
	return (1);
}

static void
io_buffer_reset(struct io_buffer *io)
{

	io->start = 0;
	io->len = 0;
}

/* Available room for adding data. */
static size_t
io_buffer_avail(struct io_buffer *io)
{

	return (io->capacity - (io->start + io->len));
}

static uint8_t *
io_buffer_head(struct io_buffer *io)
{

	return (io->data + io->start);
}

static uint8_t *
io_buffer_tail(struct io_buffer *io)
{

	return (io->data + io->start + io->len);
}

static void
io_buffer_advance(struct io_buffer *io, size_t amount)
{

	assert(amount <= io->len);
	io->start += amount;
	io->len -= amount;
}

static void
io_buffer_consume(struct io_buffer *io, size_t amount)
{

	io_buffer_advance(io, amount);
	if (io->len == 0) {
		io->start = 0;
		return;
	}

	/*
	 * XXX: Consider making this move optional and compacting on a
	 * future read() before realloc().
	 */
	memmove(io->data, io_buffer_head(io), io->len);
	io->start = 0;
}

static void
io_buffer_grow(struct io_buffer *io, size_t newsize)
{
	uint8_t *new_data;
	size_t avail, new_cap;

	avail = io_buffer_avail(io);
	if (newsize <= avail)
		return;

	new_cap = io->capacity + (newsize - avail);
	new_data = realloc(io->data, new_cap);
	if (new_data == NULL)
		err(1, "Failed to grow GDB I/O buffer");
	io->data = new_data;
	io->capacity = new_cap;
}

static bool
response_pending(void)
{

	if (cur_resp.start == 0 && cur_resp.len == 0)
		return (false);
	if (cur_resp.start + cur_resp.len == 1 && cur_resp.data[0] == '+')
		return (false);
	return (true);
}

static void
close_connection(void)
{

	/*
	 * XXX: This triggers a warning because mevent does the close
	 * before the EV_DELETE.
	 */
	pthread_mutex_lock(&gdb_lock);
	mevent_delete(write_event);
	mevent_delete_close(read_event);
	write_event = NULL;
	read_event = NULL;
	io_buffer_reset(&cur_comm);
	io_buffer_reset(&cur_resp);
	cur_fd = -1;

	/* Resume any stopped vCPUs. */
	gdb_resume_vcpus();
	pthread_mutex_unlock(&gdb_lock);
}

static uint8_t
hex_digit(uint8_t nibble)
{

	if (nibble <= 9)
		return (nibble + '0');
	else
		return (nibble + 'a' - 10);
}

static uint8_t
parse_digit(uint8_t v)
{

	if (v >= '0' && v <= '9')
		return (v - '0');
	if (v >= 'a' && v <= 'f')
		return (v - 'a' + 10);
	if (v >= 'A' && v <= 'F')
		return (v - 'A' + 10);
	return (0xF);
}

/* Parses big-endian hexadecimal. */
static uintmax_t
parse_integer(const uint8_t *p, size_t len)
{
	uintmax_t v;

	v = 0;
	while (len > 0) {
		v <<= 4;
		v |= parse_digit(*p);
		p++;
		len--;
	}
	return (v);
}

static uint8_t
parse_byte(const uint8_t *p)
{

	return (parse_digit(p[0]) << 4 | parse_digit(p[1]));
}

static void
send_pending_data(int fd)
{
	ssize_t nwritten;

	if (cur_resp.len == 0) {
		mevent_disable(write_event);
		return;
	}
	nwritten = write(fd, io_buffer_head(&cur_resp), cur_resp.len);
	if (nwritten == -1) {
		warn("Write to GDB socket failed");
		close_connection();
	} else {
		io_buffer_advance(&cur_resp, nwritten);
		if (cur_resp.len == 0)
			mevent_disable(write_event);
		else
			mevent_enable(write_event);
	}
}

/* Append a single character to the output buffer. */
static void
send_char(uint8_t data)
{
	io_buffer_grow(&cur_resp, 1);
	*io_buffer_tail(&cur_resp) = data;
	cur_resp.len++;
}

/* Append an array of bytes to the output buffer. */
static void
send_data(const uint8_t *data, size_t len)
{

	io_buffer_grow(&cur_resp, len);
	memcpy(io_buffer_tail(&cur_resp), data, len);
	cur_resp.len += len;
}

static void
format_byte(uint8_t v, uint8_t *buf)
{

	buf[0] = hex_digit(v >> 4);
	buf[1] = hex_digit(v & 0xf);
}

/*
 * Append a single byte (formatted as two hex characters) to the
 * output buffer.
 */
static void
send_byte(uint8_t v)
{
	uint8_t buf[2];

	format_byte(v, buf);
	send_data(buf, sizeof(buf));
}

static void
start_packet(void)
{

	send_char('$');
	cur_csum = 0;
}

static void
finish_packet(void)
{

	send_char('#');
	send_byte(cur_csum);
	debug("-> %.*s\n", (int)cur_resp.len, io_buffer_head(&cur_resp));
}

/*
 * Append a single character (for the packet payload) and update the
 * checksum.
 */
static void
append_char(uint8_t v)
{

	send_char(v);
	cur_csum += v;
}

/*
 * Append an array of bytes (for the packet payload) and update the
 * checksum.
 */
static void
append_packet_data(const uint8_t *data, size_t len)
{

	send_data(data, len);
	while (len > 0) {
		cur_csum += *data;
		data++;
		len--;
	}
}

static void
append_string(const char *str)
{

	append_packet_data(str, strlen(str));
}

static void
append_byte(uint8_t v)
{
	uint8_t buf[2];

	format_byte(v, buf);
	append_packet_data(buf, sizeof(buf));
}

static void
append_unsigned_native(uintmax_t value, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		append_byte(value);
		value >>= 8;
	}
}

static void
append_unsigned_be(uintmax_t value, size_t len)
{
	char buf[len * 2];
	size_t i;

	for (i = 0; i < len; i++) {
		format_byte(value, buf + (len - i - 1) * 2);
		value >>= 8;
	}
	append_packet_data(buf, sizeof(buf));
}

static void
append_integer(unsigned int value)
{

	if (value == 0)
		append_char('0');
	else
		append_unsigned_be(value, fls(value) + 7 / 8);
}

static void
append_asciihex(const char *str)
{

	while (*str != '\0') {
		append_byte(*str);
		str++;
	}
}

static void
send_empty_response(void)
{

	start_packet();
	finish_packet();
}

static void
send_error(int error)
{

	start_packet();
	append_char('E');
	append_byte(error);
	finish_packet();
}

static void
send_ok(void)
{

	start_packet();
	append_string("OK");
	finish_packet();
}

static int
parse_threadid(const uint8_t *data, size_t len)
{

	if (len == 1 && *data == '0')
		return (0);
	if (len == 2 && memcmp(data, "-1", 2) == 0)
		return (-1);
	if (len == 0)
		return (-2);
	return (parse_integer(data, len));
}

static void
report_stop(void)
{

	start_packet();
	if (stopped_vcpu == -1)
		append_char('S');
	else
		append_char('T');
	append_byte(GDB_SIGNAL_TRAP);
	if (stopped_vcpu != -1) {
		append_string("thread:");
		append_integer(stopped_vcpu + 1);
		append_char(';');
	}
	stopped_vcpu = -1;
	finish_packet();
}

static void
gdb_finish_suspend_vcpus(void)
{

	if (first_stop) {
		first_stop = false;
		stopped_vcpu = -1;
	} else if (response_pending())
		stop_pending = true;
	else {
		report_stop();
		send_pending_data(cur_fd);
	}
}

static void
_gdb_cpu_suspend(int vcpu, bool report_stop)
{

	debug("$vCPU %d suspending\n", vcpu);
	CPU_SET(vcpu, &vcpus_waiting);
	if (report_stop && CPU_CMP(&vcpus_waiting, &vcpus_suspended) == 0)
		gdb_finish_suspend_vcpus();
	while (CPU_ISSET(vcpu, &vcpus_suspended) && vcpu != stepping_vcpu)
		pthread_cond_wait(&idle_vcpus, &gdb_lock);
	CPU_CLR(vcpu, &vcpus_waiting);
	debug("$vCPU %d resuming\n", vcpu);
}

void
gdb_cpu_add(int vcpu)
{

	debug("$vCPU %d starting\n", vcpu);
	pthread_mutex_lock(&gdb_lock);
	CPU_SET(vcpu, &vcpus_active);

	/*
	 * If a vcpu is added while vcpus are stopped, suspend the new
	 * vcpu so that it will pop back out with a debug exit before
	 * executing the first instruction.
	 */
	if (!CPU_EMPTY(&vcpus_suspended)) {
		CPU_SET(vcpu, &vcpus_suspended);
		_gdb_cpu_suspend(vcpu, false);
	}
	pthread_mutex_unlock(&gdb_lock);
}

void
gdb_cpu_suspend(int vcpu)
{

	pthread_mutex_lock(&gdb_lock);
	_gdb_cpu_suspend(vcpu, true);
	pthread_mutex_unlock(&gdb_lock);
}

void
gdb_cpu_mtrap(int vcpu)
{

	debug("$vCPU %d MTRAP\n", vcpu);
	pthread_mutex_lock(&gdb_lock);
	if (vcpu == stepping_vcpu) {
		stepping_vcpu = -1;
		vm_set_capability(ctx, vcpu, VM_CAP_MTRAP_EXIT, 0);
		vm_suspend_cpu(ctx, vcpu);
		assert(stopped_vcpu == -1);
		stopped_vcpu = vcpu;
		_gdb_cpu_suspend(vcpu, true);
	}
	pthread_mutex_unlock(&gdb_lock);
}

static void
gdb_suspend_vcpus(void)
{

	assert(pthread_mutex_isowned_np(&gdb_lock));
	debug("suspending all CPUs\n");
	vcpus_suspended = vcpus_active;
	vm_suspend_cpu(ctx, -1);
	if (CPU_CMP(&vcpus_waiting, &vcpus_suspended) == 0)
		gdb_finish_suspend_vcpus();
}

static bool
gdb_step_vcpu(int vcpu)
{
	int error, val;

	debug("$vCPU %d step\n", vcpu);
	error = vm_get_capability(ctx, vcpu, VM_CAP_MTRAP_EXIT, &val);
	if (error < 0)
		return (false);
	error = vm_set_capability(ctx, vcpu, VM_CAP_MTRAP_EXIT, 1);
	vm_resume_cpu(ctx, vcpu);
	stepping_vcpu = vcpu;
	pthread_cond_broadcast(&idle_vcpus);
	return (true);
}

static void
gdb_resume_vcpus(void)
{

	assert(pthread_mutex_isowned_np(&gdb_lock));
	vm_resume_cpu(ctx, -1);
	debug("resuming all CPUs\n");
	CPU_ZERO(&vcpus_suspended);
	pthread_cond_broadcast(&idle_vcpus);
}

static void
gdb_read_regs(void)
{
	uint64_t regvals[nitems(gdb_regset)];
	int i;

	if (vm_get_register_set(ctx, cur_vcpu, nitems(gdb_regset),
	    gdb_regset, regvals) == -1) {
		send_error(errno);
		return;
	}
	start_packet();
	for (i = 0; i < nitems(regvals); i++)
		append_unsigned_native(regvals[i], gdb_regsize[i]);
	finish_packet();
}

static void
gdb_read_mem(const uint8_t *data, size_t len)
{
	uint64_t gpa, gva, val;
	uint8_t *cp;
	size_t resid, todo, bytes;
	bool started;
	int error;

	cp = memchr(data, ',', len);
	if (cp == NULL) {
		send_error(EINVAL);
		return;
	}
	gva = parse_integer(data + 1, cp - (data + 1));
	resid = parse_integer(cp + 1, len - (cp + 1 - data));
	started = false;

	while (resid > 0) {
		error = guest_vaddr2paddr(cur_vcpu, gva, &gpa);
		if (error == -1) {
			if (started)
				finish_packet();
			else
				send_error(errno);
			return;
		}
		if (error == 0) {
			if (started)
				finish_packet();
			else
				send_error(EFAULT);
			return;
		}

		/* Read bytes from current page. */
		todo = getpagesize() - gpa % getpagesize();
		if (todo > resid)
			todo = resid;

		cp = paddr_guest2host(ctx, gpa, todo);
		if (cp != NULL) {
			/*
			 * If this page is guest RAM, read it a byte
			 * at a time.
			 */
			if (!started) {
				start_packet();
				started = true;
			}
			while (todo > 0) {
				append_byte(*cp);
				cp++;
				gpa++;
				gva++;
				resid--;
				todo--;
			}
		} else {
			/*
			 * If this page isn't guest RAM, try to handle
			 * it via MMIO.  For MMIO requests, use
			 * aligned reads of words when possible.
			 */
			while (todo > 0) {
				if (gpa & 1 || todo == 1)
					bytes = 1;
				else if (gpa & 2 || todo == 2)
					bytes = 2;
				else
					bytes = 4;
				error = read_mem(ctx, cur_vcpu, gpa, &val,
				    bytes);
				if (error == 0) {
					if (!started) {
						start_packet();
						started = true;
					}
					gpa += bytes;
					gva += bytes;
					resid -= bytes;
					todo -= bytes;
					while (bytes > 0) {
						append_byte(val);
						val >>= 8;
						bytes--;
					}
				} else {
					if (started)
						finish_packet();
					else
						send_error(EFAULT);
					return;
				}
			}
		}
		assert(resid == 0 || gpa % getpagesize() == 0);
	}
	if (!started)
		start_packet();
	finish_packet();
}

static bool
command_equals(const uint8_t *data, size_t len, const char *cmd)
{

	if (strlen(cmd) > len)
		return (false);
	return (memcmp(data, cmd, strlen(cmd)) == 0);
}

static void
gdb_query(const uint8_t *data, size_t len)
{

	/*
	 * TODO:
	 * - qSearch
	 * - qSupported
	 */
	if (command_equals(data, len, "qAttached")) {
		start_packet();
		append_char('1');
		finish_packet();
	} else if (command_equals(data, len, "qC")) {
		start_packet();
		append_string("QC");
		append_integer(cur_vcpu + 1);
		finish_packet();
	} else if (command_equals(data, len, "qfThreadInfo")) {
		cpuset_t mask;
		bool first;
		int vcpu;

		if (CPU_EMPTY(&vcpus_active)) {
			send_error(EINVAL);
			return;
		}
		mask = vcpus_active;
		start_packet();
		append_char('m');
		first = true;
		while (!CPU_EMPTY(&mask)) {
			vcpu = CPU_FFS(&mask) - 1;
			CPU_CLR(vcpu, &mask);
			if (first)
				first = false;
			else
				append_char(',');
			append_integer(vcpu + 1);
		}
		finish_packet();
	} else if (command_equals(data, len, "qsThreadInfo")) {
		start_packet();
		append_char('l');
		finish_packet();
	} else if (command_equals(data, len, "qThreadExtraInfo")) {
		char buf[16];
		int tid;

		data += strlen("qThreadExtraInfo");
		len -= strlen("qThreadExtraInfo");
		if (*data != ',') {
			send_error(EINVAL);
			return;
		}
		tid = parse_threadid(data + 1, len - 1);
		if (tid <= 0 || !CPU_ISSET(tid - 1, &vcpus_active)) {
			send_error(EINVAL);
			return;
		}

		snprintf(buf, sizeof(buf), "vCPU %d", tid - 1);
		start_packet();
		append_asciihex(buf);
		finish_packet();
	} else
		send_empty_response();
}

static void
handle_command(const uint8_t *data, size_t len)
{

	/* Reject packets with a sequence-id. */
	if (len >= 3 && data[0] >= '0' && data[0] <= '9' &&
	    data[0] >= '0' && data[0] <= '9' && data[2] == ':') {
		send_empty_response();
		return;
	}

	switch (*data) {
	case 'c':
		if (len != 1) {
			send_error(EINVAL);
			break;
		}

		/* Don't send a reply until a stop occurs. */
		gdb_resume_vcpus();
		break;
	case 'D':
		send_ok();

		/* TODO: Resume any stopped CPUs. */
		break;
	case 'g': {
		gdb_read_regs();
		break;
	}
	case 'H': {
		int tid;

		if (data[1] != 'g' && data[1] != 'c') {
			send_error(EINVAL);
			break;
		}
		tid = parse_threadid(data + 2, len - 2);
		if (tid == -2) {
			send_error(EINVAL);
			break;
		}

		if (CPU_EMPTY(&vcpus_active)) {
			send_error(EINVAL);
			break;
		}
		if (tid == -1 || tid == 0)
			cur_vcpu = CPU_FFS(&vcpus_active) - 1;
		else if (CPU_ISSET(tid - 1, &vcpus_active))
			cur_vcpu = tid - 1;
		else {
			send_error(EINVAL);
			break;
		}
		send_ok();
		break;
	}
	case 'm':
		gdb_read_mem(data, len);
		break;
	case 'T': {
		int tid;

		tid = parse_threadid(data + 1, len - 1);
		if (tid <= 0 || !CPU_ISSET(tid - 1, &vcpus_active)) {
			send_error(EINVAL);
			return;
		}
		send_ok();
		break;
	}
	case 'q':
		gdb_query(data, len);
		break;
	case 's':
		if (len != 1) {
			send_error(EINVAL);
			break;
		}

		/* Don't send a reply until a stop occurs. */
		if (!gdb_step_vcpu(cur_vcpu)) {
			send_error(EOPNOTSUPP);
			break;
		}
		break;
	case '?':
		/* XXX: Only if stopped? */
		/* For now, just report that we are always stopped. */
		start_packet();
		append_char('S');
		append_byte(GDB_SIGNAL_TRAP);
		finish_packet();
		break;
	case 'G': /* TODO */
	case 'M': /* TODO */
	case 'v':
		/* Handle 'vCont' */
		/* 'vCtrlC' */
	case 'p': /* TODO */
	case 'P': /* TODO */
	case 'Q': /* TODO */
	case 't': /* TODO */
	case 'X': /* TODO */
	case 'z': /* TODO */
	case 'Z': /* TODO */
	default:
		send_empty_response();
	}
}

/* Check for a valid packet in the command buffer. */
static void
check_command(int fd)
{
	uint8_t *head, *hash, *p, sum;
	size_t avail, plen;

	for (;;) {
		avail = cur_comm.len;
		if (avail == 0)
			return;
		head = io_buffer_head(&cur_comm);
		switch (*head) {
		case 0x03:
			debug("<- Ctrl-C\n");
			io_buffer_consume(&cur_comm, 1);

			gdb_suspend_vcpus();
			break;
		case '+':
			/* ACK of previous response. */
			debug("<- +\n");
			if (response_pending())
				io_buffer_reset(&cur_resp);
			io_buffer_consume(&cur_comm, 1);
			if (stop_pending) {
				stop_pending = false;
				report_stop();
				send_pending_data(fd);
			}
			break;
		case '-':
			/* NACK of previous response. */
			debug("<- -\n");
			if (response_pending()) {
				cur_resp.len += cur_resp.start;
				cur_resp.start = 0;
				if (cur_resp.data[0] == '+')
					io_buffer_advance(&cur_resp, 1);
				debug("-> %.*s\n", (int)cur_resp.len,
				    io_buffer_head(&cur_resp));
			}
			io_buffer_consume(&cur_comm, 1);
			send_pending_data(fd);
			break;
		case '$':
			/* Packet. */

			if (response_pending()) {
				warnx("New GDB command while response in "
				    "progress");
				io_buffer_reset(&cur_resp);
			}

			/* Is packet complete? */
			hash = memchr(head, '#', avail);
			if (hash == NULL)
				return;
			plen = (hash - head + 1) + 2;
			if (avail < plen)
				return;
			debug("<- %.*s\n", (int)plen, head);

			/* Verify checksum. */
			for (sum = 0, p = head + 1; p < hash; p++)
				sum += *p;
			if (sum != parse_byte(hash + 1)) {
				io_buffer_consume(&cur_comm, plen);
				debug("-> -\n");
				send_char('-');
				send_pending_data(fd);
				break;
			}
			send_char('+');

			handle_command(head + 1, hash - (head + 1));
			io_buffer_consume(&cur_comm, plen);
			if (!response_pending())
				debug("-> +\n");
			send_pending_data(fd);
			break;
		default:
			/* XXX: Possibly drop connection instead. */
			debug("-> %02x\n", *head);
			io_buffer_consume(&cur_comm, 1);
			break;
		}
	}
}

static void
gdb_readable(int fd, enum ev_type event, void *arg)
{
	ssize_t nread;
	int pending;

	if (ioctl(fd, FIONREAD, &pending) == -1) {
		warn("FIONREAD on GDB socket");
		return;
	}

	/*
	 * 'pending' might be zero due to EOF.  We need to call read
	 * with a non-zero length to detect EOF.
	 */
	if (pending == 0)
		pending = 1;

	/* Ensure there is room in the command buffer. */
	io_buffer_grow(&cur_comm, pending);
	assert(io_buffer_avail(&cur_comm) >= pending);

	nread = read(fd, io_buffer_tail(&cur_comm), io_buffer_avail(&cur_comm));
	if (nread == 0) {
		close_connection();
	} else if (nread == -1) {
		if (errno == EAGAIN)
			return;

		warn("Read from GDB socket");
		close_connection();
	} else {
		cur_comm.len += nread;
		pthread_mutex_lock(&gdb_lock);
		check_command(fd);
		pthread_mutex_unlock(&gdb_lock);
	}
}

static void
gdb_writable(int fd, enum ev_type event, void *arg)
{

	send_pending_data(fd);
}

static void
new_connection(int fd, enum ev_type event, void *arg)
{
	int optval, s;

	s = accept4(fd, NULL, NULL, SOCK_NONBLOCK);
	if (s == -1) {
		if (arg != NULL)
			err(1, "Failed accepting initial GDB connection");

		/* Silently ignore errors post-startup. */
		return;
	}

	optval = 1;
	if (setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &optval, sizeof(optval)) ==
	    -1) {
		warn("Failed to disable SIGPIPE for GDB connection");
		close(s);
		return;
	}

	pthread_mutex_lock(&gdb_lock);
	if (cur_fd != -1) {
		close(s);
		warnx("Ignoring additional GDB connection.");
	}

	read_event = mevent_add(s, EVF_READ, gdb_readable, NULL);
	if (read_event == NULL) {
		if (arg != NULL)
			err(1, "Failed to setup initial GDB connection");
		pthread_mutex_unlock(&gdb_lock);
		return;
	}
	write_event = mevent_add(s, EVF_WRITE, gdb_writable, NULL);
	if (write_event == NULL) {
		if (arg != NULL)
			err(1, "Failed to setup initial GDB connection");
		mevent_delete_close(read_event);
		read_event = NULL;
	}

	cur_fd = s;
	cur_vcpu = 0;
	stepping_vcpu = -1;
	stopped_vcpu = -1;
	stop_pending = false;

	/* Break on attach. */
	first_stop = true;
	gdb_suspend_vcpus();
	pthread_mutex_unlock(&gdb_lock);
}

#ifndef WITHOUT_CAPSICUM
void
limit_gdb_socket(int s)
{
	cap_rights_t rights;
	unsigned long ioctls[] = { FIONREAD };

	cap_rights_init(&rights, CAP_ACCEPT, CAP_EVENT, CAP_READ, CAP_WRITE,
	    CAP_SETSOCKOPT, CAP_IOCTL);
	if (caph_rights_limit(s, &rights) == -1)
		errx(EX_OSERR, "Unable to apply rights for sandbox");
	if (caph_ioctls_limit(s, ioctls, nitems(ioctls)) == -1)
		errx(EX_OSERR, "Unable to apply rights for sandbox");
}
#endif

void
init_gdb(struct vmctx *_ctx, int sport, bool wait)
{
	struct sockaddr_in sin;
	int error, flags, s;

	debug("==> starting on %d, %swaiting\n", sport, wait ? "" : "not ");

	error = pthread_mutex_init(&gdb_lock, NULL);
	if (error != 0)
		errc(1, error, "gdb mutex init");
	error = pthread_cond_init(&idle_vcpus, NULL);
	if (error != 0)
		errc(1, error, "gdb cv init");

	ctx = _ctx;
	s = socket(PF_INET, SOCK_STREAM, 0);
	if (s < 0)
		err(1, "gdb socket create");

	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons(sport);

	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		err(1, "gdb socket bind");

	if (listen(s, 1) < 0)
		err(1, "gdb socket listen");

	if (wait) {
		/*
		 * Set vcpu 0 in vcpus_suspended.  This will trigger the
		 * logic in gdb_cpu_add() to suspend the first vcpu before
		 * it starts execution.  The vcpu will remain suspended
		 * until a debugger connects.
		 */
		stepping_vcpu = -1;
		stopped_vcpu = -1;
		CPU_SET(0, &vcpus_suspended);
	}

	flags = fcntl(s, F_GETFL);
	if (fcntl(s, F_SETFL, flags | O_NONBLOCK) == -1)
		err(1, "Failed to mark gdb socket non-blocking");

#ifndef WITHOUT_CAPSICUM
	limit_gdb_socket(s);
#endif
	mevent_add(s, EVF_READ, new_connection, NULL);
}
