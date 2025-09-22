/*	$OpenBSD: rtkit.c,v 1.19 2025/08/01 09:51:52 jsg Exp $	*/
/*
 * Copyright (c) 2021 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/task.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <uvm/uvm_extern.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#include <arm64/dev/aplmbox.h>
#include <arm64/dev/rtkit.h>

#define RTKIT_EP_MGMT			0
#define RTKIT_EP_CRASHLOG		1
#define RTKIT_EP_SYSLOG			2
#define RTKIT_EP_DEBUG			3
#define RTKIT_EP_IOREPORT		4
#define RTKIT_EP_OSLOG			8
#define RTKIT_EP_UNKNOWN		10

#define RTKIT_MGMT_TYPE(x)		(((x) >> 52) & 0xff)
#define RTKIT_MGMT_TYPE_SHIFT		52

#define RTKIT_MGMT_PWR_STATE(x)		(((x) >> 0) & 0xffff)

#define RTKIT_MGMT_HELLO		1
#define RTKIT_MGMT_HELLO_ACK		2
#define RTKIT_MGMT_STARTEP		5
#define RTKIT_MGMT_IOP_PWR_STATE	6
#define RTKIT_MGMT_IOP_PWR_STATE_ACK	7
#define RTKIT_MGMT_EPMAP		8
#define RTKIT_MGMT_AP_PWR_STATE		11

#define RTKIT_MGMT_HELLO_MINVER(x)	(((x) >> 0) & 0xffff)
#define RTKIT_MGMT_HELLO_MINVER_SHIFT	0
#define RTKIT_MGMT_HELLO_MAXVER(x)	(((x) >> 16) & 0xffff)
#define RTKIT_MGMT_HELLO_MAXVER_SHIFT	16

#define RTKIT_MGMT_STARTEP_EP_SHIFT	32
#define RTKIT_MGMT_STARTEP_START	(1ULL << 1)

#define RTKIT_MGMT_EPMAP_LAST		(1ULL << 51)
#define RTKIT_MGMT_EPMAP_BASE(x)	(((x) >> 32) & 0x7)
#define RTKIT_MGMT_EPMAP_BASE_SHIFT	32
#define RTKIT_MGMT_EPMAP_BITMAP(x)	(((x) >> 0) & 0xffffffff)
#define RTKIT_MGMT_EPMAP_MORE		(1ULL << 0)

#define RTKIT_BUFFER_REQUEST		1
#define RTKIT_BUFFER_ADDR(x)		(((x) >> 0) & 0xfffffffffff)
#define RTKIT_BUFFER_SIZE(x)		(((x) >> 44) & 0xff)
#define RTKIT_BUFFER_SIZE_SHIFT		44

#define RTKIT_SYSLOG_LOG		5
#define RTKIT_SYSLOG_LOG_IDX(x)		(((x) >> 0) & 0xff)
#define RTKIT_SYSLOG_INIT		8
#define RTKIT_SYSLOG_INIT_N_ENTRIES(x)	(((x) >> 0) & 0xff)
#define RTKIT_SYSLOG_INIT_MSG_SIZE(x)	(((x) >> 24) & 0xff)

#define RTKIT_IOREPORT_UNKNOWN1		8
#define RTKIT_IOREPORT_UNKNOWN2		12

#define RTKIT_OSLOG_TYPE(x)		(((x) >> 56) & 0xff)
#define RTKIT_OSLOG_TYPE_SHIFT		(56 - RTKIT_MGMT_TYPE_SHIFT)
#define RTKIT_OSLOG_BUFFER_REQUEST	1
#define RTKIT_OSLOG_BUFFER_ADDR(x)	(((x) >> 0) & 0xfffffffff)
#define RTKIT_OSLOG_BUFFER_SIZE(x)	(((x) >> 36) & 0xfffff)
#define RTKIT_OSLOG_BUFFER_SIZE_SHIFT	36
#define RTKIT_OSLOG_UNKNOWN1		3
#define RTKIT_OSLOG_UNKNOWN2		4
#define RTKIT_OSLOG_UNKNOWN3		5

/* Versions we support. */
#define RTKIT_MINVER			11
#define RTKIT_MAXVER			12

