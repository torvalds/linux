/*-
 * link_policy.c
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
 * $Id: link_policy.c,v 1.3 2003/08/18 19:19:54 max Exp $
 * $FreeBSD$
 */

#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "hccontrol.h"

/* Send Role Discovery to the unit */
static int
hci_role_discovery(int s, int argc, char **argv)
{
	ng_hci_role_discovery_cp	cp;
	ng_hci_role_discovery_rp	rp;
	int				n;

	/* parse command parameters */
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

	/* send request */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_LINK_POLICY,
			NG_HCI_OCF_ROLE_DISCOVERY), 
			(char const *) &cp, sizeof(cp), 
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Connection handle: %d\n", le16toh(rp.con_handle));
	fprintf(stdout, "Role: %s [%#x]\n",
		(rp.role == NG_HCI_ROLE_MASTER)? "Master" : "Slave", rp.role);

	return (OK);
} /* hci_role_discovery */

/* Send Swith Role to the unit */
static int
hci_switch_role(int s, int argc, char **argv)
{
	int			 n0;
	char			 b[512];
	ng_hci_switch_role_cp	 cp;
	ng_hci_event_pkt_t	*e = (ng_hci_event_pkt_t *) b; 

	/* parse command parameters */
	switch (argc) {
	case 2:
		/* bdaddr */
		if (!bt_aton(argv[0], &cp.bdaddr)) {
			struct hostent	*he = NULL;

			if ((he = bt_gethostbyname(argv[0])) == NULL)
				return (USAGE);

			memcpy(&cp.bdaddr, he->h_addr, sizeof(cp.bdaddr));
		}

		/* role */
		if (sscanf(argv[1], "%d", &n0) != 1)
			return (USAGE);

		cp.role = n0? NG_HCI_ROLE_SLAVE : NG_HCI_ROLE_MASTER;
		break;

	default:
		return (USAGE);
	}

	/* send request and expect status response */
	n0 = sizeof(b);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_LINK_POLICY,
			NG_HCI_OCF_SWITCH_ROLE),
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

	if (e->event == NG_HCI_EVENT_ROLE_CHANGE) {
		ng_hci_role_change_ep	*ep = (ng_hci_role_change_ep *)(e + 1);

		if (ep->status != 0x00) {
			fprintf(stdout, "Status: %s [%#02x]\n", 
				hci_status2str(ep->status), ep->status);
			return (FAILED);
		}

		fprintf(stdout, "BD_ADDR: %s\n", hci_bdaddr2str(&ep->bdaddr));
		fprintf(stdout, "Role: %s [%#x]\n",
			(ep->role == NG_HCI_ROLE_MASTER)? "Master" : "Slave",
			ep->role);
	} else
		goto again;

	return (OK);
} /* hci_switch_role */

/* Send Read_Link_Policy_Settings command to the unit */
static int
hci_read_link_policy_settings(int s, int argc, char **argv)
{
	ng_hci_read_link_policy_settings_cp	cp;
	ng_hci_read_link_policy_settings_rp	rp;
	int					n;

	/* parse command parameters */
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

	/* send request */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_LINK_POLICY,
			NG_HCI_OCF_READ_LINK_POLICY_SETTINGS), 
			(char const *) &cp, sizeof(cp), 
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Connection handle: %d\n", le16toh(rp.con_handle));
	fprintf(stdout, "Link policy settings: %#x\n", le16toh(rp.settings));

	return (OK);
} /* hci_read_link_policy_settings */

