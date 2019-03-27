/*-
 * link_control.c
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
 * $Id: link_control.c,v 1.4 2003/08/18 19:19:54 max Exp $
 * $FreeBSD$
 */

#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "hccontrol.h"

static void hci_inquiry_response (int n, uint8_t **b);

/* Send Inquiry command to the unit */
static int
hci_inquiry(int s, int argc, char **argv)
{
	int			 n0, n1, n2, timo;
	char			 b[512];
	ng_hci_inquiry_cp	 cp;
	ng_hci_event_pkt_t	*e = (ng_hci_event_pkt_t *) b;

	/* set defaults */
	cp.lap[2] = 0x9e;
	cp.lap[1] = 0x8b;
	cp.lap[0] = 0x33;
	cp.inquiry_length = 5;
	cp.num_responses = 8;

	/* parse command parameters */
	switch (argc) {
	case 3:
		/* number of responses, range 0x00 - 0xff */
		if (sscanf(argv[2], "%d", &n0) != 1 || n0 < 0 || n0 > 0xff)
			return (USAGE);

		cp.num_responses = (n0 & 0xff);

	case 2:
		/* inquiry length (N * 1.28) sec, range 0x01 - 0x30 */
		if (sscanf(argv[1], "%d", &n0) != 1 || n0 < 0x1 || n0 > 0x30)
			return (USAGE);

		cp.inquiry_length = (n0 & 0xff);

	case 1:
		/* LAP */
		if (sscanf(argv[0], "%x:%x:%x", &n2, &n1, &n0) != 3)
			return (USAGE);

		cp.lap[0] = (n0 & 0xff);
		cp.lap[1] = (n1 & 0xff);
		cp.lap[2] = (n2 & 0xff);

	case 0:
		/* use defaults */
		break;

	default:
		return (USAGE);
	}

	/* send request and expect status back */
	n0 = sizeof(b);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_LINK_CONTROL,
			NG_HCI_OCF_INQUIRY), (char const *) &cp, sizeof(cp),
			b, &n0) == ERROR)
		return (ERROR);

	if (*b != 0x00)
		return (FAILED);

	timo = timeout;
	timeout = cp.inquiry_length * 1.28 + 1;

wait_for_more:
	/* wait for inquiry events */
	n0 = sizeof(b);
	if (hci_recv(s, b, &n0) == ERROR) {
		timeout = timo;
		return (ERROR);
	}

	if (n0 < sizeof(*e)) {
		timeout = timo;
		errno = EIO;
		return (ERROR);
	}

	switch (e->event) {
	case NG_HCI_EVENT_INQUIRY_RESULT: {
		ng_hci_inquiry_result_ep	*ir = 
				(ng_hci_inquiry_result_ep *)(e + 1);
		uint8_t				*r = (uint8_t *)(ir + 1);

		fprintf(stdout, "Inquiry result, num_responses=%d\n",
			ir->num_responses);

		for (n0 = 0; n0 < ir->num_responses; n0++)
			hci_inquiry_response(n0, &r);

		goto wait_for_more;
		}

	case NG_HCI_EVENT_INQUIRY_COMPL:
		fprintf(stdout, "Inquiry complete. Status: %s [%#02x]\n",
			hci_status2str(*(b + sizeof(*e))), *(b + sizeof(*e)));
		break;

	default:
		goto wait_for_more;
	}

	timeout = timo;

	return (OK);
} /* hci_inquiry */

/* Print Inquiry_Result event */
static void
hci_inquiry_response(int n, uint8_t **b)
{
	ng_hci_inquiry_response	*ir = (ng_hci_inquiry_response *)(*b);

	fprintf(stdout, "Inquiry result #%d\n", n);
	fprintf(stdout, "\tBD_ADDR: %s\n", hci_bdaddr2str(&ir->bdaddr));
	fprintf(stdout, "\tPage Scan Rep. Mode: %#02x\n",
		ir->page_scan_rep_mode);
	fprintf(stdout, "\tPage Scan Period Mode: %#02x\n",
		ir->page_scan_period_mode);
	fprintf(stdout, "\tPage Scan Mode: %#02x\n",
		ir->page_scan_mode);
	fprintf(stdout, "\tClass: %02x:%02x:%02x\n",
		ir->uclass[2], ir->uclass[1], ir->uclass[0]);
	fprintf(stdout, "\tClock offset: %#04x\n",
		le16toh(ir->clock_offset));

	*b += sizeof(*ir);
} /* hci_inquiry_response */