struct rtkit_dmamem {
	bus_dmamap_t		rdm_map;
	bus_dma_segment_t	rdm_seg;
	size_t			rdm_size;
	caddr_t			rdm_kva;
};

struct rtkit_state {
	struct mbox_channel	*mc;
	struct rtkit		*rk;
	int			flags;
	char			*crashlog;
	bus_addr_t		crashlog_addr;
	bus_size_t		crashlog_size;
	struct task		crashlog_task;
	char			*ioreport;
	bus_addr_t		ioreport_addr;
	bus_size_t		ioreport_size;
	struct task		ioreport_task;
	char			*oslog;
	bus_addr_t		oslog_addr;
	bus_size_t		oslog_size;
	struct task		oslog_task;
	char			*syslog;
	bus_addr_t		syslog_addr;
	bus_size_t		syslog_size;
	struct task		syslog_task;
	uint8_t			syslog_n_entries;
	uint8_t			syslog_msg_size;
	char			*syslog_msg;
	uint16_t		iop_pwrstate;
	uint16_t		ap_pwrstate;
	uint64_t		epmap;
	void			(*callback[32])(void *, uint64_t);
	void			*arg[32];
	struct rtkit_dmamem	dmamem[32];
	int			ndmamem;
};

int
rtkit_recv(struct mbox_channel *mc, struct aplmbox_msg *msg)
{
	return mbox_recv(mc, msg, sizeof(*msg));
}

int
rtkit_send(struct rtkit_state *state, uint32_t endpoint,
    uint64_t type, uint64_t data)
{
	struct aplmbox_msg msg;

	msg.data0 = (type << RTKIT_MGMT_TYPE_SHIFT) | data;
	msg.data1 = endpoint;

	if (state->flags & RK_DEBUG) {
		printf("%s: 0x%016llx 0x%02x\n", __func__,
		    msg.data0, msg.data1);
	}

	return mbox_send(state->mc, &msg, sizeof(msg));
}

bus_addr_t
rtkit_alloc(struct rtkit_state *state, bus_size_t size, caddr_t *kvap)
{
	struct rtkit *rk = state->rk;
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	caddr_t kva;
	int nsegs;

	if (state->ndmamem >= nitems(state->dmamem))
		return (bus_addr_t)-1;

	if (bus_dmamem_alloc(rk->rk_dmat, size, 16384, 0,
	    &seg, 1, &nsegs, BUS_DMA_WAITOK | BUS_DMA_ZERO))
		return (bus_addr_t)-1;

	if (bus_dmamem_map(rk->rk_dmat, &seg, 1, size,
	    &kva, BUS_DMA_WAITOK | BUS_DMA_COHERENT)) {
		bus_dmamem_free(rk->rk_dmat, &seg, 1);
		return (bus_addr_t)-1;
	}

	if (bus_dmamap_create(rk->rk_dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK, &map)) {
		bus_dmamem_unmap(rk->rk_dmat, kva, size);
		bus_dmamem_free(rk->rk_dmat, &seg, 1);
		return (bus_addr_t)-1;
	}

	if (bus_dmamap_load_raw(rk->rk_dmat, map, &seg, 1, size,
	    BUS_DMA_WAITOK)) {
		bus_dmamap_destroy(rk->rk_dmat, map);
		bus_dmamem_unmap(rk->rk_dmat, kva, size);
		bus_dmamem_free(rk->rk_dmat, &seg, 1);
		return (bus_addr_t)-1;
	}

	if (rk->rk_map) {
		if (rk->rk_map(rk->rk_cookie, seg.ds_addr, seg.ds_len)) {
			bus_dmamap_unload(rk->rk_dmat, map);
			bus_dmamap_destroy(rk->rk_dmat, map);
			bus_dmamem_unmap(rk->rk_dmat, kva, size);
			bus_dmamem_free(rk->rk_dmat, &seg, 1);
			return (bus_addr_t)-1;
		}
	}

	state->dmamem[state->ndmamem].rdm_map = map;
	state->dmamem[state->ndmamem].rdm_seg = seg;
	state->dmamem[state->ndmamem].rdm_size = size;
	state->dmamem[state->ndmamem].rdm_kva = kva;
	state->ndmamem++;

	*kvap = kva;
	return map->dm_segs[0].ds_addr;
}

