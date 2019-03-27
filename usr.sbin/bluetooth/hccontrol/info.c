/*-
 * info.c
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
 * $Id: info.c,v 1.3 2003/08/18 19:19:54 max Exp $
 * $FreeBSD$
 */

#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "hccontrol.h"

/* Send Read_Local_Version_Information command to the unit */
static int
hci_read_local_version_information(int s, int argc, char **argv)
{
	ng_hci_read_local_ver_rp	rp;
	int				n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_INFO,
			NG_HCI_OCF_READ_LOCAL_VER), (char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	rp.manufacturer = le16toh(rp.manufacturer);

	fprintf(stdout, "HCI version: %s [%#02x]\n",
		hci_ver2str(rp.hci_version), rp.hci_version);
	fprintf(stdout, "HCI revision: %#04x\n",
		le16toh(rp.hci_revision));
	fprintf(stdout, "LMP version: %s [%#02x]\n",
		hci_lmpver2str(rp.lmp_version), rp.lmp_version);
	fprintf(stdout, "LMP sub-version: %#04x\n", 
		le16toh(rp.lmp_subversion));
	fprintf(stdout, "Manufacturer: %s [%#04x]\n", 
		hci_manufacturer2str(rp.manufacturer), rp.manufacturer);

	return (OK);
} /* hci_read_local_version_information */

/* Send Read_Local_Supported_Features command to the unit */
static int
hci_read_local_supported_features(int s, int argc, char **argv)
{
	ng_hci_read_local_features_rp	rp;
	int				n;
	char				buffer[1024];

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_INFO,
			NG_HCI_OCF_READ_LOCAL_FEATURES),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Features: ");
	for (n = 0; n < sizeof(rp.features); n++)
		fprintf(stdout, "%#02x ", rp.features[n]);
	fprintf(stdout, "\n%s\n", hci_features2str(rp.features, 
		buffer, sizeof(buffer)));

	return (OK);
} /* hci_read_local_supported_features */

/* Sent Read_Buffer_Size command to the unit */
static int
hci_read_buffer_size(int s, int argc, char **argv)
{
	ng_hci_read_buffer_size_rp	rp;
	int				n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_INFO,
			NG_HCI_OCF_READ_BUFFER_SIZE),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Max. ACL packet size: %d bytes\n",
		le16toh(rp.max_acl_size));
	fprintf(stdout, "Number of ACL packets: %d\n",
		le16toh(rp.num_acl_pkt));
	fprintf(stdout, "Max. SCO packet size: %d bytes\n",
		rp.max_sco_size);
	fprintf(stdout, "Number of SCO packets: %d\n",
		le16toh(rp.num_sco_pkt));
	
	return (OK);
} /* hci_read_buffer_size */

/* Send Read_Country_Code command to the unit */
static int
hci_read_country_code(int s, int argc, char **argv)
{
	ng_hci_read_country_code_rp	rp;
	int				n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_INFO,
			NG_HCI_OCF_READ_COUNTRY_CODE),
			(char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "Country code: %s [%#02x]\n",
			hci_cc2str(rp.country_code), rp.country_code);

	return (OK);
} /* hci_read_country_code */

/* Send Read_BD_ADDR command to the unit */
static int
hci_read_bd_addr(int s, int argc, char **argv)
{
	ng_hci_read_bdaddr_rp	rp;
	int			n;

	n = sizeof(rp);
	if (hci_simple_request(s, NG_HCI_OPCODE(NG_HCI_OGF_INFO,
			NG_HCI_OCF_READ_BDADDR), (char *) &rp, &n) == ERROR)
		return (ERROR);

	if (rp.status != 0x00) {
		fprintf(stdout, "Status: %s [%#02x]\n", 
			hci_status2str(rp.status), rp.status);
		return (FAILED);
	}

	fprintf(stdout, "BD_ADDR: %s\n", bt_ntoa(&rp.bdaddr, NULL));

	return (OK);
} /* hci_read_bd_addr */

struct hci_command	info_commands[] = {
{
"read_local_version_information",
"\nThis command will read the values for the version information for the\n" \
"local Bluetooth unit.",
&hci_read_local_version_information	
},
{
"read_local_supported_features",
"\nThis command requests a list of the supported features for the local\n" \
"unit. This command will return a list of the LMP features.",
&hci_read_local_supported_features
},
{
"read_buffer_size",
"\nThe Read_Buffer_Size command is used to read the maximum size of the\n" \
"data portion of HCI ACL and SCO Data Packets sent from the Host to the\n" \
"Host Controller.",
&hci_read_buffer_size
},
{
"read_country_code",
"\nThis command will read the value for the Country_Code return parameter.\n" \
"The Country_Code defines which range of frequency band of the ISM 2.4 GHz\n" \
"band will be used by the unit.",
&hci_read_country_code
},
{
"read_bd_addr",
"\nThis command will read the value for the BD_ADDR parameter. The BD_ADDR\n" \
"is a 48-bit unique identifier for a Bluetooth unit.",
&hci_read_bd_addr
},
{
NULL,
}};