/* Send Write_Link_Policy_Settings command to the unit */
static int
hci_write_link_policy_settings(int s, int argc, char **argv)
{
	ng_hci_write_link_policy_settings_cp	cp;
	ng_hci_write_link_policy_settings_rp	rp;
	int					n;

	/* parse command parameters */
	switch (argc) {
	case 2:
		/* connection handle */
		if (sscanf(argv[0], "%d", &n) != 1 || n <= 0 || n > 0x0eff)
			return (USAGE);

		cp.con_handle = (uint16_t) (n & 0x0fff);
		cp.con_handle = htole16(cp.con_handle);

		/* link policy settings */
		if (sscanf(argv[1], "%x", &n) != 1)
			return (USAGE);

		cp.settings = (uint16_t) (n & 0x0ffff);
		cp.settings = htole16(cp.settings);
		break;

	default:
		return (USAGE);
	}

	/* send request */
	n = sizeof(rp);
	if (hci_request(s, NG_HCI_OPCODE(NG_HCI_OGF_LINK_POLICY,
			NG_HCI_OCF_WRITE_LINK_POLICY_SETTINGS), 
			(char const *) &cp, sizeof(cp), 
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	return (OK);
} /* hci_write_link_policy_settings */

struct hci_command	link_policy_commands[] = {
{
"role_discovery <connection_handle>",
"\nThe Role_Discovery command is used for a Bluetooth device to determine\n" \
"which role the device is performing for a particular Connection Handle.\n" \
"The connection handle must be a connection handle for an ACL connection.\n\n" \
"\t<connection_handle> - dddd; connection handle",
&hci_role_discovery
},
{
"switch_role <BD_ADDR> <role>",
"\nThe Switch_Role command is used for a Bluetooth device to switch the\n" \
"current role the device is performing for a particular connection with\n" \
"another specified Bluetooth device. The BD_ADDR command parameter indicates\n"\
"for which connection the role switch is to be performed. The Role indicates\n"\
"the requested new role that the local device performs. Note: the BD_ADDR\n" \
"command parameter must specify a Bluetooth device for which a connection\n"
"already exists.\n\n" \
"\t<BD_ADDR> - xx:xx:xx:xx:xx:xx BD_ADDR or name\n" \
"\t<role>    - dd; role; 0 - Master, 1 - Slave",
&hci_switch_role
},
{
"read_link_policy_settings <connection_handle>",
"\nThis command will read the Link Policy setting for the specified connection\n"\
"handle. The link policy settings parameter determines the behavior of the\n" \
"local Link Manager when it receives a request from a remote device or it\n" \
"determines itself to change the master-slave role or to enter the hold,\n" \
"sniff, or park mode. The local Link Manager will automatically accept or\n" \
"reject such a request from the remote device, and may even autonomously\n" \
"request itself, depending on the value of the link policy settings parameter\n"\
"for the corresponding connection handle. The connection handle must be a\n" \
"connection handle for an ACL connection.\n\n" \
"\t<connection_handle> - dddd; connection handle",
&hci_read_link_policy_settings
},
{
"write_link_policy_settings <connection_handle> <settings>",
"\nThis command will write the Link Policy setting for the specified connection\n"\
"handle. The link policy settings parameter determines the behavior of the\n" \
"local Link Manager when it receives a request from a remote device or it\n" \
"determines itself to change the master-slave role or to enter the hold,\n" \
"sniff, or park mode. The local Link Manager will automatically accept or\n" \
"reject such a request from the remote device, and may even autonomously\n" \
"request itself, depending on the value of the link policy settings parameter\n"\
"for the corresponding connection handle. The connection handle must be a\n" \
"connection handle for an ACL connection. Multiple Link Manager policies may\n"\
"be specified for the link policy settings parameter by performing a bitwise\n"\
"OR operation of the different activity types.\n\n" \
"\t<connection_handle> - dddd; connection handle\n" \
"\t<settings>          - xxxx; settings\n" \
"\t\t0x0000 - Disable All LM Modes (Default)\n" \
"\t\t0x0001 - Enable Master Slave Switch\n" \
"\t\t0x0002 - Enable Hold Mode\n" \
"\t\t0x0004 - Enable Sniff Mode\n" \
"\t\t0x0008 - Enable Park Mode\n",
&hci_write_link_policy_settings
},
{
NULL,
}};