int
rtkit_start(struct rtkit_state *state, uint32_t endpoint)
{
	uint64_t reply;

	reply = ((uint64_t)endpoint << RTKIT_MGMT_STARTEP_EP_SHIFT);
	reply |= RTKIT_MGMT_STARTEP_START;
	return rtkit_send(state, RTKIT_EP_MGMT, RTKIT_MGMT_STARTEP, reply);
}

int
rtkit_handle_mgmt(struct rtkit_state *state, struct aplmbox_msg *msg)
{
	uint64_t minver, maxver, ver;
	uint64_t base, bitmap, reply;
	uint32_t endpoint;
	int error;

	switch (RTKIT_MGMT_TYPE(msg->data0)) {
	case RTKIT_MGMT_HELLO:
		minver = RTKIT_MGMT_HELLO_MINVER(msg->data0);
		maxver = RTKIT_MGMT_HELLO_MAXVER(msg->data0);
		if (minver > RTKIT_MAXVER) {
			printf("%s: unsupported minimum firmware version %lld\n",
			    __func__, minver);
			return EINVAL;
		}
		if (maxver < RTKIT_MINVER) {
			printf("%s: unsupported maximum firmware version %lld\n",
			    __func__, maxver);
			return EINVAL;
		}
		ver = min(RTKIT_MAXVER, maxver);
		error = rtkit_send(state, RTKIT_EP_MGMT, RTKIT_MGMT_HELLO_ACK,
		    (ver << RTKIT_MGMT_HELLO_MINVER_SHIFT) |
		    (ver << RTKIT_MGMT_HELLO_MAXVER_SHIFT));
		if (error)
			return error;
		break;
	case RTKIT_MGMT_IOP_PWR_STATE_ACK:
		state->iop_pwrstate = RTKIT_MGMT_PWR_STATE(msg->data0);
		wakeup(&state->iop_pwrstate);
		break;
	case RTKIT_MGMT_AP_PWR_STATE:
		state->ap_pwrstate = RTKIT_MGMT_PWR_STATE(msg->data0);
		wakeup(&state->ap_pwrstate);
		break;
	case RTKIT_MGMT_EPMAP:
		base = RTKIT_MGMT_EPMAP_BASE(msg->data0);
		bitmap = RTKIT_MGMT_EPMAP_BITMAP(msg->data0);
		state->epmap |= (bitmap << (base * 32));
		reply = (base << RTKIT_MGMT_EPMAP_BASE_SHIFT);
		if (msg->data0 & RTKIT_MGMT_EPMAP_LAST)
			reply |= RTKIT_MGMT_EPMAP_LAST;
		else
			reply |= RTKIT_MGMT_EPMAP_MORE;
		error = rtkit_send(state, RTKIT_EP_MGMT,
		    RTKIT_MGMT_EPMAP, reply);
		if (error)
			return error;
		if (msg->data0 & RTKIT_MGMT_EPMAP_LAST) {
			for (endpoint = 1; endpoint < 32; endpoint++) {
				if ((state->epmap & (1ULL << endpoint)) == 0)
					continue;

				switch (endpoint) {
				case RTKIT_EP_CRASHLOG:
				case RTKIT_EP_SYSLOG:
				case RTKIT_EP_DEBUG:
				case RTKIT_EP_IOREPORT:
				case RTKIT_EP_OSLOG:
					error = rtkit_start(state, endpoint);
					if (error)
						return error;
					break;
				case RTKIT_EP_UNKNOWN:
					break;
				default:
					printf("%s: skipping endpoint %d\n",
					    __func__, endpoint);
					break;
				}
			}
		}
		break;
	default:
		printf("%s: unhandled management event 0x%016lld\n",
		    __func__, msg->data0);
		break;
	}

	return 0;
}

struct rtkit_crashlog_header {
	uint32_t	fourcc;
	uint32_t	version;
	uint32_t	size;
	uint32_t	flags;
	uint8_t		unknown[16];
};

struct rtkit_crashlog_mbx {
	uint64_t	msg1;
	uint64_t	msg0;
	uint32_t	timestamp;
	uint8_t		unknown[4];
};