/* Send Create_Connection command to the unit */
static int
hci_create_connection(int s, int argc, char **argv)
{
	int			 n0;
	char			 b[512];
	ng_hci_create_con_cp	 cp;
	ng_hci_event_pkt_t	*e = (ng_hci_event_pkt_t *) b; 

	/* Set defaults */
	memset(&cp, 0, sizeof(cp));
	cp.pkt_type = htole16(	NG_HCI_PKT_DM1 | NG_HCI_PKT_DH1 |
				NG_HCI_PKT_DM3 | NG_HCI_PKT_DH3 |
				NG_HCI_PKT_DM5);
	cp.page_scan_rep_mode = NG_HCI_SCAN_REP_MODE0;
	cp.page_scan_mode = NG_HCI_MANDATORY_PAGE_SCAN_MODE;
	cp.clock_offset = 0;
	cp.accept_role_switch = 1;

	/* parse command parameters */
	switch (argc) {
	case 6:
		/* accept role switch */
		if (sscanf(argv[5], "%d", &n0) != 1)
			return (USAGE);

		cp.accept_role_switch = n0 ? 1 : 0;

	case 5:
		/* clock offset */
		if (sscanf(argv[4], "%d", &n0) != 1)
			return (USAGE);

		cp.clock_offset = (n0 & 0xffff);
		cp.clock_offset = htole16(cp.clock_offset);

	case 4:
		/* page scan mode */
		if (sscanf(argv[3], "%d", &n0) != 1 || n0 < 0 || n0 > 3)
			return (USAGE);

		cp.page_scan_mode = (n0 & 0xff);

	case 3:
		/* page scan rep mode */
		if (sscanf(argv[2], "%d", &n0) != 1 || n0 < 0 || n0 > 2)
			return (USAGE);

		cp.page_scan_rep_mode = (n0 & 0xff);

	case 2:
		/* packet type */
		if (sscanf(argv[1], "%x", &n0) != 1)
			return (USAGE);

		n0 &= (	NG_HCI_PKT_DM1 | NG_HCI_PKT_DH1 |
			NG_HCI_PKT_DM3 | NG_HCI_PKT_DH3 |
			NG_HCI_PKT_DM5);
		if (n0 == 0)
			return (USAGE);

		cp.pkt_type = (n0 & 0xffff);
		cp.pkt_type = htole16(cp.pkt_type);

	case 1:
		/* BD_ADDR */
		if (!bt_aton(argv[0], &cp.bdaddr)) {
			struct hostent	*he = NULL;

			if ((he = bt_gethostbyname(argv[0])) == NULL)
				return (USAGE);

			memcpy(&cp.bdaddr, he->h_addr, sizeof(cp.bdaddr));
		}
		break;

	default:
		return (USAGE);
	}

	/* send request and expect status response */
	n0 = sizeof(b);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_LINK_CONTROL,
			NG_HCI_OCF_CREATE_CON),
			(char const *) &cp, sizeof(cp), b, &n0) == ERROR)
		return (ERROR);

	if (*b != 0x00)
		return (FAILED);

	/* wait for event */
