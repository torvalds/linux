/*-
 * host_controller_baseband.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2002 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * $Id: host_controller_baseband.c,v 1.4 2003/08/18 19:19:53 max Exp $
 * $FreeBSD$
 */

#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "hccontrol.h"

/* Convert hex ASCII to int4 */
static int
hci_hexa2int4(const char *a)
{
	if ('0' <= *a && *a <= '9')
		return (*a - '0');

	if ('A' <= *a && *a <= 'F')
		return (*a - 'A' + 0xa);

	if ('a' <= *a && *a <= 'f')
		return (*a - 'a' + 0xa);

	return (-1);
}

/* Convert hex ASCII to int8 */
static int
hci_hexa2int8(const char *a)
{
	int	hi = hci_hexa2int4(a);
	int	lo = hci_hexa2int4(a + 1);

	if (hi < 0 || lo < 0)
		return (-1);

	return ((hi << 4) | lo);
}

/* Convert ascii hex string to the uint8_t[] */
static int
hci_hexstring2array(char const *s, uint8_t *a, int asize)
{
	int	i, l, b;

	l = strlen(s) / 2;
	if (l > asize)
		l = asize;

	for (i = 0; i < l; i++) {
		b = hci_hexa2int8(s + i * 2);
		if (b < 0)
			return (-1);

		a[i] = (b & 0xff);
	}

	return (0);
}

/* Send RESET to the unit */
static int
hci_reset(int s, int argc, char **argv)
{
	ng_hci_status_rp	rp;
	int			n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_RESET), (char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n",
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_reset */

/* Send Read_PIN_Type command to the unit */
static int
hci_read_pin_type(int s, int argc, char **argv)
{
	ng_hci_read_pin_type_rp	rp;
	int			n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_PIN_TYPE),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "PIN type: %s [%#02x]\n",
			hci_pin2str(rp.pin_type), rp.pin_type);

	return (OK);
} /* hci_read_pin_type */

/* Send Write_PIN_Type command to the unit */
static int
hci_write_pin_type(int s, int argc, char **argv)
{
	ng_hci_write_pin_type_cp	cp;
	ng_hci_write_pin_type_rp	rp;
	int				n;

	/* parse command parameters */
	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%d", &n) != 1 || n < 0 || n > 1)
			return (USAGE);

		cp.pin_type = (uint8_t) n;
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_WRITE_PIN_TYPE),
			(char const *) &cp, sizeof(cp),
			(char *) &rp , &n) ==  ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_pin_type */

/* Send Read_Stored_Link_Key command to the unit */
static int
hci_read_stored_link_key(int s, int argc, char **argv)
{
	struct {
		ng_hci_cmd_pkt_t			hdr;
		ng_hci_read_stored_link_key_cp		cp;
	} __attribute__ ((packed))			cmd;

	struct {
		ng_hci_event_pkt_t			hdr;
		union {
			ng_hci_command_compl_ep		cc;
			ng_hci_return_link_keys_ep	key;
			uint8_t				b[NG_HCI_EVENT_PKT_SIZE];
		}					ep;
	} __attribute__ ((packed))			event;

	int						n, n1;

	/* Send command */
	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.type = NG_HCI_CMD_PKT;
	cmd.hdr.opcode = htole16(NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
				NG_HCI_OCF_READ_STORED_LINK_KEY));
	cmd.hdr.length = sizeof(cmd.cp);

	switch (argc) {
	case 1:
		/* parse BD_ADDR */
		if (!bt_aton(argv[0], &cmd.cp.bdaddr)) {
			struct hostent	*he = NULL;

			if ((he = bt_gethostbyname(argv[0])) == NULL)
				return (USAGE);

			memcpy(&cmd.cp.bdaddr, he->h_addr, sizeof(cmd.cp.bdaddr));
		}
		break;

	default:
		cmd.cp.read_all = 1;
		break;
	}

	if (hci_send(s, (char const *) &cmd, sizeof(cmd)) != OK)
		return (ERROR);

	/* Receive events */
again:
	memset(&event, 0, sizeof(event));
	n = sizeof(event);
	if (hci_recv(s, (char *) &event, &n) != OK)
		return (ERROR);

	if (n <= sizeof(event.hdr)) {
		errno = EMSGSIZE;
		return (ERROR);
	}

	if (event.hdr.type != NG_HCI_EVENT_PKT) {
		errno = EIO;
		return (ERROR);
	}

	/* Parse event */
	switch (event.hdr.event) {
	case NG_HCI_EVENT_COMMAND_COMPL: {
		ng_hci_read_stored_link_key_rp	*rp = NULL;

		if (event.ep.cc.opcode == 0x0000 ||
		    event.ep.cc.opcode != cmd.hdr.opcode)
			goto again;

		rp = (ng_hci_read_stored_link_key_rp *)(event.ep.b + 
				sizeof(event.ep.cc));

		fprintf(stdout, "Complete: Status: %s [%#x]\n", 
				hci_status2str(rp->status), rp->status);
		fprintf(stdout, "Maximum Number of keys: %d\n",
				le16toh(rp->max_num_keys));
		fprintf(stdout, "Number of keys read: %d\n",
				le16toh(rp->num_keys_read));
		} break;

	case NG_HCI_EVENT_RETURN_LINK_KEYS: {
		struct _key {
			bdaddr_t	bdaddr;
			uint8_t		key[NG_HCI_KEY_SIZE];
		} __attribute__ ((packed))	*k = NULL;

		fprintf(stdout, "Event: Number of keys: %d\n",
			event.ep.key.num_keys);

		k = (struct _key *)(event.ep.b + sizeof(event.ep.key));
		for (n = 0; n < event.ep.key.num_keys; n++) {
			fprintf(stdout, "\t%d: %s ",
				n + 1, hci_bdaddr2str(&k->bdaddr));

			for (n1 = 0; n1 < sizeof(k->key); n1++)
				fprintf(stdout, "%02x", k->key[n1]);
			fprintf(stdout, "\n");

			k ++;
		}

		goto again;

		} break;

	default:
		goto again;
	}
	
	return (OK);
} /* hci_read_store_link_key */