struct rtkit_crashlog_rg8 {
	uint64_t	unknown0;
	uint64_t	reg[31];
	uint64_t	sp;
	uint64_t	pc;
	uint64_t	psr;
	uint64_t	cpacr;
	uint64_t	fpsr;
	uint64_t	fpcr;
	uint64_t	fpreg[64];
	uint64_t	far;
	uint64_t	unknown1;
	uint64_t	esr;
	uint64_t	unknown2;
};

#define RTKIT_FOURCC(s)	((s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3])

void
rtkit_crashlog_dump_str(char *buf, size_t size)
{
	char *end = buf + size - 1;
	char *newl;
	uint32_t idx;

	if (size < 5)
		return;

	idx = lemtoh32((uint32_t *)buf);
	buf += sizeof(uint32_t);

	*end = 0;
	while (buf < end) {
		if (*buf == 0)
			return;
		newl = memchr(buf, '\n', buf - end);
		if (newl)
			*newl = 0;
		printf("RTKit Cstr %x: %s\n", idx, buf);
		if (!newl)
			return;
		buf = newl + 1;
	}
}

void
rtkit_crashlog_dump_ver(char *buf, size_t size)
{
	char *end = buf + size - 1;

	if (size < 17)
		return;

	buf += 16;

	*end = 0;
	printf("RTKit Cver %s\n", buf);
}

void
rtkit_crashlog_dump_mbx(char *buf, size_t size)
{
	struct rtkit_crashlog_mbx mbx;
	char *end = buf + size;

	buf += 28;
	size -= 28;

	while (buf + sizeof(mbx) <= end) {
		memcpy(&mbx, buf, sizeof(mbx));
		printf("RTKit Cmbx: 0x%016llx 0x%016llx @0x%08x\n",
		    mbx.msg0, mbx.msg1, mbx.timestamp);
		buf += sizeof(mbx);
	}
}

void
rtkit_crashlog_dump_rg8(char *buf, size_t size)
{
	struct rtkit_crashlog_rg8 rg8;
	int i;

	if (size < sizeof(rg8))
		return;

	memcpy(&rg8, buf, sizeof(rg8));
	printf("RTKit Crg8: psr %016llx\n", rg8.psr);
	printf("RTKit Crg8: pc  %016llx\n", rg8.pc);
	printf("RTKit Crg8: esr %016llx\n", rg8.esr);
	printf("RTKit Crg8: far %016llx\n", rg8.far);
	printf("RTKit Crg8: sp  %016llx\n", rg8.sp);
	for (i = 0; i < nitems(rg8.reg); i++)
		printf("RTKit Crg8: reg[%d] %016llx\n", i, rg8.reg[i]);
}

void
rtkit_crashlog_dump(char *buf, size_t size)
{
	struct rtkit_crashlog_header hdr;
	size_t off;

	if (size < sizeof(hdr))
		return;

	memcpy(&hdr, buf, sizeof(hdr));
	if (letoh32(hdr.fourcc) != RTKIT_FOURCC("CLHE")) {
		printf("RTKit: Invalid header\n");
		return;
	}

	if (letoh32(hdr.size) > size) {
		printf("RTKit: Invalid header size\n");
		return;
	}

	off = sizeof(hdr);
	while (off < letoh32(hdr.size)) {
		uint32_t fourcc, size;

		fourcc = lemtoh32((uint32_t *)(buf + off));
		size = lemtoh32((uint32_t *)(buf + off + 12));
		if (fourcc == RTKIT_FOURCC("CLHE"))
			break;
		if (fourcc == RTKIT_FOURCC("Cstr"))
			rtkit_crashlog_dump_str(buf + off + 16, size - 16);
		if (fourcc == RTKIT_FOURCC("Cver"))
			rtkit_crashlog_dump_ver(buf + off + 16, size - 16);
		if (fourcc == RTKIT_FOURCC("Cmbx"))
			rtkit_crashlog_dump_mbx(buf + off + 16, size - 16);
		if (fourcc == RTKIT_FOURCC("Crg8"))
			rtkit_crashlog_dump_rg8(buf + off + 16, size - 16);
		off += size;
	}
}