again:
	n0 = sizeof(b);
	if (hci_recv(s, b, &n0) == ERROR)
		return (ERROR);
	if (n0 < sizeof(*e)) {
		errno = EIO;
		return (ERROR);
	}

	if (e->event == NG_HCI_EVENT_CON_COMPL) {
		ng_hci_con_compl_ep	*ep = (ng_hci_con_compl_ep *)(e + 1);

		if (ep->status != 0x00) {
			fprintf(stdout, "Status: %s [%#02x]\n", 
				hci_status2str(ep->status), ep->status);
			return (FAILED);
		}

		fprintf(stdout, "BD_ADDR: %s\n", hci_bdaddr2str(&ep->bdaddr));
		fprintf(stdout, "Connection handle: %d\n",
			le16toh(ep->con_handle));
		fprintf(stdout, "Encryption mode: %s [%d]\n",
			hci_encrypt2str(ep->encryption_mode, 0),
			ep->encryption_mode);
	} else
		goto again;

	return (OK);
} /* hci_create_connection */

/* Send Disconnect command to the unit */
static int
hci_disconnect(int s, int argc, char **argv)
{
	int			 n;
	char			 b[512];
	ng_hci_discon_cp	 cp;
	ng_hci_event_pkt_t	*e = (ng_hci_event_pkt_t *) b; 

	/* Set defaults */
	memset(&cp, 0, sizeof(cp));
	cp.reason = 0x13;

	/* parse command parameters */
	switch (argc) {
	case 2:
		/* reason */
		if (sscanf(argv[1], "%d", &n) != 1 || n <= 0x00 || n > 0xff)
			return (USAGE);

		cp.reason = (uint8_t) (n & 0xff);

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

	/* send request and expect status response */
	n = sizeof(b);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_LINK_CONTROL,
			NG_HCI_OCF_DISCON),
			(char const *) &cp, sizeof(cp), b, &n) == ERROR)
		return (ERROR);

	if (*b != 0x00)
		return (FAILED);

	/* wait for event */
again:
	n = sizeof(b);
	if (hci_recv(s, b, &n) == ERROR)
		return (ERROR);
	if (n < sizeof(*e)) {
		errno = EIO;
		return (ERROR);
	}

	if (e->event == NG_HCI_EVENT_DISCON_COMPL) {
		ng_hci_discon_compl_ep	*ep = (ng_hci_discon_compl_ep *)(e + 1);

		if (ep->status != 0x00) {
			fprintf(stdout, "Status: %s [%#02x]\n", 
				hci_status2str(ep->status), ep->status);
			return (FAILED);
		}

		fprintf(stdout, "Connection handle: %d\n",
			le16toh(ep->con_handle));
		fprintf(stdout, "Reason: %s [%#02x]\n",
			hci_status2str(ep->reason), ep->reason);
	} else
		goto again;

	return (OK);
} /* hci_disconnect */

/* Send Add_SCO_Connection command to the unit */
static int
hci_add_sco_connection(int s, int argc, char **argv)
{
	int			 n;
	char			 b[512];
	ng_hci_add_sco_con_cp	 cp;
	ng_hci_event_pkt_t	*e = (ng_hci_event_pkt_t *) b; 

	/* Set defaults */
	memset(&cp, 0, sizeof(cp));
	cp.pkt_type = htole16(NG_HCI_PKT_HV1 | NG_HCI_PKT_HV2 | NG_HCI_PKT_HV3);

	/* parse command parameters */
	switch (argc) {
	case 2:
		/* packet type */
		if (sscanf(argv[1], "%x", &n) != 1)
			return (USAGE);

		n &= (NG_HCI_PKT_HV1 | NG_HCI_PKT_HV2 | NG_HCI_PKT_HV3);
		if (n == 0)
			return (USAGE);

		cp.pkt_type = (uint16_t) (n & 0x0fff);
		cp.pkt_type = htole16(cp.pkt_type);

	case 1:
		/* acl connection handle */
		if (sscanf(argv[0], "%d", &n) != 1 || n <= 0 || n > 0x0eff)
			return (USAGE);

		cp.con_handle = (uint16_t) (n & 0x0fff);
		cp.con_handle = htole16(cp.con_handle);
		break;

	default:
		return (USAGE);
	}

	/* send request and expect status response */
	n = sizeof(b);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_LINK_CONTROL,
			NG_HCI_OCF_ADD_SCO_CON),
			(char const *) &cp, sizeof(cp), b, &n) == ERROR)
		return (ERROR);

	if (*b != 0x00)
		return (FAILED);

	/* wait for event */