/* Send Write_Stored_Link_Key command to the unit */
static int
hci_write_stored_link_key(int s, int argc, char **argv)
{
	struct {
		ng_hci_write_stored_link_key_cp	p;
		bdaddr_t			bdaddr;
		uint8_t				key[NG_HCI_KEY_SIZE];
	}					cp;
	ng_hci_write_stored_link_key_rp		rp;
	int32_t					n;

	memset(&cp, 0, sizeof(cp));

	switch (argc) {
	case 2:
		cp.p.num_keys_write = 1;

		/* parse BD_ADDR */
		if (!bt_aton(argv[0], &cp.bdaddr)) {
			struct hostent	*he = NULL;

			if ((he = bt_gethostbyname(argv[0])) == NULL)
				return (USAGE);

			memcpy(&cp.bdaddr, he->h_addr, sizeof(cp.bdaddr));
		}

		/* parse key */
		if (hci_hexstring2array(argv[1], cp.key, sizeof(cp.key)) < 0)
			return (USAGE);
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND, 
			NG_HCI_OCF_WRITE_STORED_LINK_KEY),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Number of keys written: %d\n", rp.num_keys_written);

	return (OK);
} /* hci_write_stored_link_key */


/* Send Delete_Stored_Link_Key command to the unit */
static int
hci_delete_stored_link_key(int s, int argc, char **argv)
{
	ng_hci_delete_stored_link_key_cp	cp;
	ng_hci_delete_stored_link_key_rp	rp;
	int32_t					n;

	memset(&cp, 0, sizeof(cp));

	switch (argc) {
	case 1:
		/* parse BD_ADDR */
		if (!bt_aton(argv[0], &cp.bdaddr)) {
			struct hostent	*he = NULL;

			if ((he = bt_gethostbyname(argv[0])) == NULL)
				return (USAGE);

			memcpy(&cp.bdaddr, he->h_addr, sizeof(cp.bdaddr));
		}
		break;

	default:
		cp.delete_all = 1;
		break;
	}

	/* send command */
	n = sizeof(cp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND, 
			NG_HCI_OCF_DELETE_STORED_LINK_KEY),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Number of keys deleted: %d\n", rp.num_keys_deleted);

	return (OK);
} /* hci_delete_stored_link_key */

/* Send Change_Local_Name command to the unit */
static int
hci_change_local_name(int s, int argc, char **argv) 
{
	ng_hci_change_local_name_cp	cp;
	ng_hci_change_local_name_rp	rp;
	int				n;

	/* parse command parameters */
	switch (argc) {
	case 1:
		snprintf(cp.name, sizeof(cp.name), "%s", argv[0]);
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND, 
			NG_HCI_OCF_CHANGE_LOCAL_NAME),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_change_local_name */

/* Send Read_Local_Name command to the unit */
static int
hci_read_local_name(int s, int argc, char **argv)
{
	ng_hci_read_local_name_rp	rp;
	int				n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_LOCAL_NAME),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Local name: %s\n", rp.name);

	return (OK);
} /* hci_read_local_name */

/* Send Read_Connection_Accept_Timeout to the unit */
static int
hci_read_connection_accept_timeout(int s, int argc, char **argv)
{
	ng_hci_read_con_accept_timo_rp	rp;
	int				n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND, 
			NG_HCI_OCF_READ_CON_ACCEPT_TIMO),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	rp.timeout = le16toh(rp.timeout);
	fprintf(stdout, "Connection accept timeout: %.2f msec [%d slots]\n",
			rp.timeout * 0.625, rp.timeout);
					
	return (OK);
} /* hci_read_connection_accept_timeout */

/* Send Write_Connection_Accept_Timeout to the unit */
static int
hci_write_connection_accept_timeout(int s, int argc, char **argv)
{
	ng_hci_write_con_accept_timo_cp	cp;
	ng_hci_write_con_accept_timo_rp	rp;
	int				n;

	/* parse command parameters */
	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%d", &n) != 1 || n < 1 || n > 0xb540)
			return (USAGE);

		cp.timeout = (uint16_t) n;
		cp.timeout = htole16(cp.timeout);
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND, 
			NG_HCI_OCF_WRITE_CON_ACCEPT_TIMO),
			(char const *) &cp, sizeof(cp), 
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n",
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_connection_accept_timeout */

/* Send Read_Page_Timeout command to the unit */
static int
hci_read_page_timeout(int s, int argc, char **argv)
{
	ng_hci_read_page_timo_rp	rp;
	int				n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_PAGE_TIMO),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	rp.timeout = le16toh(rp.timeout);
	fprintf(stdout, "Page timeout: %.2f msec [%d slots]\n",
		rp.timeout * 0.625, rp.timeout);

	return (OK);
} /* hci_read_page_timeoout */

/* Send Write_Page_Timeout command to the unit */
static int
hci_write_page_timeout(int s, int argc, char **argv)
{
	ng_hci_write_page_timo_cp	cp;
	ng_hci_write_page_timo_rp	rp;
	int				n;

	/* parse command parameters */
	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%d", &n) != 1 || n < 1 || n > 0xffff)
			return (USAGE);

		cp.timeout = (uint16_t) n;
		cp.timeout = htole16(cp.timeout);
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND, 
			NG_HCI_OCF_WRITE_PAGE_TIMO),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_page_timeout */

/* Send Read_Scan_Enable command to the unit */
static int
hci_read_scan_enable(int s, int argc, char **argv)
{
	ng_hci_read_scan_enable_rp	rp;
	int				n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND, 
			NG_HCI_OCF_READ_SCAN_ENABLE),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Scan enable: %s [%#02x]\n",
		hci_scan2str(rp.scan_enable), rp.scan_enable);

	return (OK);
} /* hci_read_scan_enable */

