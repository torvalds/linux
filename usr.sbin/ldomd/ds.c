/*	$OpenBSD: ds.c,v 1.12 2022/07/27 19:42:22 kn Exp $	*/

/*
 * Copyright (c) 2012 Mark Kettenis
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

#include <sys/types.h>
#include <sys/poll.h>
#include <sys/queue.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ds.h"
#include "ldom_util.h"

void	ldc_rx_ctrl_vers(struct ldc_conn *, struct ldc_pkt *);
void	ldc_rx_ctrl_rtr(struct ldc_conn *, struct ldc_pkt *);
void	ldc_rx_ctrl_rts(struct ldc_conn *, struct ldc_pkt *);
void	ldc_rx_ctrl_rdx(struct ldc_conn *, struct ldc_pkt *);

void	ldc_send_ack(struct ldc_conn *);
void	ldc_send_nack(struct ldc_conn *);
void	ldc_send_rtr(struct ldc_conn *);
void	ldc_send_rts(struct ldc_conn *);
void	ldc_send_rdx(struct ldc_conn *);

void
ldc_rx_ctrl(struct ldc_conn *lc, struct ldc_pkt *lp)
{
	switch (lp->ctrl) {
	case LDC_VERS:
		ldc_rx_ctrl_vers(lc, lp);
		break;

	case LDC_RTS:
		ldc_rx_ctrl_rts(lc, lp);
		break;

	case LDC_RTR:
		ldc_rx_ctrl_rtr(lc, lp);
		break;

	case LDC_RDX:
		ldc_rx_ctrl_rdx(lc, lp);
		break;

	default:
		DPRINTF(("CTRL/0x%02x/0x%02x\n", lp->stype, lp->ctrl));
		ldc_reset(lc);
		break;
	}
}

void
ldc_rx_ctrl_vers(struct ldc_conn *lc, struct ldc_pkt *lp)
{
	struct ldc_pkt *lvp = (struct ldc_pkt *)lp;

	switch (lp->stype) {
	case LDC_INFO:
		if (lc->lc_state == LDC_RCV_VERS) {
			DPRINTF(("Spurious CTRL/INFO/VERS: state %d\n",
			    lc->lc_state));
			return;
		}
		DPRINTF(("CTRL/INFO/VERS %d.%d\n", lvp->major, lvp->minor));
		if (lvp->major == LDC_VERSION_MAJOR &&
		    lvp->minor == LDC_VERSION_MINOR)
			ldc_send_ack(lc);
		else
			ldc_send_nack(lc);
		break;

	case LDC_ACK:
		if (lc->lc_state != LDC_SND_VERS) {
			DPRINTF(("Spurious CTRL/ACK/VERS: state %d\n",
			    lc->lc_state));
			ldc_reset(lc);
			return;
		}
		DPRINTF(("CTRL/ACK/VERS\n"));
		ldc_send_rts(lc);
		break;

	case LDC_NACK:
		DPRINTF(("CTRL/NACK/VERS\n"));
		ldc_reset(lc);
		break;

	default:
		DPRINTF(("CTRL/0x%02x/VERS\n", lp->stype));
		ldc_reset(lc);
		break;
	}
}

void
ldc_rx_ctrl_rts(struct ldc_conn *lc, struct ldc_pkt *lp)
{
	switch (lp->stype) {
	case LDC_INFO:
		if (lc->lc_state != LDC_RCV_VERS) {
			DPRINTF(("Spurious CTRL/INFO/RTS: state %d\n",
			    lc->lc_state));
			ldc_reset(lc);
			return;
		}
		DPRINTF(("CTRL/INFO/RTS\n"));
		if (lp->env != LDC_MODE_RELIABLE) {
			ldc_reset(lc);
			return;
		}
		ldc_send_rtr(lc);
		break;

	case LDC_ACK:
		DPRINTF(("CTRL/ACK/RTS\n"));
		ldc_reset(lc);
		break;

	case LDC_NACK:
		DPRINTF(("CTRL/NACK/RTS\n"));
		ldc_reset(lc);
		break;

	default:
		DPRINTF(("CTRL/0x%02x/RTS\n", lp->stype));
		ldc_reset(lc);
		break;
	}
}

void
ldc_rx_ctrl_rtr(struct ldc_conn *lc, struct ldc_pkt *lp)
{
	switch (lp->stype) {
	case LDC_INFO:
		if (lc->lc_state != LDC_SND_RTS) {
			DPRINTF(("Spurious CTRL/INFO/RTR: state %d\n",
			    lc->lc_state));
			ldc_reset(lc);
			return;
		}
		DPRINTF(("CTRL/INFO/RTR\n"));
		if (lp->env != LDC_MODE_RELIABLE) {
			ldc_reset(lc);
			return;
		}
		ldc_send_rdx(lc);
#if 0
		lc->lc_start(lc);
#endif
		break;

	case LDC_ACK:
		DPRINTF(("CTRL/ACK/RTR\n"));
		ldc_reset(lc);
		break;

	case LDC_NACK:
		DPRINTF(("CTRL/NACK/RTR\n"));
		ldc_reset(lc);
		break;

	default:
		DPRINTF(("CTRL/0x%02x/RTR\n", lp->stype));
		ldc_reset(lc);
		break;
	}
}

void
ldc_rx_ctrl_rdx(struct ldc_conn *lc, struct ldc_pkt *lp)
{
	switch (lp->stype) {
	case LDC_INFO:
		if (lc->lc_state != LDC_SND_RTR) {
			DPRINTF(("Spurious CTRL/INFO/RTR: state %d\n",
			    lc->lc_state));
			ldc_reset(lc);
			return;
		}
		DPRINTF(("CTRL/INFO/RDX\n"));
#if 0
		lc->lc_start(lc);
#endif
		break;

	case LDC_ACK:
		DPRINTF(("CTRL/ACK/RDX\n"));
		ldc_reset(lc);
		break;

	case LDC_NACK:
		DPRINTF(("CTRL/NACK/RDX\n"));
		ldc_reset(lc);
		break;

	default:
		DPRINTF(("CTRL/0x%02x/RDX\n", lp->stype));
		ldc_reset(lc);
		break;
	}
}

void
ldc_rx_data(struct ldc_conn *lc, struct ldc_pkt *lp)
{
	size_t len;

	if (lp->stype != LDC_INFO && lp->stype != LDC_ACK) {
		DPRINTF(("DATA/0x%02x\n", lp->stype));
		ldc_reset(lc);
		return;
	}

	if (lc->lc_state != LDC_SND_RTR &&
	    lc->lc_state != LDC_SND_RDX) {
		DPRINTF(("Spurious DATA/INFO: state %d\n", lc->lc_state));
		ldc_reset(lc);
		return;
	}

#if 0
	if (lp->ackid) {
		int i;

		for (i = 0; ds_service[i].ds_svc_id; i++) {
			if (ds_service[i].ds_ackid &&
			    lp->ackid >= ds_service[i].ds_ackid) {
				ds_service[i].ds_ackid = 0;
				ds_service[i].ds_start(lc, ds_service[i].ds_svc_handle);
			}
		}
	}
#endif
	if (lp->stype == LDC_ACK)
		return;

	if (lp->env & LDC_FRAG_START) {
		lc->lc_len = (lp->env & LDC_LEN_MASK);
		memcpy((uint8_t *)lc->lc_msg, &lp->data, lc->lc_len);
	} else {
		len = (lp->env & LDC_LEN_MASK);
		if (lc->lc_len + len > sizeof(lc->lc_msg)) {
			DPRINTF(("Buffer overrun\n"));
			ldc_reset(lc);
			return;
		}
		memcpy((uint8_t *)lc->lc_msg + lc->lc_len, &lp->data, len);
		lc->lc_len += len;
	}

	if (lp->env & LDC_FRAG_STOP) {
		ldc_ack(lc, lp->seqid);
		lc->lc_rx_data(lc, lc->lc_msg, lc->lc_len);
	}
}

void
ldc_send_vers(struct ldc_conn *lc)
{
	struct ldc_pkt lp;
	ssize_t nbytes;

	bzero(&lp, sizeof(lp));
	lp.type = LDC_CTRL;
	lp.stype = LDC_INFO;
	lp.ctrl = LDC_VERS;
	lp.major = 1;
	lp.minor = 0;

	nbytes = write(lc->lc_fd, &lp, sizeof(lp));
	if (nbytes != sizeof(lp))
		err(1, "write");

	lc->lc_state = LDC_SND_VERS;
}

void
ldc_send_ack(struct ldc_conn *lc)
{
	struct ldc_pkt lp;
	ssize_t nbytes;

	bzero(&lp, sizeof(lp));
	lp.type = LDC_CTRL;
	lp.stype = LDC_ACK;
	lp.ctrl = LDC_VERS;
	lp.major = 1;
	lp.minor = 0;

	nbytes = write(lc->lc_fd, &lp, sizeof(lp));
	if (nbytes != sizeof(lp))
		err(1, "write");

	lc->lc_state = LDC_RCV_VERS;
}

void
ldc_send_nack(struct ldc_conn *lc)
{
	struct ldc_pkt lp;
	ssize_t nbytes;

	bzero(&lp, sizeof(lp));
	lp.type = LDC_CTRL;
	lp.stype = LDC_NACK;
	lp.ctrl = LDC_VERS;
	lp.major = 1;
	lp.minor = 0;

	nbytes = write(lc->lc_fd, &lp, sizeof(lp));
	if (nbytes != sizeof(lp))
		err(1, "write");

	lc->lc_state = 0;
}

void
ldc_send_rts(struct ldc_conn *lc)
{
	struct ldc_pkt lp;
	ssize_t nbytes;

	bzero(&lp, sizeof(lp));
	lp.type = LDC_CTRL;
	lp.stype = LDC_INFO;
	lp.ctrl = LDC_RTS;
	lp.env = LDC_MODE_RELIABLE;
	lp.seqid = lc->lc_tx_seqid++;

	nbytes = write(lc->lc_fd, &lp, sizeof(lp));
	if (nbytes != sizeof(lp))
		err(1, "write");

	lc->lc_state = LDC_SND_RTS;
}

void
ldc_send_rtr(struct ldc_conn *lc)
{
	struct ldc_pkt lp;
	ssize_t nbytes;

	bzero(&lp, sizeof(lp));
	lp.type = LDC_CTRL;
	lp.stype = LDC_INFO;
	lp.ctrl = LDC_RTR;
	lp.env = LDC_MODE_RELIABLE;
	lp.seqid = lc->lc_tx_seqid++;

	nbytes = write(lc->lc_fd, &lp, sizeof(lp));
	if (nbytes != sizeof(lp))
		err(1, "write");

	lc->lc_state = LDC_SND_RTR;
}

void
ldc_send_rdx(struct ldc_conn *lc)
{
	struct ldc_pkt lp;
	ssize_t nbytes;

	bzero(&lp, sizeof(lp));
	lp.type = LDC_CTRL;
	lp.stype = LDC_INFO;
	lp.ctrl = LDC_RDX;
	lp.env = LDC_MODE_RELIABLE;
	lp.seqid = lc->lc_tx_seqid++;

	nbytes = write(lc->lc_fd, &lp, sizeof(lp));
	if (nbytes != sizeof(lp))
		err(1, "write");

	lc->lc_state = LDC_SND_RDX;
}

void
ldc_reset(struct ldc_conn *lc)
{
	lc->lc_tx_seqid = 0;
	lc->lc_state = 0;
#if 0
	lc->lc_reset(lc);
#endif
}

void
ldc_ack(struct ldc_conn *lc, uint32_t ackid)
{
	struct ldc_pkt lp;
	ssize_t nbytes;

	bzero(&lp, sizeof(lp));
	lp.type = LDC_DATA;
	lp.stype = LDC_ACK;
	lp.seqid = lc->lc_tx_seqid++;
	lp.ackid = ackid;
	nbytes = write(lc->lc_fd, &lp, sizeof(lp));
	if (nbytes != sizeof(lp))
		err(1, "write");
}

void
ds_rx_msg(struct ldc_conn *lc, void *data, size_t len)
{
	struct ds_conn *dc = lc->lc_cookie;
	struct ds_msg *dm = data;

	switch(dm->msg_type) {
	case DS_INIT_REQ:
	{
		struct ds_init_req *dr = data;

		DPRINTF(("DS_INIT_REQ %d.%d\n", dr->major_vers,
		    dr->minor_vers));
		if (dr->major_vers != 1 || dr->minor_vers != 0){
			ldc_reset(lc);
			return;
		}
		ds_init_ack(lc);
		break;
	}

	case DS_REG_REQ:
	{
		struct ds_reg_req *dr = data;
		struct ds_conn_svc *dcs;
		uint16_t major = 0;

		DPRINTF(("DS_REG_REQ %s %d.%d 0x%016llx\n", dr->svc_id,
		    dr->major_vers, dr->minor_vers, dr->svc_handle));
		TAILQ_FOREACH(dcs, &dc->services, link) {
			if (strcmp(dr->svc_id, dcs->service->ds_svc_id) == 0 &&
			    dr->major_vers == dcs->service->ds_major_vers) {
				dcs->svc_handle = dr->svc_handle;
				dcs->ackid = lc->lc_tx_seqid;
				ds_reg_ack(lc, dcs->svc_handle,
				    dcs->service->ds_minor_vers);
				dcs->service->ds_start(lc, dcs->svc_handle);
				return;
			}
		}

		TAILQ_FOREACH(dcs, &dc->services, link) {
			if (strcmp(dr->svc_id, dcs->service->ds_svc_id) == 0 &&
			    dcs->service->ds_major_vers > major)
				major = dcs->service->ds_major_vers;
		}

		ds_reg_nack(lc, dr->svc_handle, major);
		break;
	}

	case DS_UNREG:
	{
		struct ds_unreg *du = data;

		DPRINTF(("DS_UNREG 0x%016llx\n", du->svc_handle));
		ds_unreg_ack(lc, du->svc_handle);
		break;
	}

	case DS_DATA:
	{
		struct ds_data *dd = data;
		struct ds_conn_svc *dcs;

		DPRINTF(("DS_DATA 0x%016llx\n", dd->svc_handle));
		TAILQ_FOREACH(dcs, &dc->services, link) {
			if (dcs->svc_handle == dd->svc_handle)
				dcs->service->ds_rx_data(lc, dd->svc_handle,
				    data, len);
		}
		break;
	}

	default:
		DPRINTF(("Unknown DS message type 0x%x\n", dm->msg_type));
		ldc_reset(lc);
		break;
	}
}

void
ds_init_ack(struct ldc_conn *lc)
{
	struct ds_init_ack da;

	DPRINTF((" DS_INIT_ACK\n"));
	bzero(&da, sizeof(da));
	da.msg_type = DS_INIT_ACK;
	da.payload_len = sizeof(da) - 8;
	da.minor_vers = 0;
	ds_send_msg(lc, &da, sizeof(da));
}

void
ds_reg_ack(struct ldc_conn *lc, uint64_t svc_handle, uint16_t minor)
{
	struct ds_reg_ack da;

	DPRINTF((" DS_REG_ACK 0x%016llx\n", svc_handle));
	bzero(&da, sizeof(da));
	da.msg_type = DS_REG_ACK;
	da.payload_len = sizeof(da) - 8;
	da.svc_handle = svc_handle;
	da.minor_vers = minor;
	ds_send_msg(lc, &da, sizeof(da));
}

void
ds_reg_nack(struct ldc_conn *lc, uint64_t svc_handle, uint16_t major)
{
	struct ds_reg_nack dn;

	DPRINTF((" DS_REG_NACK 0x%016llx\n", svc_handle));
	bzero(&dn, sizeof(dn));
	dn.msg_type = DS_REG_NACK;
	dn.payload_len = sizeof(dn) - 8;
	dn.svc_handle = svc_handle;
	dn.result = DS_REG_VER_NACK;
	dn.major_vers = major;
	ds_send_msg(lc, &dn, sizeof(dn));
}

void
ds_unreg_ack(struct ldc_conn *lc, uint64_t svc_handle)
{
	struct ds_unreg du;

	DPRINTF((" DS_UNREG_ACK 0x%016llx\n", svc_handle));
	bzero(&du, sizeof(du));
	du.msg_type = DS_UNREG_ACK;
	du.payload_len = sizeof(du) - 8;
	du.svc_handle = svc_handle;
	ds_send_msg(lc, &du, sizeof(du));
}

void
ds_unreg_nack(struct ldc_conn *lc, uint64_t svc_handle)
{
	struct ds_unreg du;

	DPRINTF((" DS_UNREG_NACK 0x%016llx\n", svc_handle));
	bzero(&du, sizeof(du));
	du.msg_type = DS_UNREG_NACK;
	du.payload_len = sizeof(du) - 8;
	du.svc_handle = svc_handle;
	ds_send_msg(lc, &du, sizeof(du));
}

void
ds_receive_msg(struct ldc_conn *lc, void *buf, size_t len)
{
	int env = LDC_FRAG_START;
	struct ldc_pkt lp;
	uint8_t *p = buf;
	ssize_t nbytes;

	while (len > 0) {
		nbytes = read(lc->lc_fd, &lp, sizeof(lp));
		if (nbytes != sizeof(lp))
			err(1, "read");

		if (lp.type != LDC_DATA &&
		    lp.stype != LDC_INFO) {
			ldc_reset(lc);
			return;
		}

		if ((lp.env & LDC_FRAG_START) != env) {
			ldc_reset(lc);
			return;
		}

		bcopy(&lp.data, p, (lp.env & LDC_LEN_MASK));
		p += (lp.env & LDC_LEN_MASK);
		len -= (lp.env & LDC_LEN_MASK);

		if (lp.env & LDC_FRAG_STOP)
			ldc_ack(lc, lp.seqid);

		env = (lp.env & LDC_FRAG_STOP) ? LDC_FRAG_START : 0;
	}
}

void
ldc_send_msg(struct ldc_conn *lc, void *buf, size_t len)
{
	struct ldc_pkt lp;
	uint8_t *p = buf;
	ssize_t nbytes;

	while (len > 0) {
		bzero(&lp, sizeof(lp));
		lp.type = LDC_DATA;
		lp.stype = LDC_INFO;
		lp.env = min(len, LDC_PKT_PAYLOAD);
		if (p == buf)
			lp.env |= LDC_FRAG_START;
		if (len <= LDC_PKT_PAYLOAD)
			lp.env |= LDC_FRAG_STOP;
		lp.seqid = lc->lc_tx_seqid++;
		bcopy(p, &lp.data, min(len, LDC_PKT_PAYLOAD));

		nbytes = write(lc->lc_fd, &lp, sizeof(lp));
		if (nbytes != sizeof(lp))
			err(1, "write");
		p += min(len, LDC_PKT_PAYLOAD);
		len -= min(len, LDC_PKT_PAYLOAD);
	}
}

void
ds_send_msg(struct ldc_conn *lc, void *buf, size_t len)
{
	uint8_t *p = buf;
	struct ldc_pkt lp;
	ssize_t nbytes;

	while (len > 0) {
		ldc_send_msg(lc, p, min(len, LDC_MSG_MAX));
		p += min(len, LDC_MSG_MAX);
		len -= min(len, LDC_MSG_MAX);

		if (len > 0) {
			/* Consume ACK. */
			nbytes = read(lc->lc_fd, &lp, sizeof(lp));
			if (nbytes != sizeof(lp))
				err(1, "read");