again:
	n = sizeof(b);
	if (hci_recv(s, b, &n) == ERROR)
		return (ERROR);
	if (n < sizeof(*e)) {
		errno = EIO;
		return (ERROR);
	}

	if (e->event == NG_HCI_EVENT_CON_COMPL) {
		ng_hci_con_compl_ep	*ep = (ng_hci_con_compl_ep *)(e + 1);

		if (ep->status != 0x00) {
			fprintf(stdout, "Status: %s [%#02x]\n", 
				hci_status2str(ep->status), ep->status);
			return (FAILED);
		}

		fprintf(stdout, "BD_ADDR: %s\n", hci_bdaddr2str(&ep->bdaddr));
		fprintf(stdout, "Connection handle: %d\n",
			le16toh(ep->con_handle));
		fprintf(stdout, "Encryption mode: %s [%d]\n",
			hci_encrypt2str(ep->encryption_mode, 0),
			ep->encryption_mode);
	} else
		goto again;

	return (OK);
} /* Add_SCO_Connection */

/* Send Change_Connection_Packet_Type command to the unit */
static int
hci_change_connection_packet_type(int s, int argc, char **argv)
{
	int				 n;
	char				 b[512];
	ng_hci_change_con_pkt_type_cp	 cp;
	ng_hci_event_pkt_t		*e = (ng_hci_event_pkt_t *) b; 

	switch (argc) {
	case 2:
		/* connection handle */
		if (sscanf(argv[0], "%d", &n) != 1 || n <= 0 || n > 0x0eff)
			return (USAGE);

		cp.con_handle = (uint16_t) (n & 0x0fff);
		cp.con_handle = htole16(cp.con_handle);

		/* packet type */
		if (sscanf(argv[1], "%x", &n) != 1)
			return (USAGE);

		cp.pkt_type = (uint16_t) (n & 0xffff);
		cp.pkt_type = htole16(cp.pkt_type);
		break;

	default:
		return (USAGE);
	}

	/* send request and expect status response */
	n = sizeof(b);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_LINK_CONTROL,
			NG_HCI_OCF_CHANGE_CON_PKT_TYPE),
			(char const *) &cp, sizeof(cp), b, &n) == ERROR)
		return (ERROR);

	if (*b != 0x00)
		return (FAILED);

	/* wait for event */
again:
	n = sizeof(b);
	if (hci_recv(s, b, &n) == ERROR)
		return (ERROR);
	if (n < sizeof(*e)) {
		errno = EIO;
		return (ERROR);
	}

	if (e->event == NG_HCI_EVENT_CON_PKT_TYPE_CHANGED) {
		ng_hci_con_pkt_type_changed_ep	*ep = 
				(ng_hci_con_pkt_type_changed_ep *)(e + 1);

		if (ep->status != 0x00) {
			fprintf(stdout, "Status: %s [%#02x]\n", 
				hci_status2str(ep->status), ep->status);
			return (FAILED);
		}

		fprintf(stdout, "Connection handle: %d\n",
			le16toh(ep->con_handle));
		fprintf(stdout, "Packet type: %#04x\n",
			le16toh(ep->pkt_type));
	} else
		goto again;

	return (OK);
} /* hci_change_connection_packet_type */