/* Send Write_Scan_Enable command to the unit */
static int
hci_write_scan_enable(int s, int argc, char **argv)
{
	ng_hci_write_scan_enable_cp	cp;
	ng_hci_write_scan_enable_rp	rp;
	int				n;

	/* parse command parameters */
	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%d", &n) != 1 || n < 0 || n > 3)
			return (USAGE);

		cp.scan_enable = (uint8_t) n;
		break;

	default:
		return (USAGE);
	}

	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND, 
			NG_HCI_OCF_WRITE_SCAN_ENABLE),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n",
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_scan_enable */

/* Send Read_Page_Scan_Activity command to the unit */
static int
hci_read_page_scan_activity(int s, int argc, char **argv)
{
	ng_hci_read_page_scan_activity_rp	rp;
	int					n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_PAGE_SCAN_ACTIVITY),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n",
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	rp.page_scan_interval = le16toh(rp.page_scan_interval);
	rp.page_scan_window = le16toh(rp.page_scan_window);

	fprintf(stdout, "Page Scan Interval: %.2f msec [%d slots]\n",
		rp.page_scan_interval * 0.625, rp.page_scan_interval);
	fprintf(stdout, "Page Scan Window: %.2f msec [%d slots]\n",
		rp.page_scan_window * 0.625, rp.page_scan_window);

	return (OK);
} /* hci_read_page_scan_activity */

/* Send Write_Page_Scan_Activity command to the unit */
static int
hci_write_page_scan_activity(int s, int argc, char **argv)
{
	ng_hci_write_page_scan_activity_cp	cp;
	ng_hci_write_page_scan_activity_rp	rp;
	int					n;

	/* parse command parameters */
	switch (argc) {
	case 2:
		/* page scan interval */
		if (sscanf(argv[0], "%d", &n) != 1 || n < 0x12 || n > 0x1000)
			return (USAGE);

		cp.page_scan_interval = (uint16_t) n;

		/* page scan window */
		if (sscanf(argv[1], "%d", &n) != 1 || n < 0x12 || n > 0x1000)
			return (USAGE);

		cp.page_scan_window = (uint16_t) n;

		if (cp.page_scan_window > cp.page_scan_interval)
			return (USAGE);

		cp.page_scan_interval = htole16(cp.page_scan_interval);
		cp.page_scan_window = htole16(cp.page_scan_window);
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_WRITE_PAGE_SCAN_ACTIVITY),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_page_scan_activity */

/* Send Read_Inquiry_Scan_Activity command to the unit */
static int
hci_read_inquiry_scan_activity(int s, int argc, char **argv)
{
	ng_hci_read_inquiry_scan_activity_rp	rp;
	int					n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_INQUIRY_SCAN_ACTIVITY),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n",
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	rp.inquiry_scan_interval = le16toh(rp.inquiry_scan_interval);
	rp.inquiry_scan_window = le16toh(rp.inquiry_scan_window);

	fprintf(stdout, "Inquiry Scan Interval: %.2f msec [%d slots]\n",
		rp.inquiry_scan_interval * 0.625, rp.inquiry_scan_interval);
	fprintf(stdout, "Inquiry Scan Window: %.2f msec [%d slots]\n",
		rp.inquiry_scan_window * 0.625, rp.inquiry_scan_interval);

	return (OK);
} /* hci_read_inquiry_scan_activity */

/* Send Write_Inquiry_Scan_Activity command to the unit */
static int
hci_write_inquiry_scan_activity(int s, int argc, char **argv)
{
	ng_hci_write_inquiry_scan_activity_cp	cp;
	ng_hci_write_inquiry_scan_activity_rp	rp;
	int					n;

	/* parse command parameters */
	switch (argc) {
	case 2:
		/* inquiry scan interval */
		if (sscanf(argv[0], "%d", &n) != 1 || n < 0x12 || n > 0x1000)
			return (USAGE);

		cp.inquiry_scan_interval = (uint16_t) n;

		/* inquiry scan window */
		if (sscanf(argv[1], "%d", &n) != 1 || n < 0x12 || n > 0x1000)
			return (USAGE);

		cp.inquiry_scan_window = (uint16_t) n;

		if (cp.inquiry_scan_window > cp.inquiry_scan_interval)
			return (USAGE);

		cp.inquiry_scan_interval = 
			htole16(cp.inquiry_scan_interval);
		cp.inquiry_scan_window = htole16(cp.inquiry_scan_window);
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_WRITE_INQUIRY_SCAN_ACTIVITY),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n",
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_inquiry_scan_activity */

/* Send Read_Authentication_Enable command to the unit */
static int
hci_read_authentication_enable(int s, int argc, char **argv)
{
	ng_hci_read_auth_enable_rp	rp;
	int				n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_AUTH_ENABLE),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n",
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Authentication Enable: %s [%d]\n",
		rp.auth_enable? "Enabled" : "Disabled", rp.auth_enable);

	return (OK);
} /* hci_read_authentication_enable */

/* Send Write_Authentication_Enable command to the unit */
static int
hci_write_authentication_enable(int s, int argc, char **argv)
{
	ng_hci_write_auth_enable_cp	cp;
	ng_hci_write_auth_enable_rp	rp;
	int				n;

	/* parse command parameters */
	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%d", &n) != 1 || n < 0 || n > 1)
			return (USAGE);

		cp.auth_enable = (uint8_t) n;
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_WRITE_AUTH_ENABLE),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_authentication_enable */

/* Send Read_Encryption_Mode command to the unit */
static int
hci_read_encryption_mode(int s, int argc, char **argv)
{
	ng_hci_read_encryption_mode_rp	rp;
	int				n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_ENCRYPTION_MODE),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Encryption mode: %s [%#02x]\n", 
		hci_encrypt2str(rp.encryption_mode, 0), rp.encryption_mode);

	return (OK);
} /* hci_read_encryption_mode */

/* Send Write_Encryption_Mode command to the unit */
static int
hci_write_encryption_mode(int s, int argc, char **argv)
{
	ng_hci_write_encryption_mode_cp	cp;
	ng_hci_write_encryption_mode_rp	rp;
	int				n;

	/* parse command parameters */
	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%d", &n) != 1 || n < 0 || n > 2)
			return (USAGE);

		cp.encryption_mode = (uint8_t) n;
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_WRITE_ENCRYPTION_MODE),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_encryption_mode */