#if 0
			{
				uint64_t *msg = (uint64_t *)&lp;
				int i;

				for (i = 0; i < 8; i++)
					printf("%02x: %016llx\n", i, msg[i]);
			}
#endif
		}
	}
}

TAILQ_HEAD(ds_conn_head, ds_conn) ds_conns =
    TAILQ_HEAD_INITIALIZER(ds_conns);
int num_ds_conns;

struct ds_conn *
ds_conn_open(const char *path, void *cookie)
{
	struct ds_conn *dc;

	dc = xmalloc(sizeof(*dc));
	dc->path = xstrdup(path);
	dc->cookie = cookie;

	dc->fd = open(path, O_RDWR);
	if (dc->fd == -1)
		err(1, "cannot open %s", path);

	memset(&dc->lc, 0, sizeof(dc->lc));
	dc->lc.lc_fd = dc->fd;
	dc->lc.lc_cookie = dc;
	dc->lc.lc_rx_data = ds_rx_msg;

	TAILQ_INIT(&dc->services);
	TAILQ_INSERT_TAIL(&ds_conns, dc, link);
	dc->id = num_ds_conns++;
	return dc;
}

void
ds_conn_register_service(struct ds_conn *dc, struct ds_service *ds)
{
	struct ds_conn_svc *dcs;

	dcs = xzalloc(sizeof(*dcs));
	dcs->service = ds;

	TAILQ_INSERT_TAIL(&dc->services, dcs, link);
}