/* Send Remote_Name_Request command to the unit */
static int
hci_remote_name_request(int s, int argc, char **argv)
{
	int				 n0;
	char				 b[512];
	ng_hci_remote_name_req_cp	 cp;
	ng_hci_event_pkt_t		*e = (ng_hci_event_pkt_t *) b; 

	memset(&cp, 0, sizeof(cp));
	cp.page_scan_rep_mode = NG_HCI_SCAN_REP_MODE0;
	cp.page_scan_mode = NG_HCI_MANDATORY_PAGE_SCAN_MODE;

	/* parse command parameters */
	switch (argc) {
	case 4:
		/* clock_offset */
		if (sscanf(argv[3], "%x", &n0) != 1)
			return (USAGE);

		cp.clock_offset = (n0 & 0xffff);
		cp.clock_offset = htole16(cp.clock_offset);

	case 3:
		/* page_scan_mode */
		if (sscanf(argv[2], "%d", &n0) != 1 || n0 < 0x00 || n0 > 0x03)
			return (USAGE);

		cp.page_scan_mode = (n0 & 0xff);

	case 2:
		/* page_scan_rep_mode */
		if (sscanf(argv[1], "%d", &n0) != 1 || n0 < 0x00 || n0 > 0x02)
			return (USAGE);

		cp.page_scan_rep_mode = (n0 & 0xff);

	case 1:
		/* BD_ADDR */
		if (!bt_aton(argv[0], &cp.bdaddr)) {
			struct hostent	*he = NULL;

			if ((he = bt_gethostbyname(argv[0])) == NULL)
				return (USAGE);

			memcpy(&cp.bdaddr, he->h_addr, sizeof(cp.bdaddr));
		}
		break;

	default:
		return (USAGE);
	}

	/* send request and expect status response */
	n0 = sizeof(b);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_LINK_CONTROL,
			NG_HCI_OCF_REMOTE_NAME_REQ),
			(char const *) &cp, sizeof(cp), b, &n0) == ERROR)
		return (ERROR);

	if (*b != 0x00)
		return (FAILED);

	/* wait for event */
again:
	n0 = sizeof(b);
	if (hci_recv(s, b, &n0) == ERROR)
		return (ERROR);
	if (n0 < sizeof(*e)) {
		errno = EIO;
		return (ERROR);
	}

	if (e->event == NG_HCI_EVENT_REMOTE_NAME_REQ_COMPL) {
		ng_hci_remote_name_req_compl_ep	*ep = 
				(ng_hci_remote_name_req_compl_ep *)(e + 1);

		if (ep->status != 0x00) {
			fprintf(stdout, "Status: %s [%#02x]\n", 
				hci_status2str(ep->status), ep->status);
			return (FAILED);
		}

		fprintf(stdout, "BD_ADDR: %s\n", hci_bdaddr2str(&ep->bdaddr));
		fprintf(stdout, "Name: %s\n", ep->name);
	} else 
		goto again;

	return (OK);
} /* hci_remote_name_request */

/* Send Read_Remote_Supported_Features command to the unit */
static int
hci_read_remote_supported_features(int s, int argc, char **argv)
{
	int				 n;
	char				 b[512];
	ng_hci_read_remote_features_cp	 cp;
	ng_hci_event_pkt_t		*e = (ng_hci_event_pkt_t *) b; 
	char				 buffer[1024];

	/* parse command parameters */
	switch (argc) {
	case 1:
		/* connecton handle */
		if (sscanf(argv[0], "%d", &n) != 1 || n < 0 || n > 0x0eff)
			return (USAGE);

		cp.con_handle = (n & 0x0fff);
		cp.con_handle = htole16(cp.con_handle);
		break;

	default:
		return (USAGE);
	}

	/* send request and expect status response */
	n = sizeof(b);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_LINK_CONTROL,
			NG_HCI_OCF_READ_REMOTE_FEATURES), 
			(char const *) &cp, sizeof(cp), b, &n) == ERROR)
		return (ERROR);

	if (*b != 0x00)
		return (FAILED);

	/* wait for event */