void
rtkit_handle_crashlog_buffer(void *arg)
{
	struct rtkit_state *state = arg;
	struct rtkit *rk = state->rk;
	bus_addr_t addr = state->crashlog_addr;
	bus_size_t size = state->crashlog_size;

	if (addr) {
		paddr_t pa = addr | PMAP_NOCACHE;
		vaddr_t va;

		if (rk && rk->rk_logmap) {
			pa = rk->rk_logmap(rk->rk_cookie, addr);
			if (pa == (paddr_t)-1)
				return;
		}

		state->crashlog = km_alloc(size * PAGE_SIZE,
		    &kv_any, &kp_none, &kd_waitok);
		va = (vaddr_t)state->crashlog;

		while (size-- > 0) {
			pmap_kenter_pa(va, pa, PROT_READ);
			va += PAGE_SIZE;
			pa += PAGE_SIZE;
		}
		return;
	}

	if (rk) {
		addr = rtkit_alloc(state, size << PAGE_SHIFT,
		    &state->crashlog);
		if (addr == (bus_addr_t)-1)
			return;
	}

	rtkit_send(state, RTKIT_EP_CRASHLOG, RTKIT_BUFFER_REQUEST,
	    (size << RTKIT_BUFFER_SIZE_SHIFT) | addr);
}

int
rtkit_handle_crashlog(struct rtkit_state *state, struct aplmbox_msg *msg)
{
	bus_addr_t addr;
	bus_size_t size;

	switch (RTKIT_MGMT_TYPE(msg->data0)) {
	case RTKIT_BUFFER_REQUEST:
		addr = RTKIT_BUFFER_ADDR(msg->data0);
		size = RTKIT_BUFFER_SIZE(msg->data0);

		if (state->crashlog) {
			char *buf;

			printf("\nRTKit crashed:\n");

			buf = malloc(size * PAGE_SIZE, M_TEMP, M_NOWAIT);
			if (buf) {
				memcpy(buf, state->crashlog, size * PAGE_SIZE);
				rtkit_crashlog_dump(buf, size * PAGE_SIZE);
				free(buf, M_TEMP, size * PAGE_SIZE);
			}
			break;
		}

		state->crashlog_addr = addr;
		state->crashlog_size = size;
		if (cold)
			rtkit_handle_crashlog_buffer(state);
		else
			task_add(systq, &state->crashlog_task);
		break;
	default:
		printf("%s: unhandled crashlog event 0x%016llx\n",
		    __func__, msg->data0);
		break;
	}

	return 0;
}

void
rtkit_handle_syslog_log(struct rtkit_state *state, struct aplmbox_msg *msg)
{
	char context[24];
	size_t syslog_msg_size;
	char *syslog_msg;
	int idx, pos;

	if ((state->flags & RK_SYSLOG) == 0)
		return;

	if (state->syslog_msg == NULL)
		return;
	idx = RTKIT_SYSLOG_LOG_IDX(msg->data0);
	if (idx > state->syslog_n_entries)
		return;

	syslog_msg_size = state->syslog_msg_size + 32;
	syslog_msg = state->syslog + (idx * syslog_msg_size + 8);
	memcpy(context, syslog_msg, sizeof(context));
	context[sizeof(context) - 1] = 0;

	syslog_msg += sizeof(context);
	memcpy(state->syslog_msg, syslog_msg, state->syslog_msg_size);
	state->syslog_msg[state->syslog_msg_size - 1] = 0;

	pos = strlen(state->syslog_msg) - 1;
	while (pos >= 0) {
		if (state->syslog_msg[pos] != ' ' &&
		    state->syslog_msg[pos] != '\n' &&
		    state->syslog_msg[pos] != '\r')
			break;
		state->syslog_msg[pos--] = 0;
	}

	printf("RTKit syslog %d: %s:%s\n", idx, context, state->syslog_msg);
}

void
rtkit_handle_syslog_buffer(void *arg)
{
	struct rtkit_state *state = arg;
	struct rtkit *rk = state->rk;
	bus_addr_t addr = state->syslog_addr;
	bus_size_t size = state->syslog_size;

	if (rk) {
		addr = rtkit_alloc(state, size << PAGE_SHIFT,
		    &state->syslog);
		if (addr == (bus_addr_t)-1)
			return;
	}

	rtkit_send(state, RTKIT_EP_SYSLOG, RTKIT_BUFFER_REQUEST,
	    (size << RTKIT_BUFFER_SIZE_SHIFT) | addr);
}

