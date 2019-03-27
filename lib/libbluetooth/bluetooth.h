/*
 * bluetooth.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2009 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: bluetooth.h,v 1.5 2003/09/14 23:28:42 max Exp $
 * $FreeBSD$
 */

#ifndef _BLUETOOTH_H_
#define _BLUETOOTH_H_

#include <sys/types.h>
#include <sys/endian.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>

#include <errno.h>
#include <netdb.h>
#include <bitstring.h>

#include <netgraph/ng_message.h>
#include <netgraph/bluetooth/include/ng_bluetooth.h>
#include <netgraph/bluetooth/include/ng_hci.h>
#include <netgraph/bluetooth/include/ng_l2cap.h>
#include <netgraph/bluetooth/include/ng_btsocket.h>
#include <time.h>

__BEGIN_DECLS

/*
 * Linux BlueZ compatibility
 */

#define	bacmp(ba1, ba2)	memcmp((ba1), (ba2), sizeof(bdaddr_t))
#define	bacpy(dst, src)	memcpy((dst), (src), sizeof(bdaddr_t))
#define ba2str(ba, str)	bt_ntoa((ba), (str))
#define str2ba(str, ba)	(bt_aton((str), (ba)) == 1? 0 : -1)
#define htobs(d)	htole16(d)
#define htobl(d)	htole32(d)
#define btohs(d)	le16toh(d)
#define btohl(d)	le32toh(d)

/*
 * Interface to the outside world
 */

struct hostent *  bt_gethostbyname    (char const *name);
struct hostent *  bt_gethostbyaddr    (char const *addr, int len, int type);
struct hostent *  bt_gethostent       (void);
void              bt_sethostent       (int stayopen);
void              bt_endhostent       (void);

struct protoent * bt_getprotobyname   (char const *name);
struct protoent * bt_getprotobynumber (int proto);
struct protoent * bt_getprotoent      (void);
void              bt_setprotoent      (int stayopen);
void              bt_endprotoent      (void);

char const *      bt_ntoa             (bdaddr_t const *ba, char *str);
int               bt_aton             (char const *str, bdaddr_t *ba);

/* bt_devXXXX() functions (inspired by NetBSD) */
int               bt_devaddr          (char const *devname, bdaddr_t *addr);
int               bt_devname          (char *devname, bdaddr_t const *addr);

/* 
 * Bluetooth HCI functions
 */

#define	HCI_DEVMAX			32		/* arbitrary */
#define	HCI_DEVNAME_SIZE		NG_NODESIZ
#define	HCI_DEVFEATURES_SIZE		NG_HCI_FEATURES_SIZE

struct bt_devinfo
{
	char		devname[HCI_DEVNAME_SIZE];

	uint32_t	state;		/* device/implementation specific */

	bdaddr_t	bdaddr;
	uint16_t	_reserved0;

	uint8_t		features[HCI_DEVFEATURES_SIZE];

	/* buffer info */
	uint16_t	_reserved1;
	uint16_t	cmd_free;
	uint16_t	sco_size;
	uint16_t	sco_pkts;
	uint16_t	sco_free;
	uint16_t	acl_size;
	uint16_t	acl_pkts;
	uint16_t	acl_free;

	/* stats */
	uint32_t	cmd_sent;
	uint32_t	evnt_recv;
	uint32_t	acl_recv;
	uint32_t	acl_sent;
	uint32_t	sco_recv;
	uint32_t	sco_sent;
	uint32_t	bytes_recv;
	uint32_t	bytes_sent;

	/* misc/specific */
	uint16_t	link_policy_info;
	uint16_t	packet_type_info;
	uint16_t	role_switch_info;
	uint16_t	debug;

	uint8_t		_padding[20];	/* leave space for future additions */
};

struct bt_devreq
{
	uint16_t	opcode;
	uint8_t		event;
	void		*cparam;
	size_t		clen;
	void		*rparam;
	size_t		rlen;
};

struct bt_devfilter {
	bitstr_t	bit_decl(packet_mask, 8);
	bitstr_t	bit_decl(event_mask, 256);
};

struct bt_devinquiry {
	bdaddr_t        bdaddr;
	uint8_t         pscan_rep_mode;
	uint8_t         pscan_period_mode;
	uint8_t         dev_class[3];
	uint16_t        clock_offset;
	int8_t          rssi;
	uint8_t         data[240];
};

typedef int	(bt_devenum_cb_t)(int, struct bt_devinfo const *, void *);

int		bt_devopen (char const *devname);
int		bt_devclose(int s);
int		bt_devsend (int s, uint16_t opcode, void *param, size_t plen);
ssize_t		bt_devrecv (int s, void *buf, size_t size, time_t to);
int		bt_devreq  (int s, struct bt_devreq *r, time_t to);
int		bt_devfilter(int s, struct bt_devfilter const *newp,
			     struct bt_devfilter *oldp);
void		bt_devfilter_pkt_set(struct bt_devfilter *filter, uint8_t type);
void		bt_devfilter_pkt_clr(struct bt_devfilter *filter, uint8_t type);
int		bt_devfilter_pkt_tst(struct bt_devfilter const *filter, uint8_t type);
void		bt_devfilter_evt_set(struct bt_devfilter *filter, uint8_t event);
void		bt_devfilter_evt_clr(struct bt_devfilter *filter, uint8_t event);
int		bt_devfilter_evt_tst(struct bt_devfilter const *filter, uint8_t event);
int		bt_devinquiry(char const *devname, time_t length, int num_rsp,
			      struct bt_devinquiry **ii);
char *		bt_devremote_name(char const *devname, const bdaddr_t *remote,
				  time_t to, uint16_t clk_off,
				  uint8_t ps_rep_mode, uint8_t ps_mode);
int		bt_devinfo (struct bt_devinfo *di);
int		bt_devenum (bt_devenum_cb_t cb, void *arg);

static __inline char *
bt_devremote_name_gen(char const *btooth_devname, const bdaddr_t *remote)
{
	return (bt_devremote_name(btooth_devname, remote, 0, 0x0000,
		NG_HCI_SCAN_REP_MODE0, NG_HCI_MANDATORY_PAGE_SCAN_MODE));
}

/*
 * bdaddr utility functions (from NetBSD)
 */

static __inline int
bdaddr_same(const bdaddr_t *a, const bdaddr_t *b)
{
	return (a->b[0] == b->b[0] && a->b[1] == b->b[1] &&
		a->b[2] == b->b[2] && a->b[3] == b->b[3] &&
		a->b[4] == b->b[4] && a->b[5] == b->b[5]);
}

static __inline int
bdaddr_any(const bdaddr_t *a)
{
	return (a->b[0] == 0 && a->b[1] == 0 && a->b[2] == 0 &&
		a->b[3] == 0 && a->b[4] == 0 && a->b[5] == 0);
}

static __inline void
bdaddr_copy(bdaddr_t *d, const bdaddr_t *s)
{
	d->b[0] = s->b[0];
	d->b[1] = s->b[1];
	d->b[2] = s->b[2];
	d->b[3] = s->b[3];
	d->b[4] = s->b[4];
	d->b[5] = s->b[5];
}

__END_DECLS

#endif /* ndef _BLUETOOTH_H_ */

