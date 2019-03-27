/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2004, 2005 Intel Corporation.  All rights reserved.
 * Copyright (c) 2005, 2006 Voltaire Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directorY of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id$
 */

#define	LINUXKPI_PARAM_PREFIX ib_madeye_

#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>

#include <rdma/ib_mad.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_sa.h>

MODULE_AUTHOR("Sean Hefty");
MODULE_DESCRIPTION("InfiniBand MAD viewer");
MODULE_LICENSE("Dual BSD/GPL");

static void madeye_remove_one(struct ib_device *device);
static void madeye_add_one(struct ib_device *device);

static struct ib_client madeye_client = {
	.name   = "madeye",
	.add    = madeye_add_one,
	.remove = madeye_remove_one
};

struct madeye_port {
	struct ib_mad_agent *smi_agent;
	struct ib_mad_agent *gsi_agent;
};

static int smp = 1;
static int gmp = 1;
static int mgmt_class = 0;
static int attr_id = 0;
static int data = 0;

module_param(smp, int, 0444);
module_param(gmp, int, 0444);
module_param(mgmt_class, int, 0444);
module_param(attr_id, int, 0444);
module_param(data, int, 0444);

MODULE_PARM_DESC(smp, "Display all SMPs (default=1)");
MODULE_PARM_DESC(gmp, "Display all GMPs (default=1)");
MODULE_PARM_DESC(mgmt_class, "Display all MADs of specified class (default=0)");
MODULE_PARM_DESC(attr_id, "Display add MADs of specified attribute ID (default=0)");
MODULE_PARM_DESC(data, "Display data area of MADs (default=0)");

static char * get_class_name(u8 mgmt_class)
{
	switch(mgmt_class) {
	case IB_MGMT_CLASS_SUBN_LID_ROUTED:
		return "LID routed SMP";
	case IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE:
		return "Directed route SMP";
	case IB_MGMT_CLASS_SUBN_ADM:
		return "Subnet admin.";
	case IB_MGMT_CLASS_PERF_MGMT:
		return "Perf. mgmt.";
	case IB_MGMT_CLASS_BM:
		return "Baseboard mgmt.";
	case IB_MGMT_CLASS_DEVICE_MGMT:
		return "Device mgmt.";
	case IB_MGMT_CLASS_CM:
		return "Comm. mgmt.";
	case IB_MGMT_CLASS_SNMP:
		return "SNMP";
	default:
		return "Unknown vendor/application";
	}
}

static char * get_method_name(u8 mgmt_class, u8 method)
{
	switch(method) {
	case IB_MGMT_METHOD_GET:
		return "Get";
	case IB_MGMT_METHOD_SET:
		return "Set";
	case IB_MGMT_METHOD_GET_RESP:
		return "Get response";
	case IB_MGMT_METHOD_SEND:
		return "Send";
	case IB_MGMT_METHOD_SEND | IB_MGMT_METHOD_RESP:
		return "Send response";
	case IB_MGMT_METHOD_TRAP:
		return "Trap";
	case IB_MGMT_METHOD_REPORT:
		return "Report";
	case IB_MGMT_METHOD_REPORT_RESP:
		return "Report response";
	case IB_MGMT_METHOD_TRAP_REPRESS:
		return "Trap repress";
	default:
		break;
	}

	switch (mgmt_class) {
	case IB_MGMT_CLASS_SUBN_ADM:
		switch (method) {
		case IB_SA_METHOD_GET_TABLE:
			return "Get table";
		case IB_SA_METHOD_GET_TABLE_RESP:
			return "Get table response";
		case IB_SA_METHOD_DELETE:
			return "Delete";
		case IB_SA_METHOD_DELETE_RESP:
			return "Delete response";
		case IB_SA_METHOD_GET_MULTI:
			return "Get Multi";
		case IB_SA_METHOD_GET_MULTI_RESP:
			return "Get Multi response";
		case IB_SA_METHOD_GET_TRACE_TBL:
			return "Get Trace Table response";
		default:
			break;
		}
	default:
		break;
	}

	return "Unknown";
}

