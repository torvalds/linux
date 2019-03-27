/*-
 * node.c
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
 * $Id: node.c,v 1.6 2003/07/22 21:14:02 max Exp $
 * $FreeBSD$
 */

#include <sys/ioctl.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <errno.h>
#include <netgraph/ng_message.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "hccontrol.h"

/* Send Read_Node_State command to the node */
static int
hci_read_node_state(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_node_state	r;

	memset(&r, 0, sizeof(r));
	if (ioctl(s, SIOC_HCI_RAW_NODE_GET_STATE, &r, sizeof(r)) < 0)
		return (ERROR);

	fprintf(stdout, "State: %#x\n", r.state);

	return (OK);
} /* hci_read_node_state */

/* Send Intitialize command to the node */
static int
hci_node_initialize(int s, int argc, char **argv)
{
	if (ioctl(s, SIOC_HCI_RAW_NODE_INIT) < 0)
		return (ERROR);

	return (OK);
} /* hci_node_initialize */

/* Send Read_Debug_Level command to the node */
static int
hci_read_debug_level(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_node_debug	r;

	memset(&r, 0, sizeof(r));
	if (ioctl(s, SIOC_HCI_RAW_NODE_GET_DEBUG, &r, sizeof(r)) < 0)
		return (ERROR);

	fprintf(stdout, "Debug level: %d\n", r.debug);

	return (OK);
} /* hci_read_debug_level */

/* Send Write_Debug_Level command to the node */
static int
hci_write_debug_level(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_node_debug	r;
  
	memset(&r, 0, sizeof(r));
	switch (argc) {
	case 1:
		r.debug = atoi(argv[0]);
		break;
 
	default:
		return (USAGE);
	}

	if (ioctl(s, SIOC_HCI_RAW_NODE_SET_DEBUG, &r, sizeof(r)) < 0)
		return (ERROR);

	return (OK);
} /* hci_write_debug_level */

/* Send Read_Node_Buffer_Size command to the node */
static int
hci_read_node_buffer_size(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_node_buffer	r;

	memset(&r, 0, sizeof(r));
	if (ioctl(s, SIOC_HCI_RAW_NODE_GET_BUFFER, &r, sizeof(r)) < 0)
		return (ERROR);

	fprintf(stdout, "Number of free command buffers: %d\n",
		r.buffer.cmd_free);
	fprintf(stdout, "Max. ACL packet size: %d\n",
		r.buffer.acl_size);
	fprintf(stdout, "Numbef of free ACL buffers: %d\n",
		r.buffer.acl_free);
	fprintf(stdout, "Total number of ACL buffers: %d\n",
		r.buffer.acl_pkts);
	fprintf(stdout, "Max. SCO packet size: %d\n",
		r.buffer.sco_size);
	fprintf(stdout, "Numbef of free SCO buffers: %d\n",
		r.buffer.sco_free);
	fprintf(stdout, "Total number of SCO buffers: %d\n",
		r.buffer.sco_pkts);

	return (OK);
} /* hci_read_node_buffer_size */

/* Send Read_Node_BD_ADDR command to the node */
static int
hci_read_node_bd_addr(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_node_bdaddr	r;

	memset(&r, 0, sizeof(r));
	if (ioctl(s, SIOC_HCI_RAW_NODE_GET_BDADDR, &r, sizeof(r)) < 0)
		return (ERROR);

	fprintf(stdout, "BD_ADDR: %s\n", bt_ntoa(&r.bdaddr, NULL));

	return (OK);
} /* hci_read_node_bd_addr */

/* Send Read_Node_Features command to the node */
static int
hci_read_node_features(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_node_features	r;
	int						n;
	char						buffer[1024];

	memset(&r, 0, sizeof(r));
	if (ioctl(s, SIOC_HCI_RAW_NODE_GET_FEATURES, &r, sizeof(r)) < 0)
		return (ERROR);

	fprintf(stdout, "Features: ");
	for (n = 0; n < sizeof(r.features)/sizeof(r.features[0]); n++)
		fprintf(stdout, "%#02x ", r.features[n]);
	fprintf(stdout, "\n%s\n", hci_features2str(r.features, 
		buffer, sizeof(buffer)));

	return (OK);
} /* hci_read_node_features */

/* Send Read_Node_Stat command to the node */
static int
hci_read_node_stat(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_node_stat	r;

	memset(&r, 0, sizeof(r));
	if (ioctl(s, SIOC_HCI_RAW_NODE_GET_STAT, &r, sizeof(r)) < 0)
		return (ERROR);

	fprintf(stdout, "Commands sent: %d\n", r.stat.cmd_sent);
	fprintf(stdout, "Events received: %d\n", r.stat.evnt_recv);
	fprintf(stdout, "ACL packets received: %d\n", r.stat.acl_recv);
	fprintf(stdout, "ACL packets sent: %d\n", r.stat.acl_sent);
	fprintf(stdout, "SCO packets received: %d\n", r.stat.sco_recv);
	fprintf(stdout, "SCO packets sent: %d\n", r.stat.sco_sent);
	fprintf(stdout, "Bytes received: %d\n", r.stat.bytes_recv);
	fprintf(stdout, "Bytes sent: %d\n", r.stat.bytes_sent);

	return (OK);
} /* hci_read_node_stat */