int
rtkit_handle_syslog(struct rtkit_state *state, struct aplmbox_msg *msg)
{
	bus_addr_t addr;
	bus_size_t size;
	int error;

	switch (RTKIT_MGMT_TYPE(msg->data0)) {
	case RTKIT_BUFFER_REQUEST:
		addr = RTKIT_BUFFER_ADDR(msg->data0);
		size = RTKIT_BUFFER_SIZE(msg->data0);
		if (addr)
			break;

		state->syslog_addr = addr;
		state->syslog_size = size;
		if (cold)
			rtkit_handle_syslog_buffer(state);
		else
			task_add(systq, &state->syslog_task);
		break;
	case RTKIT_SYSLOG_INIT:
		state->syslog_n_entries =
		    RTKIT_SYSLOG_INIT_N_ENTRIES(msg->data0);
		state->syslog_msg_size =
		    RTKIT_SYSLOG_INIT_MSG_SIZE(msg->data0);
		state->syslog_msg = malloc(state->syslog_msg_size,
		    M_DEVBUF, M_NOWAIT);
		break;
	case RTKIT_SYSLOG_LOG:
		rtkit_handle_syslog_log(state, msg);
		error = rtkit_send(state, RTKIT_EP_SYSLOG,
		    RTKIT_MGMT_TYPE(msg->data0), msg->data0);
		if (error)
			return error;
		break;
	default:
		printf("%s: unhandled syslog event 0x%016llx\n",
		    __func__, msg->data0);
		break;
	}

	return 0;
}

void
rtkit_handle_ioreport_buffer(void *arg)
{
	struct rtkit_state *state = arg;
	struct rtkit *rk = state->rk;
	bus_addr_t addr = state->ioreport_addr;
	bus_size_t size = state->ioreport_size;

	if (rk) {
		addr = rtkit_alloc(state, size << PAGE_SHIFT,
		    &state->ioreport);
		if (addr == (bus_addr_t)-1)
			return;
	}

	rtkit_send(state, RTKIT_EP_IOREPORT, RTKIT_BUFFER_REQUEST,
	    (size << RTKIT_BUFFER_SIZE_SHIFT) | addr);
}

int
rtkit_handle_ioreport(struct rtkit_state *state, struct aplmbox_msg *msg)
{
	bus_addr_t addr;
	bus_size_t size;
	int error;

	switch (RTKIT_MGMT_TYPE(msg->data0)) {
	case RTKIT_BUFFER_REQUEST:
		addr = RTKIT_BUFFER_ADDR(msg->data0);
		size = RTKIT_BUFFER_SIZE(msg->data0);
		if (addr)
			break;

		state->ioreport_addr = addr;
		state->ioreport_size = size;
		if (cold)
			rtkit_handle_ioreport_buffer(state);
		else
			task_add(systq, &state->ioreport_task);
		break;
	case RTKIT_IOREPORT_UNKNOWN1:
	case RTKIT_IOREPORT_UNKNOWN2:
		/* These unknown events have to be acked to make progress. */
		error = rtkit_send(state, RTKIT_EP_IOREPORT,
		    RTKIT_MGMT_TYPE(msg->data0), msg->data0);
		if (error)
			return error;
		break;
	default:
		printf("%s: unhandled ioreport event 0x%016llx\n",
		    __func__, msg->data0);
		break;
	}

	return 0;
}

void
rtkit_handle_oslog_buffer(void *arg)
{
	struct rtkit_state *state = arg;
	struct rtkit *rk = state->rk;
	bus_addr_t addr = state->oslog_addr;
	bus_size_t size = state->oslog_size;

	if (rk) {
		addr = rtkit_alloc(state, size, &state->oslog);
		if (addr == (bus_addr_t)-1)
			return;
	}

	rtkit_send(state, RTKIT_EP_OSLOG,
	    (RTKIT_OSLOG_BUFFER_REQUEST << RTKIT_OSLOG_TYPE_SHIFT),
	    (size << RTKIT_OSLOG_BUFFER_SIZE_SHIFT) | (addr >> PAGE_SHIFT));
}