static void print_status_details(u16 status)
{
	if (status & 0x0001)
		printk("               busy\n");
	if (status & 0x0002)
		printk("               redirection required\n");
	switch((status & 0x001C) >> 2) {
	case 1:
		printk("               bad version\n");
		break;
	case 2:
		printk("               method not supported\n");
		break;
	case 3:
		printk("               method/attribute combo not supported\n");
		break;
	case 7:
		printk("               invalid attribute/modifier value\n");
		break;
	}
}

static char * get_sa_attr(__be16 attr)
{
	switch(attr) {
	case IB_SA_ATTR_CLASS_PORTINFO:
		return "Class Port Info";
	case IB_SA_ATTR_NOTICE:
		return "Notice";
	case IB_SA_ATTR_INFORM_INFO:
		return "Inform Info";
	case IB_SA_ATTR_NODE_REC:
		return "Node Record";
	case IB_SA_ATTR_PORT_INFO_REC:
		return "PortInfo Record";
	case IB_SA_ATTR_SL2VL_REC:
		return "SL to VL Record";
	case IB_SA_ATTR_SWITCH_REC:
		return "Switch Record";
	case IB_SA_ATTR_LINEAR_FDB_REC:
		return "Linear FDB Record";
	case IB_SA_ATTR_RANDOM_FDB_REC:
		return "Random FDB Record";
	case IB_SA_ATTR_MCAST_FDB_REC:
		return "Multicast FDB Record";
	case IB_SA_ATTR_SM_INFO_REC:
		return "SM Info Record";
	case IB_SA_ATTR_LINK_REC:
		return "Link Record";
	case IB_SA_ATTR_GUID_INFO_REC:
		return "Guid Info Record";
	case IB_SA_ATTR_SERVICE_REC:
		return "Service Record";
	case IB_SA_ATTR_PARTITION_REC:
		return "Partition Record";
	case IB_SA_ATTR_PATH_REC:
		return "Path Record";
	case IB_SA_ATTR_VL_ARB_REC:
		return "VL Arb Record";
	case IB_SA_ATTR_MC_MEMBER_REC:
		return "MC Member Record";
	case IB_SA_ATTR_TRACE_REC:
		return "Trace Record";
	case IB_SA_ATTR_MULTI_PATH_REC:
		return "Multi Path Record";
	case IB_SA_ATTR_SERVICE_ASSOC_REC:
		return "Service Assoc Record";
	case IB_SA_ATTR_INFORM_INFO_REC:
		return "Inform Info Record";
	default:
		return "";
	}
}

static void print_mad_hdr(struct ib_mad_hdr *mad_hdr)
{
	printk("MAD version....0x%01x\n", mad_hdr->base_version);
	printk("Class..........0x%01x (%s)\n", mad_hdr->mgmt_class,
	       get_class_name(mad_hdr->mgmt_class));
	printk("Class version..0x%01x\n", mad_hdr->class_version);
	printk("Method.........0x%01x (%s)\n", mad_hdr->method,
	       get_method_name(mad_hdr->mgmt_class, mad_hdr->method));
	printk("Status.........0x%02x\n", be16_to_cpu(mad_hdr->status));
	if (mad_hdr->status)
		print_status_details(be16_to_cpu(mad_hdr->status));
	printk("Class specific.0x%02x\n", be16_to_cpu(mad_hdr->class_specific));
	printk("Trans ID.......0x%llx\n", 
		(unsigned long long)be64_to_cpu(mad_hdr->tid));
	if (mad_hdr->mgmt_class == IB_MGMT_CLASS_SUBN_ADM)
		printk("Attr ID........0x%02x (%s)\n",
		       be16_to_cpu(mad_hdr->attr_id),
		       get_sa_attr(be16_to_cpu(mad_hdr->attr_id)));
	else
		printk("Attr ID........0x%02x\n",
		       be16_to_cpu(mad_hdr->attr_id));
	printk("Attr modifier..0x%04x\n", be32_to_cpu(mad_hdr->attr_mod));
}

static char * get_rmpp_type(u8 rmpp_type)
{
	switch (rmpp_type) {
	case IB_MGMT_RMPP_TYPE_DATA:
		return "Data";
	case IB_MGMT_RMPP_TYPE_ACK:
		return "Ack";
	case IB_MGMT_RMPP_TYPE_STOP:
		return "Stop";
	case IB_MGMT_RMPP_TYPE_ABORT:
		return "Abort";
	default:
		return "Unknown";
	}
}