/* Send Reset_Node_Stat command to the node */
static int
hci_reset_node_stat(int s, int argc, char **argv)
{
	if (ioctl(s, SIOC_HCI_RAW_NODE_RESET_STAT) < 0)
		return (ERROR);

	return (OK);
} /* hci_reset_node_stat */

/* Send Flush_Neighbor_Cache command to the node */
static int
hci_flush_neighbor_cache(int s, int argc, char **argv)
{
	if (ioctl(s, SIOC_HCI_RAW_NODE_FLUSH_NEIGHBOR_CACHE) < 0)
		return (ERROR);

	return (OK);
} /* hci_flush_neighbor_cache */

#define MIN(a,b) (((a)>(b)) ? (b) :(a) )

static int  hci_dump_adv(uint8_t *data, int length)
{
	int elemlen;
	int type;
	int i;

	while(length>0){
		elemlen = *data;
		data++;
		length --;
		elemlen--;
		if(length<=0)
			break;
		type = *data;
		data++;
		length --;
		elemlen--;
		if(length<=0)
			break;
		switch(type){
		case 0x1:
			printf("NDflag:%x\n", *data);
			break;
		case 0x9:
			printf("LocalName:");
			for(i = 0; i < MIN(length,elemlen); i++){
				putchar(data[i]);
			}
			printf("\n");
			break;
		default:
			printf("Type%d:", type);
			for(i=0; i < MIN(length,elemlen); i++){
				printf("%02x ",data[i]);
			}
			printf("\n");
			break;
		}
		data += elemlen;
		length -= elemlen;
	}
	return 0;
}
#undef MIN
/* Send Read_Neighbor_Cache command to the node */
static int
hci_read_neighbor_cache(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_node_neighbor_cache	r;
	int						n, error = OK;
	const char  *addrtype2str[] = {"B", "P", "R", "E"};

	memset(&r, 0, sizeof(r));
	r.num_entries = NG_HCI_MAX_NEIGHBOR_NUM;
	r.entries = calloc(NG_HCI_MAX_NEIGHBOR_NUM,
				sizeof(ng_hci_node_neighbor_cache_entry_ep));
	if (r.entries == NULL) {
		errno = ENOMEM;
		return (ERROR);
	}

	if (ioctl(s, SIOC_HCI_RAW_NODE_GET_NEIGHBOR_CACHE, &r,
			sizeof(r)) < 0) {
		error = ERROR;
		goto out;
	}

	fprintf(stdout,
"T " \
"BD_ADDR           " \
"Features                " \
"Clock offset " \
"Page scan " \
"Rep. scan\n");

	for (n = 0; n < r.num_entries; n++) {
	        uint8_t addrtype = r.entries[n].addrtype;
		if(addrtype >= sizeof(addrtype2str)/sizeof(addrtype2str[0]))
			addrtype = sizeof(addrtype2str)/sizeof(addrtype2str[0]) - 1;
		fprintf(stdout, 
"%1s %-17.17s " \
"%02x %02x %02x %02x %02x %02x %02x %02x " \
"%#12x " \
"%#9x " \
"%#9x\n",
			addrtype2str[addrtype],
			hci_bdaddr2str(&r.entries[n].bdaddr),
			r.entries[n].features[0], r.entries[n].features[1],
			r.entries[n].features[2], r.entries[n].features[3],
			r.entries[n].features[4], r.entries[n].features[5],
			r.entries[n].features[6], r.entries[n].features[7],
			r.entries[n].clock_offset, r.entries[n].page_scan_mode,
			r.entries[n].page_scan_rep_mode);
		hci_dump_adv(r.entries[n].extinq_data,
			     r.entries[n].extinq_size);
		fprintf(stdout,"\n");
	}
out:
	free(r.entries);

	return (error);
} /* hci_read_neightbor_cache */