void
ds_conn_handle(struct ds_conn *dc)
{
	struct ldc_pkt lp;
	ssize_t nbytes;

	nbytes = read(dc->fd, &lp, sizeof(lp));
	if (nbytes != sizeof(lp)) {
		ldc_reset(&dc->lc);
		return;
	}

	switch (lp.type) {
	case LDC_CTRL:
		ldc_rx_ctrl(&dc->lc, &lp);
		break;
	case LDC_DATA:
		ldc_rx_data(&dc->lc, &lp);
		break;
	default:
		DPRINTF(("0x%02x/0x%02x/0x%02x\n", lp.type, lp.stype,
		    lp.ctrl));
		ldc_reset(&dc->lc);
		break;
	}
}

void
ds_conn_serve(void)
{
	struct ds_conn *dc;
	struct pollfd *pfd;
	int nfds;

	pfd = xreallocarray(NULL, num_ds_conns, sizeof(*pfd));
	TAILQ_FOREACH(dc, &ds_conns, link) {
		pfd[dc->id].fd = dc->fd;
		pfd[dc->id].events = POLLIN;
	}

	while (1) {
		nfds = poll(pfd, num_ds_conns, -1);
		if (nfds == -1)
			err(1, "poll");
		if (nfds == 0)
			errx(1, "poll timeout");

		TAILQ_FOREACH(dc, &ds_conns, link) {
			if (pfd[dc->id].revents)
				ds_conn_handle(dc);
		}
	}
}