again:
	n = sizeof(b);
	if (hci_recv(s, b, &n) == ERROR)
		return (ERROR);

	if (n < sizeof(*e)) {
		errno = EIO;
		return (ERROR);
	}

	if (e->event == NG_HCI_EVENT_READ_REMOTE_FEATURES_COMPL) {
		ng_hci_read_remote_features_compl_ep	*ep = 
				(ng_hci_read_remote_features_compl_ep *)(e + 1);

		if (ep->status != 0x00) {
			fprintf(stdout, "Status: %s [%#02x]\n", 
				hci_status2str(ep->status), ep->status);
			return (FAILED);
		}

		fprintf(stdout, "Connection handle: %d\n",
			le16toh(ep->con_handle));
		fprintf(stdout, "Features: ");
		for (n = 0; n < sizeof(ep->features); n++)
			fprintf(stdout, "%#02x ", ep->features[n]);
		fprintf(stdout, "\n%s\n", hci_features2str(ep->features, 
			buffer, sizeof(buffer)));
	} else
		goto again;

	return (OK);
} /* hci_read_remote_supported_features */

/* Send Read_Remote_Version_Information command to the unit */
static int
hci_read_remote_version_information(int s, int argc, char **argv)
{
	int				 n;
	char				 b[512];
	ng_hci_read_remote_ver_info_cp	 cp;
	ng_hci_event_pkt_t		*e = (ng_hci_event_pkt_t *) b; 

	/* parse command parameters */
	switch (argc) {
	case 1:
		/* connecton handle */
		if (sscanf(argv[0], "%d", &n) != 1 || n < 0 || n > 0x0eff)
			return (USAGE);

		cp.con_handle = (n & 0x0fff);
		cp.con_handle = htole16(cp.con_handle);
		break;

	default:
		return (USAGE);
	}

	/* send request and expect status response */
	n = sizeof(b);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_LINK_CONTROL,
			NG_HCI_OCF_READ_REMOTE_VER_INFO), 
			(char const *) &cp, sizeof(cp), b, &n) == ERROR)
		return (ERROR);

	if (*b != 0x00)
		return (FAILED);

	/* wait for event */
again:
	n = sizeof(b);
	if (hci_recv(s, b, &n) == ERROR)
		return (ERROR);

	if (n < sizeof(*e)) {
		errno = EIO;
		return (ERROR);
	}

	if (e->event == NG_HCI_EVENT_READ_REMOTE_VER_INFO_COMPL) {
		ng_hci_read_remote_ver_info_compl_ep	*ep = 
				(ng_hci_read_remote_ver_info_compl_ep *)(e + 1);

		if (ep->status != 0x00) {
			fprintf(stdout, "Status: %s [%#02x]\n", 
				hci_status2str(ep->status), ep->status);
			return (FAILED);
		}

		ep->manufacturer = le16toh(ep->manufacturer);

		fprintf(stdout, "Connection handle: %d\n",
			le16toh(ep->con_handle));
		fprintf(stdout, "LMP version: %s [%#02x]\n",
			hci_lmpver2str(ep->lmp_version), ep->lmp_version);
		fprintf(stdout, "LMP sub-version: %#04x\n",
			le16toh(ep->lmp_subversion));
		fprintf(stdout, "Manufacturer: %s [%#04x]\n",
			hci_manufacturer2str(ep->manufacturer),
			ep->manufacturer);
	} else
		goto again;

	return (OK);
} /* hci_read_remote_version_information */

/* Send Read_Clock_Offset command to the unit */
static int
hci_read_clock_offset(int s, int argc, char **argv)
{
	int				 n;
	char				 b[512];
	ng_hci_read_clock_offset_cp	 cp;
	ng_hci_event_pkt_t		*e = (ng_hci_event_pkt_t *) b; 

	/* parse command parameters */
	switch (argc) {
	case 1:
		/* connecton handle */
		if (sscanf(argv[0], "%d", &n) != 1 || n < 0 || n > 0x0eff)
			return (USAGE);

		cp.con_handle = (n & 0x0fff);
		cp.con_handle = htole16(cp.con_handle);
		break;

	default:
		return (USAGE);
	}

	/* send request and expect status response */
	n = sizeof(b);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_LINK_CONTROL,
			NG_HCI_OCF_READ_CLOCK_OFFSET),
			(char const *) &cp, sizeof(cp), b, &n) == ERROR)
		return (ERROR);

	if (*b != 0x00)
		return (FAILED);

	/* wait for event */