int
rtkit_handle_oslog(struct rtkit_state *state, struct aplmbox_msg *msg)
{
	bus_addr_t addr;
	bus_size_t size;

	switch (RTKIT_OSLOG_TYPE(msg->data0)) {
	case RTKIT_OSLOG_BUFFER_REQUEST:
		addr = RTKIT_OSLOG_BUFFER_ADDR(msg->data0) << PAGE_SHIFT;
		size = RTKIT_OSLOG_BUFFER_SIZE(msg->data0);
		if (addr)
			break;

		state->oslog_addr = addr;
		state->oslog_size = size;
		if (cold)
			rtkit_handle_oslog_buffer(state);
		else
			task_add(systq, &state->oslog_task);
		break;
	case RTKIT_OSLOG_UNKNOWN1:
	case RTKIT_OSLOG_UNKNOWN2:
	case RTKIT_OSLOG_UNKNOWN3:
		break;
	default:
		printf("%s: unhandled oslog event 0x%016llx\n",
		    __func__, msg->data0);
		break;
	}

	return 0;
}

int
rtkit_poll(struct rtkit_state *state)
{
	struct mbox_channel *mc = state->mc;
	struct aplmbox_msg msg;
	void (*callback)(void *, uint64_t);
	void *arg;
	uint32_t endpoint;
	int error;

	error = rtkit_recv(mc, &msg);
	if (error)
		return error;

	if (state->flags & RK_DEBUG) {
		printf("%s: 0x%016llx 0x%02x\n", __func__,
		    msg.data0, msg.data1);
	}

	endpoint = msg.data1;
	switch (endpoint) {
	case RTKIT_EP_MGMT:
		error = rtkit_handle_mgmt(state, &msg);
		if (error)
			return error;
		break;
	case RTKIT_EP_CRASHLOG:
		error = rtkit_handle_crashlog(state, &msg);
		if (error)
			return error;
		break;
	case RTKIT_EP_SYSLOG:
		error = rtkit_handle_syslog(state, &msg);
		if (error)
			return error;
		break;
	case RTKIT_EP_IOREPORT:
		error = rtkit_handle_ioreport(state, &msg);
		if (error)
			return error;
		break;
	case RTKIT_EP_OSLOG:
		error = rtkit_handle_oslog(state, &msg);
		if (error)
			return error;
		break;
	default:
		if (endpoint >= 32 && endpoint < 64 && 
		    state->callback[endpoint - 32]) {
			callback = state->callback[endpoint - 32];
			arg = state->arg[endpoint - 32];
			callback(arg, msg.data0);
			break;
		}

		printf("%s: unhandled endpoint %d\n", __func__, msg.data1);
		break;
	}

	return 0;
}

void
rtkit_rx_callback(void *cookie)
{
	rtkit_poll(cookie);
}

struct rtkit_state *
rtkit_init(int node, const char *name, int flags, struct rtkit *rk)
{
	struct rtkit_state *state;
	struct mbox_client client;

	state = malloc(sizeof(*state), M_DEVBUF, M_WAITOK | M_ZERO);
	client.mc_rx_callback = rtkit_rx_callback;
	client.mc_rx_arg = state;
	if (flags & RK_WAKEUP)
		client.mc_flags = MC_WAKEUP;
	else
		client.mc_flags = 0;

	state->mc = mbox_channel(node, name, &client);
	if (state->mc == NULL) {
		free(state, M_DEVBUF, sizeof(*state));
		return NULL;
	}
	state->rk = rk;
	state->flags = flags;

	state->iop_pwrstate = RTKIT_MGMT_PWR_STATE_SLEEP;
	state->ap_pwrstate = RTKIT_MGMT_PWR_STATE_QUIESCED;
	
	task_set(&state->crashlog_task, rtkit_handle_crashlog_buffer, state);
	task_set(&state->syslog_task, rtkit_handle_syslog_buffer, state);
	task_set(&state->ioreport_task, rtkit_handle_ioreport_buffer, state);
	task_set(&state->oslog_task, rtkit_handle_oslog_buffer, state);

	return state;
}

int
rtkit_boot(struct rtkit_state *state)
{
	int error;

	/* Wake up! */
	error = rtkit_set_iop_pwrstate(state, RTKIT_MGMT_PWR_STATE_INIT);
	if (error)
		return error;

	return rtkit_set_ap_pwrstate(state, RTKIT_MGMT_PWR_STATE_ON);
}