/* Send Read_Connection_List command to the node */
static int
hci_read_connection_list(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_con_list	r;
	int					n, error = OK;

	memset(&r, 0, sizeof(r));
	r.num_connections = NG_HCI_MAX_CON_NUM;
	r.connections = calloc(NG_HCI_MAX_CON_NUM, sizeof(ng_hci_node_con_ep));
	if (r.connections == NULL) {
		errno = ENOMEM;
		return (ERROR);
	}

	if (ioctl(s, SIOC_HCI_RAW_NODE_GET_CON_LIST, &r, sizeof(r)) < 0) {
		error = ERROR;
		goto out;
	}

	fprintf(stdout,
"Remote BD_ADDR    " \
"Handle " \
"Type " \
"Mode " \
"Role " \
"Encrypt " \
"Pending " \
"Queue " \
"State\n");

	for (n = 0; n < r.num_connections; n++) {
		fprintf(stdout,
"%-17.17s " \
"%6d " \
"%4.4s " \
"%4d " \
"%4.4s " \
"%7.7s " \
"%7d " \
"%5d " \
"%s\n",
			hci_bdaddr2str(&r.connections[n].bdaddr),
			r.connections[n].con_handle,
			(r.connections[n].link_type == NG_HCI_LINK_ACL)?
				"ACL" : "SCO",
			r.connections[n].mode,
			(r.connections[n].role == NG_HCI_ROLE_MASTER)?
				"MAST" : "SLAV",
			hci_encrypt2str(r.connections[n].encryption_mode, 1),
			r.connections[n].pending,
			r.connections[n].queue_len,
			hci_con_state2str(r.connections[n].state));
	}
out:
	free(r.connections);

	return (error);
} /* hci_read_connection_list */

/* Send Read_Node_Link_Policy_Settings_Mask command to the node */
int
hci_read_node_link_policy_settings_mask(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_node_link_policy_mask	r;

	memset(&r, 0, sizeof(r));
	if (ioctl(s, SIOC_HCI_RAW_NODE_GET_LINK_POLICY_MASK, &r, sizeof(r)) < 0)
		return (ERROR);

	fprintf(stdout, "Link Policy Settings mask: %#04x\n", r.policy_mask);

	return (OK);
} /* hci_read_node_link_policy_settings_mask */

/* Send Write_Node_Link_Policy_Settings_Mask command to the node */
int
hci_write_node_link_policy_settings_mask(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_node_link_policy_mask	r;
	int							m;

	memset(&r, 0, sizeof(r));

	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%x", &m) != 1)
			return (USAGE);

		r.policy_mask = (m & 0xffff);
		break;

	default:
		return (USAGE);
	}

	if (ioctl(s, SIOC_HCI_RAW_NODE_SET_LINK_POLICY_MASK, &r, sizeof(r)) < 0)
		return (ERROR);

	return (OK);
} /* hci_write_node_link_policy_settings_mask */

/* Send Read_Node_Packet_Mask command to the node */
int
hci_read_node_packet_mask(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_node_packet_mask	r;

	memset(&r, 0, sizeof(r));
	if (ioctl(s, SIOC_HCI_RAW_NODE_GET_PACKET_MASK, &r, sizeof(r)) < 0)
		return (ERROR);

	fprintf(stdout, "Packet mask: %#04x\n", r.packet_mask);

	return (OK);
} /* hci_read_node_packet_mask */

/* Send Write_Node_Packet_Mask command to the node */
int
hci_write_node_packet_mask(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_node_packet_mask	r;
	int						m;

	memset(&r, 0, sizeof(r));

	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%x", &m) != 1)
			return (USAGE);

		r.packet_mask = (m & 0xffff);
		break;

	default:
		return (USAGE);
	}

	if (ioctl(s, SIOC_HCI_RAW_NODE_SET_PACKET_MASK, &r, sizeof(r)) < 0)
		return (ERROR);

	return (OK);
} /* hci_write_node_packet_mask */

/* Send Read_Node_Role_Switch command to the node */
int
hci_read_node_role_switch(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_node_role_switch	r;

	memset(&r, 0, sizeof(r));
	if (ioctl(s, SIOC_HCI_RAW_NODE_GET_ROLE_SWITCH, &r, sizeof(r)) < 0)
		return (ERROR);

	fprintf(stdout, "Role switch: %d\n", r.role_switch);

	return (OK);
} /* hci_read_node_role_switch */

/* Send Write_Node_Role_Switch command to the node */
int
hci_write_node_role_switch(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_node_role_switch	r;
	int						m;

	memset(&r, 0, sizeof(r));

	switch (argc) {
	case 1:
		if (sscanf(argv[0], "%d", &m) != 1)
			return (USAGE);

		r.role_switch = m? 1 : 0;
		break;

	default:
		return (USAGE);
	}

	if (ioctl(s, SIOC_HCI_RAW_NODE_SET_ROLE_SWITCH, &r, sizeof(r)) < 0)
		return (ERROR);

	return (OK);
} /* hci_write_node_role_switch */