/* Send Read_Class_Of_Device command to the unit */
static int
hci_read_class_of_device(int s, int argc, char **argv)
{
	ng_hci_read_unit_class_rp	rp;
	int				n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_UNIT_CLASS),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Class: %02x:%02x:%02x\n",
		rp.uclass[2], rp.uclass[1], rp.uclass[0]);

	return (0);
} /* hci_read_class_of_device */

/* Send Write_Class_Of_Device command to the unit */
static int
hci_write_class_of_device(int s, int argc, char **argv)
{
	ng_hci_write_unit_class_cp	cp;
	ng_hci_write_unit_class_rp	rp;
	int				n0, n1, n2;

	/* parse command parameters */
	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%x:%x:%x", &n2, &n1, &n0) != 3)
			return (USAGE);

		cp.uclass[0] = (n0 & 0xff);
		cp.uclass[1] = (n1 & 0xff);
		cp.uclass[2] = (n2 & 0xff);
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n0 = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_WRITE_UNIT_CLASS),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n0) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_class_of_device */

/* Send Read_Voice_Settings command to the unit */
static int
hci_read_voice_settings(int s, int argc, char **argv)
{
	ng_hci_read_voice_settings_rp	rp;
	int				n,
					input_coding,
					input_data_format,
					input_sample_size;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_VOICE_SETTINGS),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	rp.settings = le16toh(rp.settings);

	input_coding      = (rp.settings & 0x0300) >> 8;
	input_data_format = (rp.settings & 0x00c0) >> 6;
	input_sample_size = (rp.settings & 0x0020) >> 5;

	fprintf(stdout, "Voice settings: %#04x\n", rp.settings);
	fprintf(stdout, "Input coding: %s [%d]\n",
		hci_coding2str(input_coding), input_coding);
	fprintf(stdout, "Input data format: %s [%d]\n",
		hci_vdata2str(input_data_format), input_data_format);

	if (input_coding == 0x00) /* Only for Linear PCM */
		fprintf(stdout, "Input sample size: %d bit [%d]\n",
			input_sample_size? 16 : 8, input_sample_size);

	return (OK);
} /* hci_read_voice_settings */

/* Send Write_Voice_Settings command to the unit */
static int
hci_write_voice_settings(int s, int argc, char **argv)
{
	ng_hci_write_voice_settings_cp	cp;
	ng_hci_write_voice_settings_rp	rp;
	int				n;

	/* parse command parameters */
	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%x", &n) != 1)
			return (USAGE);

		cp.settings = (uint16_t) n;
		cp.settings = htole16(cp.settings);
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_WRITE_VOICE_SETTINGS),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_voice_settings */

/* Send Read_Number_Broadcast_Restransmissions */
static int
hci_read_number_broadcast_retransmissions(int s, int argc, char **argv)
{
	ng_hci_read_num_broadcast_retrans_rp	rp;
	int					n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_NUM_BROADCAST_RETRANS),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Number of broadcast retransmissions: %d\n",
		rp.counter);

	return (OK);
} /* hci_read_number_broadcast_retransmissions */

/* Send Write_Number_Broadcast_Restransmissions */
static int
hci_write_number_broadcast_retransmissions(int s, int argc, char **argv)
{
	ng_hci_write_num_broadcast_retrans_cp	cp;
	ng_hci_write_num_broadcast_retrans_rp	rp;
	int					n;

	/* parse command parameters */
	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%d", &n) != 1 || n < 0 || n > 0xff)
			return (USAGE);

		cp.counter = (uint8_t) n;
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_WRITE_NUM_BROADCAST_RETRANS),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_number_broadcast_retransmissions */

/* Send Read_Hold_Mode_Activity command to the unit */
static int
hci_read_hold_mode_activity(int s, int argc, char **argv)
{
	ng_hci_read_hold_mode_activity_rp	rp;
	int					n;
	char					buffer[1024];

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_HOLD_MODE_ACTIVITY),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Hold Mode Activities: %#02x\n", rp.hold_mode_activity);
	if (rp.hold_mode_activity == 0)
		fprintf(stdout, "Maintain current Power State");
	else
		fprintf(stdout, "%s", hci_hmode2str(rp.hold_mode_activity,
				buffer, sizeof(buffer)));

	fprintf(stdout, "\n");

	return (OK);
} /* hci_read_hold_mode_activity */

/* Send Write_Hold_Mode_Activity command to the unit */
static int
hci_write_hold_mode_activity(int s, int argc, char **argv)
{
	ng_hci_write_hold_mode_activity_cp	cp;
	ng_hci_write_hold_mode_activity_rp	rp;
	int					n;

	/* parse command parameters */
	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%d", &n) != 1 || n < 0 || n > 4)
			return (USAGE);

		cp.hold_mode_activity = (uint8_t) n;
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_WRITE_HOLD_MODE_ACTIVITY),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_hold_mode_activity */

/* Send Read_SCO_Flow_Control_Enable command to the unit */
static int
hci_read_sco_flow_control_enable(int s, int argc, char **argv)
{
	ng_hci_read_sco_flow_control_rp	rp;
	int				n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_SCO_FLOW_CONTROL),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "SCO flow control %s [%d]\n",
		rp.flow_control? "enabled" : "disabled", rp.flow_control);

	return (OK);
} /* hci_read_sco_flow_control_enable */

/* Send Write_SCO_Flow_Control_Enable command to the unit */
static int
hci_write_sco_flow_control_enable(int s, int argc, char **argv)
{
	ng_hci_write_sco_flow_control_cp	cp;
	ng_hci_write_sco_flow_control_rp	rp;
	int					n;

	/* parse command parameters */
	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%d", &n) != 1 || n < 0 || n > 1)
			return (USAGE);

		cp.flow_control = (uint8_t) n;
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_WRITE_SCO_FLOW_CONTROL),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_sco_flow_control_enable */