again:
	n = sizeof(b);
	if (hci_recv(s, b, &n) == ERROR)
		return (ERROR);

	if (n < sizeof(*e)) {
		errno = EIO;
		return (ERROR);
	}

	if (e->event == NG_HCI_EVENT_READ_CLOCK_OFFSET_COMPL) {
		ng_hci_read_clock_offset_compl_ep	*ep = 
				(ng_hci_read_clock_offset_compl_ep *)(e + 1);

		if (ep->status != 0x00) {
			fprintf(stdout, "Status: %s [%#02x]\n", 
				hci_status2str(ep->status), ep->status);
			return (FAILED);
		}

		fprintf(stdout, "Connection handle: %d\n",
			le16toh(ep->con_handle));
		fprintf(stdout, "Clock offset: %#04x\n",
			le16toh(ep->clock_offset));
	} else
		goto again;

	return (OK);
} /* hci_read_clock_offset */

struct hci_command	link_control_commands[] = {
{
"inquiry <LAP> <inquiry_length> <num_reponses>",
"\nThis command will cause the Bluetooth unit to enter Inquiry Mode.\n" \
"Inquiry Mode is used to discover other nearby Bluetooth units. The LAP\n" \
"input parameter contains the LAP from which the inquiry access code shall\n" \
"be derived when the inquiry procedure is made. The Inquiry_Length parameter\n"\
"specifies the total duration of the Inquiry Mode and, when this time\n" \
"expires, Inquiry will be halted. The Num_Responses parameter specifies the\n" \
"number of responses that can be received before the Inquiry is halted.\n\n" \
"\t<LAP>            - xx:xx:xx; 9e:8b:33 (GIAC), 93:8b:00 (LDIAC)\n" \
"\t<inquiry_length> - dd; total length == dd * 1.28 sec\n" \
"\t<num_responses>  - dd",
&hci_inquiry
},
{
"create_connection <BD_ADDR> <pkt> <rep_mode> <ps_mode> <clck_off> <role_sw>",
"" \
"\t<BD_ADDR> - xx:xx:xx:xx:xx:xx BD_ADDR or name\n\n" \
"\t<pkt>     - xxxx; packet type\n" \
"" \
"\t\tACL packets\n" \
"\t\t-----------\n" \
"\t\t0x0008 DM1\n" \
"\t\t0x0010 DH1\n" \
"\t\t0x0400 DM3\n" \
"\t\t0x0800 DH3\n" \
"\t\t0x4000 DM5\n" \
"\t\t0x8000 DH5\n\n" \
"" \
"\trep_mode  - d; page scan repetition mode\n" \
"" \
"\t\tPage scan repetition modes\n" \
"\t\t--------------------------\n" \
"\t\t0 Page scan repetition mode 0\n" \
"\t\t1 Page scan repetition mode 1\n" \
"\t\t2 Page scan repetition mode 2\n" \
"\n" \
"\tps_mode   - d; Page scan mode\n" \
"" \
"\t\tPage scan modes\n" \
"\t\t---------------\n" \
"\t\t0 Mandatory page scan mode\n" \
"\t\t1 Optional page scan mode1\n" \
"\t\t2 Optional page scan mode2\n" \
"\t\t3 Optional page scan mode3\n" \
"\n" \
"\tclck_off  - dddd; clock offset. Use 0 if unknown\n\n" \
"\trole_sw   - d; allow (1) or deny role switch\n",
&hci_create_connection
},
{
"disconnect <connection_handle> <reason>",
"\nThe Disconnection command is used to terminate an existing connection.\n" \
"The connection handle command parameter indicates which connection is to\n" \
"be disconnected. The Reason command parameter indicates the reason for\n" \
"ending the connection.\n\n" \
"\t<connection_handle> - dddd; connection handle\n" \
"\t<reason>            - dd; reason; usually 19 (0x13) - user ended;\n" \
"\t                      also 0x05, 0x13-0x15, 0x1A, 0x29",
&hci_disconnect
},
{
"add_sco_connection <acl connection handle> <packet type>",
"This command will cause the link manager to create a SCO connection using\n" \
"the ACL connection specified by the connection handle command parameter.\n" \
"The Link Manager will determine how the new connection is established. This\n"\
"connection is determined by the current state of the device, its piconet,\n" \
"and the state of the device to be connected. The packet type command parameter\n" \
"specifies which packet types the Link Manager should use for the connection.\n"\
"The Link Manager must only use the packet type(s) specified by the packet\n" \
"type command parameter for sending HCI SCO data packets. Multiple packet\n" \
"types may be specified for the packet type command parameter by performing\n" \
"a bitwise OR operation of the different packet types. Note: An SCO connection\n" \
"can only be created when an ACL connection already exists and when it is\n" \
"not put in park mode.\n\n" \
"\t<connection_handle> - dddd; ACL connection handle\n" \
"\t<packet_type>       - xxxx; packet type\n" \
"" \
"\t\tSCO packets\n" \
"\t\t-----------\n" \
"\t\t0x0020 HV1\n" \
"\t\t0x0040 HV2\n" \
"\t\t0x0080 HV3\n",
&hci_add_sco_connection
},
{
"change_connection_packet_type <connection_hande> <packet_type>",
"The Change_Connection_Packet_Type command is used to change which packet\n" \
"types can be used for a connection that is currently established. This\n" \
"allows current connections to be dynamically modified to support different\n" \
"types of user data. The Packet_Type command parameter specifies which\n" \
"packet types the Link Manager can use for the connection. Multiple packet\n" \
"types may be specified for the Packet_Type command parameter by bitwise OR\n" \
"operation of the different packet types.\n\n" \
"\t<connection_handle> - dddd; connection handle\n" \
"\t<packet_type>       - xxxx; packet type mask\n" \
"" \
"\t\tACL packets\n" \
"\t\t-----------\n" \
"\t\t0x0008 DM1\n" \
"\t\t0x0010 DH1\n" \
"\t\t0x0400 DM3\n" \
"\t\t0x0800 DH3\n" \
"\t\t0x4000 DM5\n" \
"\t\t0x8000 DH5\n\n" \
"" \
"\t\tSCO packets\n" \
"\t\t-----------\n" \
"\t\t0x0020 HV1\n" \
"\t\t0x0040 HV2\n" \
"\t\t0x0080 HV3\n" \
"",
&hci_change_connection_packet_type
},
{
"remote_name_request <BD_ADDR> <ps_rep_mode> <ps_mode> <clock_offset>",
"\nThe Remote_Name_Request command is used to obtain the user-friendly\n" \
"name of another Bluetooth unit.\n\n" \
"\t<BD_ADDR>      - xx:xx:xx:xx:xx:xx BD_ADDR or name\n" \
"\t<ps_rep_mode>  - dd; page scan repetition mode [0-2]\n" \
"\t<ps_mode>      - dd; page scan mode [0-3]\n" \
"\t<clock_offset> - xxxx; clock offset [0 - 0xffff]",
&hci_remote_name_request
},
{
"read_remote_supported_features <connection_handle>",
"\nThis command requests a list of the supported features for the remote\n" \
"unit identified by the connection handle parameter. The connection handle\n" \
"must be a connection handle for an ACL connection.\n\n" \
"\t<connection_handle> - dddd; connection handle",
&hci_read_remote_supported_features
},
{
"read_remote_version_information <connection_handle>",
"\nThis command will obtain the values for the version information for the\n" \
"remote Bluetooth unit identified by the connection handle parameter. The\n" \
"connection handle must be a connection handle for an ACL connection.\n\n" \
"\t<connection_handle> - dddd; connection handle",
&hci_read_remote_version_information
},
{
"read_clock_offset <connection_handle>",
"\nThis command allows the Host to read the clock offset from the remote unit.\n" \
"\t<connection_handle> - dddd; connection handle",
&hci_read_clock_offset
},
{
NULL,
}};