void
rtkit_shutdown(struct rtkit_state *state)
{
	struct rtkit *rk = state->rk;
	int i;

	rtkit_set_ap_pwrstate(state, RTKIT_MGMT_PWR_STATE_QUIESCED);
	rtkit_set_iop_pwrstate(state, RTKIT_MGMT_PWR_STATE_SLEEP);

	KASSERT(state->iop_pwrstate == RTKIT_MGMT_PWR_STATE_SLEEP);
	KASSERT(state->ap_pwrstate == RTKIT_MGMT_PWR_STATE_QUIESCED);
	state->epmap = 0;

	state->crashlog = NULL;
	state->ioreport = NULL;
	state->oslog = NULL;
	state->syslog = NULL;

	/* Clean up our memory allocations. */
	for (i = 0; i < state->ndmamem; i++) {
		if (rk->rk_unmap) {
			rk->rk_unmap(rk->rk_cookie,
			    state->dmamem[i].rdm_seg.ds_addr,
			    state->dmamem[i].rdm_seg.ds_len);
		}
		bus_dmamap_unload(rk->rk_dmat, state->dmamem[i].rdm_map);
		bus_dmamap_destroy(rk->rk_dmat, state->dmamem[i].rdm_map);
		bus_dmamem_unmap(rk->rk_dmat, state->dmamem[i].rdm_kva,
		    state->dmamem[i].rdm_size);
		bus_dmamem_free(rk->rk_dmat, &state->dmamem[i].rdm_seg, 1);
	}
	state->ndmamem = 0;
}

int
rtkit_set_ap_pwrstate(struct rtkit_state *state, uint16_t pwrstate)
{
	int error, timo;

	if (state->ap_pwrstate == pwrstate)
		return 0;

	error = rtkit_send(state, RTKIT_EP_MGMT, RTKIT_MGMT_AP_PWR_STATE,
	    pwrstate);
	if (error)
		return error;

	if (cold) {
		for (timo = 0; timo < 100000; timo++) {
			error = rtkit_poll(state);
			if (error == EWOULDBLOCK) {
				delay(10);
				continue;
			}
			if (error)
				return error;

			if (state->ap_pwrstate == pwrstate)
				return 0;
		}
	}

	while (state->ap_pwrstate != pwrstate) {
		error = tsleep_nsec(&state->ap_pwrstate, PWAIT, "appwr",
		    SEC_TO_NSEC(1));
		if (error)
			return error;
	}

	return 0;
}

int
rtkit_set_iop_pwrstate(struct rtkit_state *state, uint16_t pwrstate)
{
	int error, timo;

	if (state->iop_pwrstate == (pwrstate & 0xff))
		return 0;

	error = rtkit_send(state, RTKIT_EP_MGMT, RTKIT_MGMT_IOP_PWR_STATE,
	    pwrstate);
	if (error)
		return error;

	if (cold) {
		for (timo = 0; timo < 100000; timo++) {
			error = rtkit_poll(state);
			if (error == EWOULDBLOCK) {
				delay(10);
				continue;
			}
			if (error)
				return error;

			if (state->iop_pwrstate == (pwrstate & 0xff))
				return 0;
		}
	}

	while (state->iop_pwrstate != (pwrstate & 0xff)) {
		error = tsleep_nsec(&state->iop_pwrstate, PWAIT, "ioppwr",
		    SEC_TO_NSEC(1));
		if (error)
			return error;
	}

	return 0;
}

int
rtkit_start_endpoint(struct rtkit_state *state, uint32_t endpoint,
    void (*callback)(void *, uint64_t), void *arg)
{
	if (endpoint < 32 || endpoint >= 64)
		return EINVAL;

	if ((state->epmap & (1ULL << endpoint)) == 0)
		return EINVAL;

	state->callback[endpoint - 32] = callback;
	state->arg[endpoint - 32] = arg;
	return rtkit_start(state, endpoint);
}

int
rtkit_send_endpoint(struct rtkit_state *state, uint32_t endpoint,
    uint64_t data)
{
	return rtkit_send(state, endpoint, 0, data);
}