/* Send Read_Link_Supervision_Timeout command to the unit */
static int
hci_read_link_supervision_timeout(int s, int argc, char **argv)
{
	ng_hci_read_link_supervision_timo_cp	cp;
	ng_hci_read_link_supervision_timo_rp	rp;
	int					n;

	switch (argc) {
	case 1:
		/* connection handle */
		if (sscanf(argv[0], "%d", &n) != 1 || n <= 0 || n > 0x0eff)
			return (USAGE);

		cp.con_handle = (uint16_t) (n & 0x0fff);
		cp.con_handle = htole16(cp.con_handle);
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_LINK_SUPERVISION_TIMO),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	rp.timeout = le16toh(rp.timeout);

	fprintf(stdout, "Connection handle: %d\n", le16toh(rp.con_handle));
	fprintf(stdout, "Link supervision timeout: %.2f msec [%d slots]\n",
		rp.timeout * 0.625, rp.timeout);

	return (OK);
} /* hci_read_link_supervision_timeout */

/* Send Write_Link_Supervision_Timeout command to the unit */
static int
hci_write_link_supervision_timeout(int s, int argc, char **argv)
{
	ng_hci_write_link_supervision_timo_cp	cp;
	ng_hci_write_link_supervision_timo_rp	rp;
	int					n;

	switch (argc) {
	case 2:
		/* connection handle */
		if (sscanf(argv[0], "%d", &n) != 1 || n <= 0 || n > 0x0eff)
			return (USAGE);

		cp.con_handle = (uint16_t) (n & 0x0fff);
		cp.con_handle = htole16(cp.con_handle);

		/* link supervision timeout */
		if (sscanf(argv[1], "%d", &n) != 1 || n < 0 || n > 0xffff)
			return (USAGE);

		cp.timeout = (uint16_t) (n & 0x0fff);
		cp.timeout = htole16(cp.timeout);
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_WRITE_LINK_SUPERVISION_TIMO),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_link_supervision_timeout */

/* Send Read_Page_Scan_Period_Mode command to the unit */
static int
hci_read_page_scan_period_mode(int s, int argc, char **argv)
{
	ng_hci_read_page_scan_period_rp	rp;
	int				n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_PAGE_SCAN_PERIOD),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Page scan period mode: %#02x\n",
		rp.page_scan_period_mode);

	return (OK);
} /* hci_read_page_scan_period_mode */

/* Send Write_Page_Scan_Period_Mode command to the unit */
static int
hci_write_page_scan_period_mode(int s, int argc, char **argv)
{
	ng_hci_write_page_scan_period_cp	cp;
	ng_hci_write_page_scan_period_rp	rp;
	int					n;

	/* parse command arguments */
	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%d", &n) != 1 || n < 0 || n > 2)
			return (USAGE);
	
		cp.page_scan_period_mode = (n & 0xff);
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_WRITE_PAGE_SCAN_PERIOD),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_page_scan_period_mode */

/* Send Read_Page_Scan_Mode command to the unit */
static int
hci_read_page_scan_mode(int s, int argc, char **argv)
{
	ng_hci_read_page_scan_rp	rp;
	int				n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_PAGE_SCAN),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Page scan mode: %#02x\n", rp.page_scan_mode);

	return (OK);
} /* hci_read_page_scan_mode */

/* Send Write_Page_Scan_Mode command to the unit */
static int
hci_write_page_scan_mode(int s, int argc, char **argv)
{
	ng_hci_write_page_scan_cp	cp;
	ng_hci_write_page_scan_rp	rp;
	int				n;

	/* parse command arguments */
	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%d", &n) != 1 || n < 0 || n > 3)
			return (USAGE);
	
		cp.page_scan_mode = (n & 0xff);
		break;

	default:
		return (USAGE);
	}

	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_WRITE_PAGE_SCAN),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_page_scan_mode */

static int
hci_read_le_host_supported_command(int s, int argc, char **argv) 
{
	ng_hci_read_le_host_supported_rp rp;
	int n;
	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_READ_LE_HOST_SUPPORTED),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "LE Host support: %#02x\n", rp.le_supported_host);
	fprintf(stdout, "Simulateneouse LE Host : %#02x\n", rp.simultaneous_le_host);

	return (OK);
	
}
static int
hci_write_le_host_supported_command(int s, int argc, char **argv) 
{
	ng_hci_write_le_host_supported_cp cp;
	ng_hci_write_le_host_supported_rp rp;

	int n;

	cp.le_supported_host = 0;
	cp.simultaneous_le_host = 0;
	switch (argc) {
	case 2:
		if (sscanf(argv[1], "%d", &n) != 1 || (n != 0 && n != 1)){
			printf("ARGC2: %d\n", n);
			return (USAGE);
		}
		cp.simultaneous_le_host = (n &1);
		
	case 1:
		if (sscanf(argv[0], "%d", &n) != 1 || (n != 0 && n != 1)){
			printf("ARGC1: %d\n", n);
			return (USAGE);
		}

		cp.le_supported_host = (n &1);
		break;

	default:
		return (USAGE);
	}


	/* send command */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_HC_BASEBAND,
			NG_HCI_OCF_WRITE_LE_HOST_SUPPORTED),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
}