static char * get_rmpp_flags(u8 rmpp_flags)
{
	if (rmpp_flags & IB_MGMT_RMPP_FLAG_ACTIVE)
		if (rmpp_flags & IB_MGMT_RMPP_FLAG_FIRST)
			if (rmpp_flags & IB_MGMT_RMPP_FLAG_LAST)
				return "Active - First & Last";
			else
				return "Active - First";
		else
			if (rmpp_flags & IB_MGMT_RMPP_FLAG_LAST)
				return "Active - Last";
			else
				return "Active";
	else
		return "Inactive";
}

static void print_rmpp_hdr(struct ib_rmpp_hdr *rmpp_hdr)
{
	printk("RMPP version...0x%01x\n", rmpp_hdr->rmpp_version);
	printk("RMPP type......0x%01x (%s)\n", rmpp_hdr->rmpp_type,
	       get_rmpp_type(rmpp_hdr->rmpp_type));
	printk("RMPP RRespTime.0x%01x\n", ib_get_rmpp_resptime(rmpp_hdr));
	printk("RMPP flags.....0x%01x (%s)\n", ib_get_rmpp_flags(rmpp_hdr),
	       get_rmpp_flags(ib_get_rmpp_flags(rmpp_hdr)));
	printk("RMPP status....0x%01x\n", rmpp_hdr->rmpp_status);
	printk("Seg number.....0x%04x\n", be32_to_cpu(rmpp_hdr->seg_num));
	switch (rmpp_hdr->rmpp_type) {
	case IB_MGMT_RMPP_TYPE_DATA:
		printk("Payload len....0x%04x\n",
		       be32_to_cpu(rmpp_hdr->paylen_newwin));
		break;
	case IB_MGMT_RMPP_TYPE_ACK:
		printk("New window.....0x%04x\n",
		       be32_to_cpu(rmpp_hdr->paylen_newwin));
		break;
	default:
		printk("Data 2.........0x%04x\n",
		       be32_to_cpu(rmpp_hdr->paylen_newwin));
		break;
	}
}

static char * get_smp_attr(__be16 attr)
{
	switch (attr) {
	case IB_SMP_ATTR_NOTICE:
		return "notice";
	case IB_SMP_ATTR_NODE_DESC:
		return "node description";
	case IB_SMP_ATTR_NODE_INFO:
		return "node info";
	case IB_SMP_ATTR_SWITCH_INFO:
		return "switch info";
	case IB_SMP_ATTR_GUID_INFO:
		return "GUID info";
	case IB_SMP_ATTR_PORT_INFO:
		return "port info";
	case IB_SMP_ATTR_PKEY_TABLE:
		return "pkey table";
	case IB_SMP_ATTR_SL_TO_VL_TABLE:
		return "SL to VL table";
	case IB_SMP_ATTR_VL_ARB_TABLE:
		return "VL arbitration table";
	case IB_SMP_ATTR_LINEAR_FORWARD_TABLE:
		return "linear forwarding table";
	case IB_SMP_ATTR_RANDOM_FORWARD_TABLE:
		return "random forward table";
	case IB_SMP_ATTR_MCAST_FORWARD_TABLE:
		return "multicast forward table";
	case IB_SMP_ATTR_SM_INFO:
		return "SM info";
	case IB_SMP_ATTR_VENDOR_DIAG:
		return "vendor diags";
	case IB_SMP_ATTR_LED_INFO:
		return "LED info";
	default:
		return "";
	}
}

