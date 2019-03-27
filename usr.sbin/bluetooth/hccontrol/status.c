/*-
 * status.c
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
 * $Id: status.c,v 1.2 2003/05/21 22:40:30 max Exp $
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/endian.h>
#include <errno.h>
#include <netgraph/bluetooth/include/ng_hci.h>
#include <stdio.h>
#include "hccontrol.h"

/* Send Read_Failed_Contact_Counter command to the unit */
static int
hci_read_failed_contact_counter(int s, int argc, char **argv)
{
	ng_hci_read_failed_contact_cntr_cp	cp;
	ng_hci_read_failed_contact_cntr_rp	rp;
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
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_STATUS,
			NG_HCI_OCF_READ_FAILED_CONTACT_CNTR),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Connection handle: %d\n", le16toh(rp.con_handle));
	fprintf(stdout, "Failed contact counter: %d\n", le16toh(rp.counter));

	return (OK);
} /* hci_read_failed_contact_counter */

/* Send Reset_Failed_Contact_Counter command to the unit */
static int
hci_reset_failed_contact_counter(int s, int argc, char **argv)
{
	ng_hci_reset_failed_contact_cntr_cp	cp;
	ng_hci_reset_failed_contact_cntr_rp	rp;
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
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_STATUS,
			NG_HCI_OCF_RESET_FAILED_CONTACT_CNTR),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}
	
	return (OK);
} /* hci_reset_failed_contact_counter */

/* Sent Get_Link_Quality command to the unit */
static int
hci_get_link_quality(int s, int argc, char **argv)
{
	ng_hci_get_link_quality_cp	cp;
	ng_hci_get_link_quality_rp	rp;
	int				n;

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
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_STATUS,
			NG_HCI_OCF_GET_LINK_QUALITY),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Connection handle: %d\n", le16toh(rp.con_handle));
	fprintf(stdout, "Link quality: %d\n", le16toh(rp.quality));
	
	return (OK);
} /* hci_get_link_quality */

/* Send Read_RSSI command to the unit */
static int
hci_read_rssi(int s, int argc, char **argv)
{
	ng_hci_read_rssi_cp	cp;
	ng_hci_read_rssi_rp	rp;
	int			n;
	
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
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_STATUS,
			NG_HCI_OCF_READ_RSSI),
			(char const *) &cp, sizeof(cp),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Connection handle: %d\n", le16toh(rp.con_handle));
	fprintf(stdout, "RSSI: %d dB\n", (int) rp.rssi);
	
	return (OK);
} /* hci_read_rssi */

struct hci_command	status_commands[] = {
{
"read_failed_contact_counter <connection_handle>",
"\nThis command will read the value for the Failed_Contact_Counter\n" \
"parameter for a particular ACL connection to another device.\n\n" \
"\t<connection_handle> - dddd; ACL connection handle\n",
&hci_read_failed_contact_counter
},
{
"reset_failed_contact_counter <connection_handle>",
"\nThis command will reset the value for the Failed_Contact_Counter\n" \
"parameter for a particular ACL connection to another device.\n\n" \
"\t<connection_handle> - dddd; ACL connection handle\n",
&hci_reset_failed_contact_counter
},
{
"get_link_quality <connection_handle>",
"\nThis command will return the value for the Link_Quality for the\n" \
"specified ACL connection handle. This command will return a Link_Quality\n" \
"value from 0-255, which represents the quality of the link between two\n" \
"Bluetooth devices. The higher the value, the better the link quality is.\n" \
"Each Bluetooth module vendor will determine how to measure the link quality." \
"\n\n" \
"\t<connection_handle> - dddd; ACL connection handle\n", 
&hci_get_link_quality
},
{
"read_rssi <connection_handle>",
"\nThis command will read the value for the difference between the\n" \
"measured Received Signal Strength Indication (RSSI) and the limits of\n" \
"the Golden Receive Power Range for a ACL connection handle to another\n" \
"Bluetooth device. Any positive RSSI value returned by the Host Controller\n" \
"indicates how many dB the RSSI is above the upper limit, any negative\n" \
"value indicates how many dB the RSSI is below the lower limit. The value\n" \
"zero indicates that the RSSI is inside the Golden Receive Power Range.\n\n" \
"\t<connection_handle> - dddd; ACL connection handle\n", 
&hci_read_rssi
},
{
NULL,
}};