struct hci_command	host_controller_baseband_commands[] = {
{
"reset",
"\nThe Reset command will reset the Host Controller and the Link Manager.\n" \
"After the reset is completed, the current operational state will be lost,\n" \
"the Bluetooth unit will enter standby mode and the Host Controller will\n" \
"automatically revert to the default values for the parameters for which\n" \
"default values are defined in the specification.",
&hci_reset
},
{
"read_pin_type",
"\nThe Read_PIN_Type command is used for the Host to read whether the Link\n" \
"Manager assumes that the Host supports variable PIN codes only a fixed PIN\n" \
"code.",
&hci_read_pin_type
},
{
"write_pin_type <pin_type>",
"\nThe Write_PIN_Type command is used for the Host to write to the Host\n" \
"Controller whether the Host supports variable PIN codes or only a fixed PIN\n"\
"code.\n\n" \
"\t<pin_type> - dd; 0 - Variable; 1 - Fixed",
&hci_write_pin_type
},
{
"read_stored_link_key [<BD_ADDR>]",
"\nThe Read_Stored_Link_Key command provides the ability to read one or\n" \
"more link keys stored in the Bluetooth Host Controller. The Bluetooth Host\n" \
"Controller can store a limited number of link keys for other Bluetooth\n" \
"devices.\n\n" \
"\t<BD_ADDR> - xx:xx:xx:xx:xx:xx BD_ADDR or name",
&hci_read_stored_link_key
},
{
"write_stored_link_key <BD_ADDR> <key>",
"\nThe Write_Stored_Link_Key command provides the ability to write one\n" \
"or more link keys to be stored in the Bluetooth Host Controller. The\n" \
"Bluetooth Host Controller can store a limited number of link keys for other\n"\
"Bluetooth devices. If no additional space is available in the Bluetooth\n"\
"Host Controller then no additional link keys will be stored.\n\n" \
"\t<BD_ADDR> - xx:xx:xx:xx:xx:xx BD_ADDR or name\n" \
"\t<key>     - xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx up to 16 bytes link key",
&hci_write_stored_link_key
},
{
"delete_stored_link_key [<BD_ADDR>]",
"\nThe Delete_Stored_Link_Key command provides the ability to remove one\n" \
"or more of the link keys stored in the Bluetooth Host Controller. The\n" \
"Bluetooth Host Controller can store a limited number of link keys for other\n"\
"Bluetooth devices.\n\n" \
"\t<BD_ADDR> - xx:xx:xx:xx:xx:xx BD_ADDR or name",
&hci_delete_stored_link_key
},
{
"change_local_name <name>",
"\nThe Change_Local_Name command provides the ability to modify the user\n" \
"friendly name for the Bluetooth unit.\n\n" \
"\t<name> - string",
&hci_change_local_name
},
{
"read_local_name",
"\nThe Read_Local_Name command provides the ability to read the\n" \
"stored user-friendly name for the Bluetooth unit.",
&hci_read_local_name
},
{
"read_connection_accept_timeout",
"\nThis command will read the value for the Connection_Accept_Timeout\n" \
"configuration parameter. The Connection_Accept_Timeout configuration\n" \
"parameter allows the Bluetooth hardware to automatically deny a\n" \
"connection request after a specified time period has occurred and\n" \
"the new connection is not accepted. Connection Accept Timeout\n" \
"measured in Number of Baseband slots.",
&hci_read_connection_accept_timeout
},
{
"write_connection_accept_timeout <timeout>",
"\nThis command will write the value for the Connection_Accept_Timeout\n" \
"configuration parameter.\n\n" \
"\t<timeout> - dddd; measured in number of baseband slots.",
&hci_write_connection_accept_timeout
},
{
"read_page_timeout",
"\nThis command will read the value for the Page_Timeout configuration\n" \
"parameter. The Page_Timeout configuration parameter defines the\n" \
"maximum time the local Link Manager will wait for a baseband page\n" \
"response from the remote unit at a locally initiated connection\n" \
"attempt. Page Timeout measured in Number of Baseband slots.",
&hci_read_page_timeout
},
{
"write_page_timeout <timeout>",
"\nThis command will write the value for the Page_Timeout configuration\n" \
"parameter.\n\n" \
"\t<timeout> - dddd; measured in number of baseband slots.",
&hci_write_page_timeout
},
{
"read_scan_enable",
"\nThis command will read the value for the Scan_Enable parameter. The\n" \
"Scan_Enable parameter controls whether or not the Bluetooth uint\n" \
"will periodically scan for page attempts and/or inquiry requests\n" \
"from other Bluetooth unit.\n\n" \
"\t0x00 - No Scans enabled.\n" \
"\t0x01 - Inquiry Scan enabled. Page Scan disabled.\n" \
"\t0x02 - Inquiry Scan disabled. Page Scan enabled.\n" \
"\t0x03 - Inquiry Scan enabled. Page Scan enabled.",
&hci_read_scan_enable
},
{
"write_scan_enable <scan_enable>",
"\nThis command will write the value for the Scan_Enable parameter.\n" \
"The Scan_Enable parameter controls whether or not the Bluetooth\n" \
"unit will periodically scan for page attempts and/or inquiry\n" \
"requests from other Bluetooth unit.\n\n" \
"\t<scan_enable> - dd;\n" \
"\t0 - No Scans enabled.\n" \
"\t1 - Inquiry Scan enabled. Page Scan disabled.\n" \
"\t2 - Inquiry Scan disabled. Page Scan enabled.\n" \
"\t3 - Inquiry Scan enabled. Page Scan enabled.",
&hci_write_scan_enable
},
{
"read_page_scan_activity",
"\nThis command will read the value for Page_Scan_Activity configuration\n" \
"parameters. The Page_Scan_Interval configuration parameter defines the\n" \
"amount of time between consecutive page scans. This time interval is \n" \
"defined from when the Host Controller started its last page scan until\n" \
"it begins the next page scan. The Page_Scan_Window configuration parameter\n" \
"defines the amount of time for the duration of the page scan. The\n" \
"Page_Scan_Window can only be less than or equal to the Page_Scan_Interval.",
&hci_read_page_scan_activity
},
{
"write_page_scan_activity interval(dddd) window(dddd)",
"\nThis command will write the value for Page_Scan_Activity configuration\n" \
"parameter. The Page_Scan_Interval configuration parameter defines the\n" \
"amount of time between consecutive page scans. This is defined as the time\n" \
"interval from when the Host Controller started its last page scan until it\n" \
"begins the next page scan. The Page_Scan_Window configuration parameter\n" \
"defines the amount of time for the duration of the page scan. \n" \
"The Page_Scan_Window can only be less than or equal to the Page_Scan_Interval.\n\n" \
"\t<interval> - Range: 0x0012 -- 0x100, Time = N * 0.625 msec\n" \
"\t<window>   - Range: 0x0012 -- 0x100, Time = N * 0.625 msec",
&hci_write_page_scan_activity
},
{
"read_inquiry_scan_activity",
"\nThis command will read the value for Inquiry_Scan_Activity configuration\n" \
"parameter. The Inquiry_Scan_Interval configuration parameter defines the\n" \
"amount of time between consecutive inquiry scans. This is defined as the\n" \
"time interval from when the Host Controller started its last inquiry scan\n" \
"until it begins the next inquiry scan.",
&hci_read_inquiry_scan_activity
},
{
"write_inquiry_scan_activity interval(dddd) window(dddd)",
"\nThis command will write the value for Inquiry_Scan_Activity configuration\n"\
"parameter. The Inquiry_Scan_Interval configuration parameter defines the\n" \
"amount of time between consecutive inquiry scans. This is defined as the\n" \
"time interval from when the Host Controller started its last inquiry scan\n" \
"until it begins the next inquiry scan. The Inquiry_Scan_Window configuration\n" \
"parameter defines the amount of time for the duration of the inquiry scan.\n" \
"The Inquiry_Scan_Window can only be less than or equal to the Inquiry_Scan_Interval.\n\n" \
"\t<interval> - Range: 0x0012 -- 0x100, Time = N * 0.625 msec\n" \
"\t<window>   - Range: 0x0012 -- 0x100, Time = N * 0.625 msec",
&hci_write_inquiry_scan_activity
},
{
"read_authentication_enable",
"\nThis command will read the value for the Authentication_Enable parameter.\n"\
"The Authentication_Enable parameter controls if the local unit requires\n"\
"to authenticate the remote unit at connection setup (between the\n" \
"Create_Connection command or acceptance of an incoming ACL connection\n"\
"and the corresponding Connection Complete event). At connection setup, only\n"\
"the unit(s) with the Authentication_Enable parameter enabled will try to\n"\
"authenticate the other unit.",
&hci_read_authentication_enable
},
{
"write_authentication_enable enable(0|1)",
"\nThis command will write the value for the Authentication_Enable parameter.\n"\
"The Authentication_Enable parameter controls if the local unit requires to\n"\
"authenticate the remote unit at connection setup (between the\n" \
"Create_Connection command or acceptance of an incoming ACL connection\n" \
"and the corresponding Connection Complete event). At connection setup, only\n"\
"the unit(s) with the Authentication_Enable parameter enabled will try to\n"\
"authenticate the other unit.",
&hci_write_authentication_enable
},
{
"read_encryption_mode",
"\nThis command will read the value for the Encryption_Mode parameter. The\n" \
"Encryption_Mode parameter controls if the local unit requires encryption\n" \
"to the remote unit at connection setup (between the Create_Connection\n" \
"command or acceptance of an incoming ACL connection and the corresponding\n" \
"Connection Complete event). At connection setup, only the unit(s) with\n" \
"the Authentication_Enable parameter enabled and Encryption_Mode parameter\n" \
"enabled will try to encrypt the connection to the other unit.\n\n" \
"\t<encryption_mode>:\n" \
"\t0x00 - Encryption disabled.\n" \
"\t0x01 - Encryption only for point-to-point packets.\n" \
"\t0x02 - Encryption for both point-to-point and broadcast packets.",
&hci_read_encryption_mode
},
{
"write_encryption_mode mode(0|1|2)",
"\tThis command will write the value for the Encryption_Mode parameter.\n" \
"The Encryption_Mode parameter controls if the local unit requires\n" \
"encryption to the remote unit at connection setup (between the\n" \
"Create_Connection command or acceptance of an incoming ACL connection\n" \
"and the corresponding Connection Complete event). At connection setup,\n" \
"only the unit(s) with the Authentication_Enable parameter enabled and\n" \
"Encryption_Mode parameter enabled will try to encrypt the connection to\n" \
"the other unit.\n\n" \
"\t<encryption_mode> (dd)\n" \
"\t0 - Encryption disabled.\n" \
"\t1 - Encryption only for point-to-point packets.\n" \
"\t2 - Encryption for both point-to-point and broadcast packets.",
&hci_write_encryption_mode
},
{
"read_class_of_device",
"\nThis command will read the value for the Class_of_Device parameter.\n" \
"The Class_of_Device parameter is used to indicate the capabilities of\n" \
"the local unit to other units.",
&hci_read_class_of_device
},
{
"write_class_of_device class(xx:xx:xx)",
"\nThis command will write the value for the Class_of_Device parameter.\n" \
"The Class_of_Device parameter is used to indicate the capabilities of \n" \
"the local unit to other units.\n\n" \
"\t<class> (xx:xx:xx) - class of device",
&hci_write_class_of_device
},
{
"read_voice_settings",
"\nThis command will read the values for the Voice_Setting parameter.\n" \
"The Voice_Setting parameter controls all the various settings for voice\n" \
"connections. These settings apply to all voice connections, and cannot be\n" \
"set for individual voice connections. The Voice_Setting parameter controls\n" \
"the configuration for voice connections: Input Coding, Air coding format,\n" \
"input data format, Input sample size, and linear PCM parameter.",
&hci_read_voice_settings
},
{
"write_voice_settings settings(xxxx)",
"\nThis command will write the values for the Voice_Setting parameter.\n" \
"The Voice_Setting parameter controls all the various settings for voice\n" \
"connections. These settings apply to all voice connections, and cannot be\n" \
"set for individual voice connections. The Voice_Setting parameter controls\n" \
"the configuration for voice connections: Input Coding, Air coding format,\n" \
"input data format, Input sample size, and linear PCM parameter.\n\n" \
"\t<voice_settings> (xxxx) - voice settings",
&hci_write_voice_settings
},
{
"read_number_broadcast_retransmissions",
"\nThis command will read the unit's parameter value for the Number of\n" \
"Broadcast Retransmissions. Broadcast packets are not acknowledged and are\n" \
"unreliable.",
&hci_read_number_broadcast_retransmissions
},
{
"write_number_broadcast_retransmissions count(dd)",
"\nThis command will write the unit's parameter value for the Number of\n" \
"Broadcast Retransmissions. Broadcast packets are not acknowledged and are\n" \
"unreliable.\n\n" \
"\t<count> (dd) - number of broadcast retransimissions",
&hci_write_number_broadcast_retransmissions
},
{
"read_hold_mode_activity",
"\nThis command will read the value for the Hold_Mode_Activity parameter.\n" \
"The Hold_Mode_Activity value is used to determine what activities should\n" \
"be suspended when the unit is in hold mode.",
&hci_read_hold_mode_activity
},
{
"write_hold_mode_activity settings(0|1|2|4)",
"\nThis command will write the value for the Hold_Mode_Activity parameter.\n" \
"The Hold_Mode_Activity value is used to determine what activities should\n" \
"be suspended when the unit is in hold mode.\n\n" \
"\t<settings> (dd) - bit mask:\n" \
"\t0 - Maintain current Power State. Default\n" \
"\t1 - Suspend Page Scan.\n" \
"\t2 - Suspend Inquiry Scan.\n" \
"\t4 - Suspend Periodic Inquiries.",
&hci_write_hold_mode_activity
},
{
"read_sco_flow_control_enable",
"\nThe Read_SCO_Flow_Control_Enable command provides the ability to read\n" \
"the SCO_Flow_Control_Enable setting. By using this setting, the Host can\n" \
"decide if the Host Controller will send Number Of Completed Packets events\n" \
"for SCO Connection Handles. This setting allows the Host to enable and\n" \
"disable SCO flow control.",
&hci_read_sco_flow_control_enable
},
{
"write_sco_flow_control_enable enable(0|1)",
"\nThe Write_SCO_Flow_Control_Enable command provides the ability to write\n" \
"the SCO_Flow_Control_Enable setting. By using this setting, the Host can\n" \
"decide if the Host Controller will send Number Of Completed Packets events\n" \
"for SCO Connection Handles. This setting allows the Host to enable and\n" \
"disable SCO flow control. The SCO_Flow_Control_Enable setting can only be\n" \
"changed if no connections exist.",
&hci_write_sco_flow_control_enable
},
{
"read_link_supervision_timeout <connection_handle>",
"\nThis command will read the value for the Link_Supervision_Timeout\n" \
"parameter for the device. The Link_Supervision_Timeout parameter is used\n" \
"by the master or slave Bluetooth device to monitor link loss. If, for any\n" \
"reason, no Baseband packets are received from that Connection Handle for a\n" \
"duration longer than the Link_Supervision_Timeout, the connection is\n"
"disconnected.\n\n" \
"\t<connection_handle> - dddd; connection handle\n",
&hci_read_link_supervision_timeout
},
{
"write_link_supervision_timeout <connection_handle> <timeout>",
"\nThis command will write the value for the Link_Supervision_Timeout\n" \
"parameter for the device. The Link_Supervision_Timeout parameter is used\n" \
"by the master or slave Bluetooth device to monitor link loss. If, for any\n" \
"reason, no Baseband packets are received from that connection handle for a\n" \
"duration longer than the Link_Supervision_Timeout, the connection is\n" \
"disconnected.\n\n" \
"\t<connection_handle> - dddd; connection handle\n" \
"\t<timeout>           - dddd; timeout measured in number of baseband slots\n",
&hci_write_link_supervision_timeout
},
{
"read_page_scan_period_mode",
"\nThis command is used to read the mandatory Page_Scan_Period_Mode of the\n" \
"local Bluetooth device. Every time an inquiry response message is sent, the\n"\
"Bluetooth device will start a timer (T_mandatory_pscan), the value of which\n"\
"is dependent on the Page_Scan_Period_Mode. As long as this timer has not\n" \
"expired, the Bluetooth device will use the Page_Scan_Period_Mode for all\n" \
"following page scans.",
&hci_read_page_scan_period_mode
},
{
"write_page_scan_period_mode <page_scan_period_mode>",
"\nThis command is used to write the mandatory Page_Scan_Period_Mode of the\n" \
"local Bluetooth device. Every time an inquiry response message is sent, the\n"\
"Bluetooth device will start a timer (T_mandatory_pscan), the value of which\n"\
"is dependent on the Page_Scan_Period_Mode. As long as this timer has not\n" \
"expired, the Bluetooth device will use the Page_Scan_Period_Mode for all\n" \
"following page scans.\n\n" \
"\t<page_scan_period_mode> - dd; page scan period mode:\n" \
"\t0x00 - P0 (Default)\n" \
"\t0x01 - P1\n" \
"\t0x02 - P2",
&hci_write_page_scan_period_mode
},
{
"read_page_scan_mode",
"\nThis command is used to read the default page scan mode of the local\n" \
"Bluetooth device. The Page_Scan_Mode parameter indicates the page scan mode\n"\
"that is used for the default page scan. Currently one mandatory page scan\n"\
"mode and three optional page scan modes are defined. Following an inquiry\n" \
"response, if the Baseband timer T_mandatory_pscan has not expired, the\n" \
"mandatory page scan mode must be applied.",
&hci_read_page_scan_mode
},
{
"write_page_scan_mode <page_scan_mode>",
"\nThis command is used to write the default page scan mode of the local\n" \
"Bluetooth device. The Page_Scan_Mode parameter indicates the page scan mode\n"\
"that is used for the default page scan. Currently, one mandatory page scan\n"\
"mode and three optional page scan modes are defined. Following an inquiry\n"\
"response, if the Baseband timer T_mandatory_pscan has not expired, the\n" \
"mandatory page scan mode must be applied.\n\n" \
"\t<page_scan_mode> - dd; page scan mode:\n" \
"\t0x00 - Mandatory Page Scan Mode (Default)\n" \
"\t0x01 - Optional Page Scan Mode I\n" \
"\t0x02 - Optional Page Scan Mode II\n" \
"\t0x03 - Optional Page Scan Mode III",
&hci_write_page_scan_mode
},
{
"read_le_host_supported_command",	\
"Read if this host is in le supported mode and stimulatenouse le supported mode",
&hci_read_le_host_supported_command,
},  
{
"write_le_host_supported_command",	\
"write_le_host_supported_command le_host[0|1] stimultajeous_le[0|1]",
&hci_write_le_host_supported_command,
},  

{ NULL, }
};