static void print_smp(struct ib_smp *smp)
{
	int i;

	printk("MAD version....0x%01x\n", smp->base_version);
	printk("Class..........0x%01x (%s)\n", smp->mgmt_class,
	       get_class_name(smp->mgmt_class));
	printk("Class version..0x%01x\n", smp->class_version);
	printk("Method.........0x%01x (%s)\n", smp->method,
	       get_method_name(smp->mgmt_class, smp->method));
	printk("Status.........0x%02x\n", be16_to_cpu(smp->status));
	if (smp->status)
		print_status_details(be16_to_cpu(smp->status));
	printk("Hop pointer....0x%01x\n", smp->hop_ptr);
	printk("Hop counter....0x%01x\n", smp->hop_cnt);
	printk("Trans ID.......0x%llx\n", 
		(unsigned long long)be64_to_cpu(smp->tid));
	printk("Attr ID........0x%02x (%s)\n", be16_to_cpu(smp->attr_id),
		get_smp_attr(smp->attr_id));
	printk("Attr modifier..0x%04x\n", be32_to_cpu(smp->attr_mod));

	printk("Mkey...........0x%llx\n",
		(unsigned long long)be64_to_cpu(smp->mkey));
	printk("DR SLID........0x%02x\n", be16_to_cpu(smp->dr_slid));
	printk("DR DLID........0x%02x", be16_to_cpu(smp->dr_dlid));

	if (data) {
		for (i = 0; i < IB_SMP_DATA_SIZE; i++) {
			if (i % 16 == 0)
				printk("\nSMP Data.......");
			printk("%01x ", smp->data[i]);
		}
		for (i = 0; i < IB_SMP_MAX_PATH_HOPS; i++) {
			if (i % 16 == 0)
				printk("\nInitial path...");
			printk("%01x ", smp->initial_path[i]);
		}
		for (i = 0; i < IB_SMP_MAX_PATH_HOPS; i++) {
			if (i % 16 == 0)
				printk("\nReturn path....");
			printk("%01x ", smp->return_path[i]);
		}
	}
	printk("\n");
}

static void snoop_smi_handler(struct ib_mad_agent *mad_agent,
			      struct ib_mad_send_buf *send_buf,
			      struct ib_mad_send_wc *mad_send_wc)
{
	struct ib_mad_hdr *hdr = send_buf->mad;

	if (!smp && hdr->mgmt_class != mgmt_class)
		return;
	if (attr_id && be16_to_cpu(hdr->attr_id) != attr_id)
		return;

	printk("Madeye:sent SMP\n");
	print_smp(send_buf->mad);
}

static void recv_smi_handler(struct ib_mad_agent *mad_agent,
			     struct ib_mad_recv_wc *mad_recv_wc)
{
	if (!smp && mad_recv_wc->recv_buf.mad->mad_hdr.mgmt_class != mgmt_class)
		return;
	if (attr_id && be16_to_cpu(mad_recv_wc->recv_buf.mad->mad_hdr.attr_id) != attr_id)
		return;

	printk("Madeye:recv SMP\n");
	print_smp((struct ib_smp *)&mad_recv_wc->recv_buf.mad->mad_hdr);
}

static int is_rmpp_mad(struct ib_mad_hdr *mad_hdr)
{
	if (mad_hdr->mgmt_class == IB_MGMT_CLASS_SUBN_ADM) {
		switch (mad_hdr->method) {
		case IB_SA_METHOD_GET_TABLE:
		case IB_SA_METHOD_GET_TABLE_RESP:
		case IB_SA_METHOD_GET_MULTI_RESP:
			return 1;
		default:
			break;
		}
	} else if ((mad_hdr->mgmt_class >= IB_MGMT_CLASS_VENDOR_RANGE2_START) &&
		   (mad_hdr->mgmt_class <= IB_MGMT_CLASS_VENDOR_RANGE2_END))
		return 1;

	return 0;
}

static void snoop_gsi_handler(struct ib_mad_agent *mad_agent,
			      struct ib_mad_send_buf *send_buf,
			      struct ib_mad_send_wc *mad_send_wc)
{
	struct ib_mad_hdr *hdr = send_buf->mad;

	if (!gmp && hdr->mgmt_class != mgmt_class)
		return;
	if (attr_id && be16_to_cpu(hdr->attr_id) != attr_id)
		return;

	printk("Madeye:sent GMP\n");
	print_mad_hdr(hdr);

	if (is_rmpp_mad(hdr))
		print_rmpp_hdr(&((struct ib_rmpp_mad *) hdr)->rmpp_hdr);
}