/* Send Read_Node_List command to the node */
int
hci_read_node_list(int s, int argc, char **argv)
{
	struct ng_btsocket_hci_raw_node_list_names	r;
	int						i;

	r.num_names = MAX_NODE_NUM;
	r.names = (struct nodeinfo*)calloc(MAX_NODE_NUM, sizeof(struct nodeinfo));
	if (r.names == NULL)
		return (ERROR);

	if (ioctl(s, SIOC_HCI_RAW_NODE_LIST_NAMES, &r, sizeof(r)) < 0) {
		free(r.names);
		return (ERROR);
	}

	fprintf(stdout, "Name            ID       Num hooks\n");
	for (i = 0; i < r.num_names; ++i)
		fprintf(stdout, "%-15s %08x %9d\n",
		    r.names[i].name, r.names[i].id, r.names[i].hooks);

	free(r.names);

	return (OK);
} /* hci_read_node_list */

struct hci_command	node_commands[] = {
{
"read_node_state",
"Get the HCI node state",
&hci_read_node_state
},
{
"initialize",
"Initialize the HCI node",
&hci_node_initialize
},
{
"read_debug_level",
"Read the HCI node debug level",
&hci_read_debug_level
},
{
"write_debug_level <level>",
"Write the HCI node debug level",
&hci_write_debug_level
},
{
"read_node_buffer_size",
"Read the HCI node buffer information. This will return current state of the\n"\
"HCI buffer for the HCI node",
&hci_read_node_buffer_size
},
{
"read_node_bd_addr",
"Read the HCI node BD_ADDR. Returns device BD_ADDR as cached by the HCI node",
&hci_read_node_bd_addr
},
{
"read_node_features",
"Read the HCI node features. This will return list of supported features as\n" \
"cached by the HCI node",
&hci_read_node_features
},
{
"read_node_stat",
"Read packets and bytes counters for the HCI node",
&hci_read_node_stat
},
{
"reset_node_stat",
"Reset packets and bytes counters for the HCI node",
&hci_reset_node_stat
},
{
"flush_neighbor_cache",
"Flush content of the HCI node neighbor cache",
&hci_flush_neighbor_cache
},
{
"read_neighbor_cache",
"Read content of the HCI node neighbor cache",
&hci_read_neighbor_cache
},
{
"read_connection_list",
"Read the baseband connection descriptors list for the HCI node",
&hci_read_connection_list
},
{
"read_node_link_policy_settings_mask",
"Read the value of the Link Policy Settinngs mask for the HCI node",
&hci_read_node_link_policy_settings_mask
},
{
"write_node_link_policy_settings_mask <policy_mask>",
"Write the value of the Link Policy Settings mask for the HCI node. By default\n" \
"all supported Link Policy modes (as reported by the local device features) are\n"\
"enabled. The particular Link Policy mode is enabled if local device supports\n"\
"it and correspinding bit in the mask was set\n\n" \
"\t<policy_mask> - xxxx; Link Policy mask\n" \
"\t\t0x0000 - Disable All LM Modes\n" \
"\t\t0x0001 - Enable Master Slave Switch\n" \
"\t\t0x0002 - Enable Hold Mode\n" \
"\t\t0x0004 - Enable Sniff Mode\n" \
"\t\t0x0008 - Enable Park Mode\n",
&hci_write_node_link_policy_settings_mask
},
{
"read_node_packet_mask",
"Read the value of the Packet mask for the HCI node",
&hci_read_node_packet_mask
},
{
"write_node_packet_mask <packet_mask>",
"Write the value of the Packet mask for the HCI node. By default all supported\n" \
"packet types (as reported by the local device features) are enabled. The\n" \
"particular packet type is enabled if local device supports it and corresponding\n" \
"bit in the mask was set\n\n" \
"\t<packet_mask> - xxxx; packet type mask\n" \
"" \
"\t\tACL packets\n" \
"\t\t-----------\n" \
"\t\t0x0008 DM1\n" \
"\t\t0x0010 DH1\n" \
"\t\t0x0400 DM3\n" \
"\t\t0x0800 DH3\n" \
"\t\t0x4000 DM5\n" \
"\t\t0x8000 DH5\n" \
"\n" \
"\t\tSCO packets\n" \
"\t\t-----------\n" \
"\t\t0x0020 HV1\n" \
"\t\t0x0040 HV2\n" \
"\t\t0x0080 HV3\n",
&hci_write_node_packet_mask
},
{
"read_node_role_switch",
"Read the value of the Role Switch parameter for the HCI node",
&hci_read_node_role_switch
},
{
"write_node_role_switch {0|1}",
"Write the value of the Role Switch parameter for the HCI node. By default,\n" \
"if Role Switch is supported, local device will try to perform Role Switch\n" \
"and become Master on incoming connection. Some devices do not support Role\n" \
"Switch and thus incoming connections from such devices will fail. Setting\n" \
"this parameter to zero will prevent Role Switch and thus accepting device\n" \
"will remain Slave",
&hci_write_node_role_switch
},
{
"read_node_list",
"Get a list of HCI nodes, their Netgraph IDs and connected hooks.",
&hci_read_node_list
},
{
NULL,
}};