static void recv_gsi_handler(struct ib_mad_agent *mad_agent,
			     struct ib_mad_recv_wc *mad_recv_wc)
{
	struct ib_mad_hdr *hdr = &mad_recv_wc->recv_buf.mad->mad_hdr;
	struct ib_rmpp_mad *mad = NULL;
	struct ib_sa_mad *sa_mad;
	struct ib_vendor_mad *vendor_mad;
	u8 *mad_data;
	int i, j;

	if (!gmp && hdr->mgmt_class != mgmt_class)
		return;
	if (attr_id && be16_to_cpu(mad_recv_wc->recv_buf.mad->mad_hdr.attr_id) != attr_id)
		return;

	printk("Madeye:recv GMP\n");
	print_mad_hdr(hdr);

	if (is_rmpp_mad(hdr)) {
		mad = (struct ib_rmpp_mad *) hdr;
		print_rmpp_hdr(&mad->rmpp_hdr);
	}

	if (data) {
		if (hdr->mgmt_class == IB_MGMT_CLASS_SUBN_ADM) {
			j = IB_MGMT_SA_DATA;
			/* Display SA header */
			if (is_rmpp_mad(hdr) &&
			    mad->rmpp_hdr.rmpp_type != IB_MGMT_RMPP_TYPE_DATA)
				return;
			sa_mad = (struct ib_sa_mad *)
				 &mad_recv_wc->recv_buf.mad;
			mad_data = sa_mad->data;
		} else {
			if (is_rmpp_mad(hdr)) {
				j = IB_MGMT_VENDOR_DATA;
				/* Display OUI */
				vendor_mad = (struct ib_vendor_mad *)
					     &mad_recv_wc->recv_buf.mad;
				printk("Vendor OUI......%01x %01x %01x\n",
					vendor_mad->oui[0],
					vendor_mad->oui[1],
					vendor_mad->oui[2]);
				mad_data = vendor_mad->data;
			} else {
				j = IB_MGMT_MAD_DATA;
				mad_data = mad_recv_wc->recv_buf.mad->data;
			}
		}
		for (i = 0; i < j; i++) {
			if (i % 16 == 0)
				printk("\nData...........");
			printk("%01x ", mad_data[i]);
		}
		printk("\n");
	}
}

static void madeye_add_one(struct ib_device *device)
{
	struct madeye_port *port;
	int reg_flags;
	u8 i, s, e;

	if (device->node_type == RDMA_NODE_IB_SWITCH) {
		s = 0;
		e = 0;
	} else {
		s = 1;
		e = device->phys_port_cnt;
	}

	port = kmalloc(sizeof *port * (e - s + 1), GFP_KERNEL);
	if (!port)
		goto out;

	reg_flags = IB_MAD_SNOOP_SEND_COMPLETIONS | IB_MAD_SNOOP_RECVS;
	for (i = 0; i <= e - s; i++) {
		port[i].smi_agent = ib_register_mad_snoop(device, i + s,
							  IB_QPT_SMI,
							  reg_flags,
							  snoop_smi_handler,
							  recv_smi_handler,
							  &port[i]);
		port[i].gsi_agent = ib_register_mad_snoop(device, i + s,
							  IB_QPT_GSI,
							  reg_flags,
							  snoop_gsi_handler,
							  recv_gsi_handler,
							  &port[i]);
	}

out:
	ib_set_client_data(device, &madeye_client, port);
}

static void madeye_remove_one(struct ib_device *device)
{
	struct madeye_port *port;
	int i, s, e;

	port = (struct madeye_port *)
		ib_get_client_data(device, &madeye_client);
	if (!port)
		return;

	if (device->node_type == RDMA_NODE_IB_SWITCH) {
		s = 0;
		e = 0;
	} else {
		s = 1;
		e = device->phys_port_cnt;
	}

	for (i = 0; i <= e - s; i++) {
		if (!IS_ERR(port[i].smi_agent))
			ib_unregister_mad_agent(port[i].smi_agent);
		if (!IS_ERR(port[i].gsi_agent))
			ib_unregister_mad_agent(port[i].gsi_agent);
	}
	kfree(port);
}

static int __init ib_madeye_init(void)
{
	return ib_register_client(&madeye_client);
}

static void __exit ib_madeye_cleanup(void)
{
	ib_unregister_client(&madeye_client);
}

module_init(ib_madeye_init);
module_exit(ib_madeye_cleanup);
