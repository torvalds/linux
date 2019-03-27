/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 2002
 * 	Hidetoshi Shimokawa. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *
 *	This product includes software developed by Hidetoshi Shimokawa.
 *
 * 4. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(__FreeBSD__)
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#endif

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#if defined(__FreeBSD__)
#include <sys/eui64.h>
#include <dev/firewire/firewire.h>
#include <dev/firewire/iec13213.h>
#include <dev/firewire/fwphyreg.h>
#include <dev/firewire/iec68113.h>
#elif defined(__NetBSD__)
#include "eui64.h"
#include <dev/ieee1394/firewire.h>
#include <dev/ieee1394/iec13213.h>
#include <dev/ieee1394/fwphyreg.h>
#include <dev/ieee1394/iec68113.h>
#else
#warning "You need to add support for your OS"
#endif


#include <netinet/in.h>
#include <fcntl.h>
#include <stdio.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include "fwmethods.h"

static void sysctl_set_int(const char *, int);

static void
usage(void)
{
	fprintf(stderr,
		"%s [-u bus_num] [-prt] [-c node] [-d node] [-o node] [-s node]\n"
		"\t  [-l file] [-g gap_count] [-f force_root ] [-b pri_req]\n"
		"\t  [-M mode] [-R filename] [-S filename] [-m EUI64 | hostname]\n"
		"\t-u: specify bus number\n"
		"\t-p: Display current PHY register settings\n"
		"\t-r: bus reset\n"
		"\t-t: read topology map\n"
		"\t-c: read configuration ROM\n"
		"\t-d: hex dump of configuration ROM\n"
		"\t-o: send link-on packet to the node\n"
		"\t-s: write RESET_START register on the node\n"
		"\t-l: load and parse hex dump file of configuration ROM\n"
		"\t-g: set gap count\n"
		"\t-f: force root node\n"
		"\t-b: set PRIORITY_BUDGET register on all supported nodes\n"
		"\t-M: specify dv or mpeg\n"
		"\t-R: Receive DV or MPEG TS stream\n"
		"\t-S: Send DV stream\n"
		"\t-m: set fwmem target\n"
		, getprogname() );
	fprintf(stderr, "\n");
}

static void
fweui2eui64(const struct fw_eui64 *fweui, struct eui64 *eui)
{
	*(u_int32_t*)&(eui->octet[0]) = htonl(fweui->hi);
	*(u_int32_t*)&(eui->octet[4]) = htonl(fweui->lo);
}

static void
get_dev(int fd, struct fw_devlstreq *data)
{
	if (data == NULL)
		err(EX_SOFTWARE, "%s: data malloc", __func__);
	if( ioctl(fd, FW_GDEVLST, data) < 0) {
       			err(EX_IOERR, "%s: ioctl", __func__);
	}
}

static int
str2node(int fd, const char *nodestr)
{
	struct eui64 eui, tmpeui;
	struct fw_devlstreq *data;
	char *endptr;
	int i, node;

	if (nodestr == '\0')
		return (-1);

	/*
	 * Deal with classic node specifications.
	 */
	node = strtol(nodestr, &endptr, 0);
	if (*endptr == '\0')
		goto gotnode;

	/*
	 * Try to get an eui and match it against available nodes.
	 */
	if (eui64_hostton(nodestr, &eui) != 0 && eui64_aton(nodestr, &eui) != 0)
		return (-1);

	data = (struct fw_devlstreq *)malloc(sizeof(struct fw_devlstreq));
	if (data == NULL)
		err(EX_SOFTWARE, "%s: data malloc", __func__);
	get_dev(fd,data);

	for (i = 0; i < data->info_len; i++) {
		fweui2eui64(&data->dev[i].eui, &tmpeui);
		if (memcmp(&eui, &tmpeui, sizeof(struct eui64)) == 0) {
			node = data->dev[i].dst;
			free(data);
			goto gotnode;
		}
	}
	if (i >= data->info_len) {
		if (data != NULL)
			free(data);
		return (-1);
	}

gotnode:
	if (node < 0 || node > 63)
		return (-1);
	else
		return (node);
}

static void
list_dev(int fd)
{
	struct fw_devlstreq *data;
	struct fw_devinfo *devinfo;
	struct eui64 eui;
	char addr[EUI64_SIZ], hostname[40];
	int i;

	data = (struct fw_devlstreq *)malloc(sizeof(struct fw_devlstreq));
	if (data == NULL)
		err(EX_SOFTWARE, "%s:data malloc", __func__);
	get_dev(fd, data);
	printf("%d devices (info_len=%d)\n", data->n, data->info_len);
	printf("node           EUI64          status    hostname\n");
	for (i = 0; i < data->info_len; i++) {
		devinfo = &data->dev[i];
		fweui2eui64(&devinfo->eui, &eui);
		eui64_ntoa(&eui, addr, sizeof(addr));
	        if (eui64_ntohost(hostname, sizeof(hostname), &eui))
			hostname[0] = 0;
		printf("%4d  %s %6d    %s\n",
			(devinfo->status || i == 0) ? devinfo->dst : -1,
			addr,
			devinfo->status,
			hostname
		);
	}
	free((void *)data);
}

static u_int32_t
read_write_quad(int fd, struct fw_eui64 eui, u_int32_t addr_lo, int readmode, u_int32_t data)
{
        struct fw_asyreq *asyreq;
	u_int32_t *qld, res;

        asyreq = (struct fw_asyreq *)malloc(sizeof(struct fw_asyreq_t) + 16);
	if (asyreq == NULL)
		err(EX_SOFTWARE, "%s:asyreq malloc", __func__);
	asyreq->req.len = 16;
#if 0
	asyreq->req.type = FWASREQNODE;
	asyreq->pkt.mode.rreqq.dst = FWLOCALBUS | node;
#else
	asyreq->req.type = FWASREQEUI;
	asyreq->req.dst.eui = eui;
#endif
	asyreq->pkt.mode.rreqq.tlrt = 0;
	if (readmode)
		asyreq->pkt.mode.rreqq.tcode = FWTCODE_RREQQ;
	else
		asyreq->pkt.mode.rreqq.tcode = FWTCODE_WREQQ;

	asyreq->pkt.mode.rreqq.dest_hi = 0xffff;
	asyreq->pkt.mode.rreqq.dest_lo = addr_lo;

	qld = (u_int32_t *)&asyreq->pkt;
	if (!readmode)
		asyreq->pkt.mode.wreqq.data = htonl(data);

	if (ioctl(fd, FW_ASYREQ, asyreq) < 0) {
       		err(EX_IOERR, "%s: ioctl", __func__);
	}
	res = qld[3];
	free(asyreq);
	if (readmode)
		return ntohl(res);
	else
		return 0;
}

/*
 * Send a PHY Config Packet
 * ieee 1394a-2005 4.3.4.3
 *
 * Message ID   Root ID    R  T   Gap Count
 * 00(2 bits)   (6 bits)   1  1   (6 bits) 
 *
 * if "R" is set, then Root ID will be the next
 * root node upon the next bus reset.
 * if "T" is set, then Gap Count will be the
 * value that all nodes use for their Gap Count
 * if "R" and "T" are not set, then this message
 * is either ignored or interpreted as an extended
 * PHY config Packet as per 1394a-2005 4.3.4.4
 */
static void
send_phy_config(int fd, int root_node, int gap_count)
{
        struct fw_asyreq *asyreq;

	asyreq = (struct fw_asyreq *)malloc(sizeof(struct fw_asyreq_t) + 12);
	if (asyreq == NULL)
		err(EX_SOFTWARE, "%s:asyreq malloc", __func__);
	asyreq->req.len = 12;
	asyreq->req.type = FWASREQNODE;
	asyreq->pkt.mode.ld[0] = 0;
	asyreq->pkt.mode.ld[1] = 0;
	asyreq->pkt.mode.common.tcode = FWTCODE_PHY;
	if (root_node >= 0)
		asyreq->pkt.mode.ld[1] |= ((root_node << 24) | (1 << 23));
	if (gap_count >= 0)
		asyreq->pkt.mode.ld[1] |= ((1 << 22) | (gap_count << 16));
	asyreq->pkt.mode.ld[2] = ~asyreq->pkt.mode.ld[1];

	printf("send phy_config root_node=%d gap_count=%d\n",
						root_node, gap_count);

	if (ioctl(fd, FW_ASYREQ, asyreq) < 0)
       		err(EX_IOERR, "%s: ioctl", __func__);
	free(asyreq);
}

static void
link_on(int fd, int node)
{
        struct fw_asyreq *asyreq;

	asyreq = (struct fw_asyreq *)malloc(sizeof(struct fw_asyreq_t) + 12);
	if (asyreq == NULL)
		err(EX_SOFTWARE, "%s:asyreq malloc", __func__);
	asyreq->req.len = 12;
	asyreq->req.type = FWASREQNODE;
	asyreq->pkt.mode.common.tcode = FWTCODE_PHY;
	asyreq->pkt.mode.ld[1] |= (1 << 30) | ((node & 0x3f) << 24);
	asyreq->pkt.mode.ld[2] = ~asyreq->pkt.mode.ld[1];

	if (ioctl(fd, FW_ASYREQ, asyreq) < 0)
       		err(EX_IOERR, "%s: ioctl", __func__);
	free(asyreq);
}

static void
reset_start(int fd, int node)
{
        struct fw_asyreq *asyreq;

	asyreq = (struct fw_asyreq *)malloc(sizeof(struct fw_asyreq_t) + 16);
	if (asyreq == NULL)
		err(EX_SOFTWARE, "%s:asyreq malloc", __func__);
	asyreq->req.len = 16;
	asyreq->req.type = FWASREQNODE;
	asyreq->pkt.mode.wreqq.dst = FWLOCALBUS | (node & 0x3f);
	asyreq->pkt.mode.wreqq.tlrt = 0;
	asyreq->pkt.mode.wreqq.tcode = FWTCODE_WREQQ;

	asyreq->pkt.mode.wreqq.dest_hi = 0xffff;
	asyreq->pkt.mode.wreqq.dest_lo = 0xf0000000 | RESET_START;

	asyreq->pkt.mode.wreqq.data = htonl(0x1);

	if (ioctl(fd, FW_ASYREQ, asyreq) < 0)
       		err(EX_IOERR, "%s: ioctl", __func__);
	free(asyreq);
}

static void
set_pri_req(int fd, u_int32_t pri_req)
{
	struct fw_devlstreq *data;
	struct fw_devinfo *devinfo;
	struct eui64 eui;
	char addr[EUI64_SIZ];
	u_int32_t max, reg, old;
	int i;

	data = (struct fw_devlstreq *)malloc(sizeof(struct fw_devlstreq));
	if (data == NULL)
		err(EX_SOFTWARE, "%s:data malloc", __func__);
	get_dev(fd, data);
#define BUGET_REG 0xf0000218
	for (i = 0; i < data->info_len; i++) {
		devinfo = &data->dev[i];
		if (!devinfo->status)
			continue;
		reg = read_write_quad(fd, devinfo->eui, BUGET_REG, 1, 0);
		fweui2eui64(&devinfo->eui, &eui);
		eui64_ntoa(&eui, addr, sizeof(addr));
		printf("%d %s, %08x",
			devinfo->dst, addr, reg);
		if (reg > 0) {
			old = (reg & 0x3f);
			max = (reg & 0x3f00) >> 8;
			if (pri_req > max)
				pri_req =  max;
			printf(" 0x%x -> 0x%x\n", old, pri_req);
			read_write_quad(fd, devinfo->eui, BUGET_REG, 0, pri_req);
		} else {
			printf("\n");
		}
	}
	free((void *)data);
}

static void
parse_bus_info_block(u_int32_t *p)
{
	char addr[EUI64_SIZ];
	struct bus_info *bi;
	struct eui64 eui;

	bi = (struct bus_info *)p;
	fweui2eui64(&bi->eui64, &eui);
	eui64_ntoa(&eui, addr, sizeof(addr));
	printf("bus_name: 0x%04x\n"
		"irmc:%d cmc:%d isc:%d bmc:%d pmc:%d\n"
		"cyc_clk_acc:%d max_rec:%d max_rom:%d\n"
		"generation:%d link_spd:%d\n"
		"EUI64: %s\n",
		bi->bus_name,
		bi->irmc, bi->cmc, bi->isc, bi->bmc, bi->pmc,
		bi->cyc_clk_acc, bi->max_rec, bi->max_rom,
		bi->generation, bi->link_spd,
		addr);
}

static int
get_crom(int fd, int node, void *crom_buf, int len)
{
	struct fw_crom_buf buf;
	int i, error;
	struct fw_devlstreq *data;

	data = (struct fw_devlstreq *)malloc(sizeof(struct fw_devlstreq));
	if (data == NULL)
		err(EX_SOFTWARE, "%s:data malloc", __func__);
	get_dev(fd, data);

	for (i = 0; i < data->info_len; i++) {
		if (data->dev[i].dst == node && data->dev[i].eui.lo != 0)
			break;
	}
	if (i == data->info_len)
		errx(1, "no such node %d.", node);
	else
		buf.eui = data->dev[i].eui;
	free((void *)data);

	buf.len = len;
	buf.ptr = crom_buf;
	bzero(crom_buf, len);
	if ((error = ioctl(fd, FW_GCROM, &buf)) < 0) {
       		err(EX_IOERR, "%s: ioctl", __func__);
	}

	return error;
}

static void
show_crom(u_int32_t *crom_buf)
{
	int i;
	struct crom_context cc;
	char *desc, info[256];
	static const char *key_types = "ICLD";
	struct csrreg *reg;
	struct csrdirectory *dir;
	struct csrhdr *hdr;
	u_int16_t crc;

	printf("first quad: 0x%08x ", *crom_buf);
	if (crom_buf[0] == 0) {
		printf("(Invalid Configuration ROM)\n");
		return;
	}
	hdr = (struct csrhdr *)crom_buf;
	if (hdr->info_len == 1) {
		/* minimum ROM */
		reg = (struct csrreg *)hdr;
		printf("verndor ID: 0x%06x\n",  reg->val);
		return;
	}
	printf("info_len=%d crc_len=%d crc=0x%04x",
		hdr->info_len, hdr->crc_len, hdr->crc);
	crc = crom_crc(crom_buf+1, hdr->crc_len);
	if (crc == hdr->crc)
		printf("(OK)\n");
	else
		printf("(NG)\n");
	parse_bus_info_block(crom_buf+1);

	crom_init_context(&cc, crom_buf);
	dir = cc.stack[0].dir;
	if (!dir) {
		printf("no root directory - giving up\n");
		return;
	}
	printf("root_directory: len=0x%04x(%d) crc=0x%04x",
			dir->crc_len, dir->crc_len, dir->crc);
	crc = crom_crc((u_int32_t *)&dir->entry[0], dir->crc_len);
	if (crc == dir->crc)
		printf("(OK)\n");
	else
		printf("(NG)\n");
	if (dir->crc_len < 1)
		return;
	while (cc.depth >= 0) {
		desc = crom_desc(&cc, info, sizeof(info));
		reg = crom_get(&cc);
		for (i = 0; i < cc.depth; i++)
			printf("\t");
		printf("%02x(%c:%02x) %06x %s: %s\n",
			reg->key,
			key_types[(reg->key & CSRTYPE_MASK)>>6],
			reg->key & CSRKEY_MASK, reg->val,
			desc, info);
		crom_next(&cc);
	}
}

#define DUMP_FORMAT	"%08x %08x %08x %08x %08x %08x %08x %08x\n"

static void
dump_crom(u_int32_t *p)
{
	int len=1024, i;

	for (i = 0; i < len/(4*8); i ++) {
		printf(DUMP_FORMAT,
			p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
		p += 8;
	}
}

static void
load_crom(char *filename, u_int32_t *p)
{
	FILE *file;
	int len=1024, i;

	if ((file = fopen(filename, "r")) == NULL)
	  err(1, "load_crom %s", filename);
	for (i = 0; i < len/(4*8); i ++) {
		fscanf(file, DUMP_FORMAT,
			p, p+1, p+2, p+3, p+4, p+5, p+6, p+7);
		p += 8;
	}
	fclose(file);
}

static void
show_topology_map(int fd)
{
	struct fw_topology_map *tmap;
	union fw_self_id sid;
	int i;
	static const char *port_status[] = {" ", "-", "P", "C"};
	static const char *pwr_class[] = {" 0W", "15W", "30W", "45W",
					"-1W", "-2W", "-5W", "-9W"};
	static const char *speed[] = {"S100", "S200", "S400", "S800"};
	tmap = malloc(sizeof(struct fw_topology_map));
	if (tmap == NULL)
		err(EX_SOFTWARE, "%s:tmap malloc", __func__);
	if (ioctl(fd, FW_GTPMAP, tmap) < 0) {
       		err(EX_IOERR, "%s: ioctl", __func__);
	}
	printf("crc_len: %d generation:%d node_count:%d sid_count:%d\n",
		tmap->crc_len, tmap->generation,
		tmap->node_count, tmap->self_id_count);
	printf("id link gap_cnt speed delay cIRM power port0 port1 port2"
		" ini more\n");
	for (i = 0; i < tmap->crc_len - 2; i++) {
		sid = tmap->self_id[i];
		if (sid.p0.sequel) {
			printf("%02d sequel packet\n", sid.p0.phy_id);
			continue;
		}
		printf("%02d   %2d      %2d  %4s     %d   %3s"
				"     %s     %s     %s   %d    %d\n",
			sid.p0.phy_id,
			sid.p0.link_active,
			sid.p0.gap_count,
			speed[sid.p0.phy_speed],
			sid.p0.contender,
			pwr_class[sid.p0.power_class],
			port_status[sid.p0.port0],
			port_status[sid.p0.port1],
			port_status[sid.p0.port2],
			sid.p0.initiated_reset,
			sid.p0.more_packets
		);
	}
	free(tmap);
}

static void
read_phy_registers(int fd, u_int8_t *buf, int offset, int len)
{
	struct fw_reg_req_t reg;
	int i;

	for (i = 0; i < len; i++) {
		reg.addr = offset + i;
		if (ioctl(fd, FWOHCI_RDPHYREG, &reg) < 0)
       			err(EX_IOERR, "%s: ioctl", __func__);
		buf[i] = (u_int8_t) reg.data;
		printf("0x%02x ",  reg.data);
	}
	printf("\n");
}

static void
read_phy_page(int fd, u_int8_t *buf, int page, int port)
{
	struct fw_reg_req_t reg;

	reg.addr = 0x7;
	reg.data = ((page & 7) << 5) | (port & 0xf);
	if (ioctl(fd, FWOHCI_WRPHYREG, &reg) < 0)
       		err(EX_IOERR, "%s: ioctl", __func__);
	read_phy_registers(fd, buf, 8, 8);
}

static void
dump_phy_registers(int fd)
{
	struct phyreg_base b;
	struct phyreg_page0 p;
	struct phyreg_page1 v;
	int i;

	printf("=== base register ===\n");
	read_phy_registers(fd, (u_int8_t *)&b, 0, 8);
	printf(
	    "Physical_ID:%d  R:%d  CPS:%d\n"
	    "RHB:%d  IBR:%d  Gap_Count:%d\n"
	    "Extended:%d Num_Ports:%d\n"
	    "PHY_Speed:%d Delay:%d\n"
	    "LCtrl:%d C:%d Jitter:%d Pwr_Class:%d\n"
	    "WDIE:%d ISBR:%d CTOI:%d CPSI:%d STOI:%d PEI:%d EAA:%d EMC:%d\n"
	    "Max_Legacy_SPD:%d BLINK:%d Bridge:%d\n"
	    "Page_Select:%d Port_Select%d\n",
	    b.phy_id, b.r, b.cps,
	    b.rhb, b.ibr, b.gap_count, 
	    b.extended, b.num_ports,
	    b.phy_speed, b.delay,
	    b.lctrl, b.c, b.jitter, b.pwr_class,
	    b.wdie, b.isbr, b.ctoi, b.cpsi, b.stoi, b.pei, b.eaa, b.emc,
	    b.legacy_spd, b.blink, b.bridge,
	    b.page_select, b.port_select
	);

	for (i = 0; i < b.num_ports; i ++) {
		printf("\n=== page 0 port %d ===\n", i);
		read_phy_page(fd, (u_int8_t *)&p, 0, i);
		printf(
		    "Astat:%d BStat:%d Ch:%d Con:%d RXOK:%d Dis:%d\n"
		    "Negotiated_speed:%d PIE:%d Fault:%d Stanby_fault:%d Disscrm:%d B_Only:%d\n"
		    "DC_connected:%d Max_port_speed:%d LPP:%d Cable_speed:%d\n"
		    "Connection_unreliable:%d Beta_mode:%d\n"
		    "Port_error:0x%x\n"
		    "Loop_disable:%d In_standby:%d Hard_disable:%d\n",
		    p.astat, p.bstat, p.ch, p.con, p.rxok, p.dis,
		    p.negotiated_speed, p.pie, p.fault, p.stanby_fault, p.disscrm, p.b_only,
		    p.dc_connected, p.max_port_speed, p.lpp, p.cable_speed,
		    p.connection_unreliable, p.beta_mode,
		    p.port_error,
		    p.loop_disable, p.in_standby, p.hard_disable
		);
	}
	printf("\n=== page 1 ===\n");
	read_phy_page(fd, (u_int8_t *)&v, 1, 0);
	printf(
	    "Compliance:%d\n"
	    "Vendor_ID:0x%06x\n"
	    "Product_ID:0x%06x\n",
	    v.compliance,
	    (v.vendor_id[0] << 16) | (v.vendor_id[1] << 8) | v.vendor_id[2],
	    (v.product_id[0] << 16) | (v.product_id[1] << 8) | v.product_id[2]
	);
}

static int
open_dev(int *fd, char *devname)
{
	if (*fd < 0) {
		*fd = open(devname, O_RDWR);
		if (*fd < 0)
			return(-1);

	}
	return(0);
}

static void
sysctl_set_int(const char *name, int val)
{
	if (sysctlbyname(name, NULL, NULL, &val, sizeof(int)) < 0)
		err(1, "sysctl %s failed.", name);
}

static fwmethod *
detect_recv_fn(int fd, char ich)
{
	char *buf;
	struct fw_isochreq isoreq;
	struct fw_isobufreq bufreq;
	int len;
	u_int32_t *ptr;
	struct ciphdr *ciph;
	fwmethod *retfn;
#define RECV_NUM_PACKET 16
#define RECV_PACKET_SZ  1024

	bufreq.rx.nchunk = 8;
	bufreq.rx.npacket = RECV_NUM_PACKET;
	bufreq.rx.psize = RECV_PACKET_SZ;
	bufreq.tx.nchunk = 0;
	bufreq.tx.npacket = 0;
	bufreq.tx.psize = 0;

	if (ioctl(fd, FW_SSTBUF, &bufreq) < 0)
		err(EX_IOERR, "%s: ioctl FW_SSTBUF", __func__);

	isoreq.ch = ich & 0x3f;
	isoreq.tag = (ich >> 6) & 3;

	if (ioctl(fd, FW_SRSTREAM, &isoreq) < 0)
		err(EX_IOERR, "%s: ioctl FW_SRSTREAM", __func__);

	buf = (char *)malloc(RECV_NUM_PACKET * RECV_PACKET_SZ);
	if (buf == NULL)
		err(EX_SOFTWARE, "%s:buf malloc", __func__);
	/*
	 * fwdev.c seems to return EIO on error and 
	 * the return value of the last uiomove 
	 * on success.  For now, checking that the 
	 * return is not less than zero should be
	 * sufficient.  fwdev.c::fw_read() should
	 * return the total length read, not the value
	 * of the last uiomove().
	 */
	len = read(fd, buf, RECV_NUM_PACKET * RECV_PACKET_SZ);
	if (len < 0)
		err(EX_IOERR, "%s: error reading from device", __func__);
	ptr = (u_int32_t *) buf;
	ciph = (struct ciphdr *)(ptr + 1);

	switch(ciph->fmt) {
		case CIP_FMT_DVCR:
			fprintf(stderr, "Detected DV format on input.\n");
			retfn = dvrecv;
			break;
		case CIP_FMT_MPEG:
			fprintf(stderr, "Detected MPEG TS format on input.\n");
			retfn = mpegtsrecv;
			break;
		default:
			errx(1, "Unsupported format for receiving: fmt=0x%x", ciph->fmt);
	}
	free(buf);
	return retfn;
}

int
main(int argc, char **argv)
{
#define MAX_BOARDS 10
	u_int32_t crom_buf[1024/4];
	u_int32_t crom_buf_hex[1024/4];
	char devbase[64];
	const char *device_string = "/dev/fw";
	int fd = -1, ch, len=1024;
	int32_t current_board = 0;
 /*
  * If !command_set, then -u will display the nodes for the board.
  * This emulates the previous behavior when -u is passed by itself
  */
	bool command_set = false;
	bool open_needed = false;
	long tmp;
	struct fw_eui64 eui;
	struct eui64 target;
	fwmethod *recvfn = NULL;
/*
 * Holders for which functions
 * to iterate through
 */
	bool display_board_only = false;
	bool display_crom = false;
	bool send_bus_reset = false;
	bool display_crom_hex = false;
	bool load_crom_from_file = false;
	bool set_fwmem_target = false;
	bool dump_topology = false;
	bool dump_phy_reg = false;

	int32_t priority_budget = -1;
	int32_t set_root_node = -1;
	int32_t set_gap_count = -1;
	int32_t send_link_on = -1;
	int32_t send_reset_start = -1;

	char *crom_string = NULL;
	char *crom_string_hex = NULL;
	char *recv_data = NULL;
	char *send_data = NULL;

	if (argc < 2) {
		for (current_board = 0; current_board < MAX_BOARDS; current_board++) {
			snprintf(devbase, sizeof(devbase), "%s%d.0", device_string, current_board);
			if (open_dev(&fd, devbase) < 0) {
				if (current_board == 0) {
					usage();
		  			err(EX_IOERR, "%s: Error opening firewire controller #%d %s",
						      __func__, current_board, devbase);
				}
				return(EIO);
			}
			list_dev(fd);
			close(fd);
			fd = -1;
		}
	}
       /*
	* Parse all command line options, then execute requested operations.
	*/
	while ((ch = getopt(argc, argv, "M:f:g:m:o:s:b:prtc:d:l:u:R:S:")) != -1) {
		switch(ch) {
		case 'b':
			priority_budget = strtol(optarg, NULL, 0);
			if (priority_budget < 0 || priority_budget > INT32_MAX)
				errx(EX_USAGE, "%s: priority_budget out of range: %s", __func__, optarg);
			command_set = true;
			open_needed = true;
			display_board_only = false;
			break;
		case 'c':
			crom_string = malloc(strlen(optarg)+1);
			if (crom_string == NULL)
				err(EX_SOFTWARE, "%s:crom_string malloc", __func__);
			if ( (strtol(crom_string, NULL, 0) < 0) || strtol(crom_string, NULL, 0) > MAX_BOARDS)
				errx(EX_USAGE, "%s:Invalid value for node", __func__);
			strcpy(crom_string, optarg);
			display_crom = 1;
			open_needed = true;
			command_set = true;
			display_board_only = false;
			break;
		case 'd':
			crom_string_hex = malloc(strlen(optarg)+1);
			if (crom_string_hex == NULL)
				err(EX_SOFTWARE, "%s:crom_string_hex malloc", __func__);
			strcpy(crom_string_hex, optarg);
			display_crom_hex = 1;
			open_needed = true;
			command_set = true;
			display_board_only = false;
			break;
		case 'f':
#define MAX_PHY_CONFIG 0x3f
			set_root_node = strtol(optarg, NULL, 0);
			if ( (set_root_node < 0) || (set_root_node > MAX_PHY_CONFIG) )
				errx(EX_USAGE, "%s:set_root_node out of range", __func__);
			open_needed = true;
			command_set = true;
			display_board_only = false;
			break;
		case 'g':
			set_gap_count = strtol(optarg, NULL, 0);
			if ( (set_gap_count < 0) || (set_gap_count > MAX_PHY_CONFIG) )
				errx(EX_USAGE, "%s:set_gap_count out of range", __func__);
			open_needed = true;
			command_set = true;
			display_board_only = false;
			break;
		case 'l':
			load_crom_from_file = 1;
			load_crom(optarg, crom_buf);
			command_set = true;
			display_board_only = false;
			break;
		case 'm':
			set_fwmem_target = 1;
			open_needed = 0;
			command_set = true;
			display_board_only = false;
			if (eui64_hostton(optarg, &target) != 0 &&
			    eui64_aton(optarg, &target) != 0)
				errx(EX_USAGE, "%s: invalid target: %s", __func__, optarg);
			break;
		case 'o':
			send_link_on = str2node(fd, optarg);
			if ( (send_link_on < 0) || (send_link_on > MAX_PHY_CONFIG) )
				errx(EX_USAGE, "%s: node out of range: %s\n",__func__, optarg);
			open_needed = true;
			command_set = true;
			display_board_only = false;
			break;
		case 'p':
			dump_phy_reg = 1;
			open_needed = true;
			command_set = true;
			display_board_only = false;
			break;
		case 'r':
			send_bus_reset = 1;
			open_needed = true;
			command_set = true;
			display_board_only = false;
			break;
		case 's':
			send_reset_start  = str2node(fd, optarg);
			if ( (send_reset_start < 0) || (send_reset_start > MAX_PHY_CONFIG) )
				errx(EX_USAGE, "%s: node out of range: %s\n", __func__, optarg);
			open_needed = true;
			command_set = true;
			display_board_only = false;
			break;
		case 't':
			dump_topology = 1;
			open_needed = true;
			command_set = true;
			display_board_only = false;
			break;
		case 'u':
			if(!command_set)
				display_board_only = true;
			current_board = strtol(optarg, NULL, 0);
			open_needed = true;
			break;
		case 'M':
			switch (optarg[0]) {
			case 'm':
				recvfn = mpegtsrecv;
				break;
			case 'd':
				recvfn = dvrecv;
				break;
			default:
				errx(EX_USAGE, "unrecognized method: %s",
				    optarg);
			}
			command_set = true;
			display_board_only = false;
			break;
		case 'R':
			recv_data = malloc(strlen(optarg)+1);
			if (recv_data == NULL)
				err(EX_SOFTWARE, "%s:recv_data malloc", __func__);
			strcpy(recv_data, optarg);
			open_needed = false;
			command_set = true;
			display_board_only = false;
			break;
		case 'S':
			send_data = malloc(strlen(optarg)+1);
			if (send_data == NULL)
				err(EX_SOFTWARE, "%s:send_data malloc", __func__);
			strcpy(send_data, optarg);
			open_needed = true;
			command_set = true;
			display_board_only = false;
			break;
		case '?':
		default:
			usage();
		        warnc(EINVAL, "%s: Unknown command line arguments", __func__);
			return 0;
		}
	} /* end while */

       /*
	* Catch the error case when the user
	* executes the command with non ''-''
	* delimited arguments.
	* Generate the usage() display and exit.
	*/
	if (!command_set && !display_board_only) {
		usage();
		warnc(EINVAL, "%s: Unknown command line arguments", __func__);
		return 0;
	}

       /*
	* If -u <bus_number> is passed, execute 
	* command for that card only.
	*
	* If -u <bus_number> is not passed, execute
	* command for card 0 only. 
	*
	*/
	if(open_needed){
		snprintf(devbase, sizeof(devbase), "%s%d.0", device_string, current_board);
		if (open_dev(&fd, devbase) < 0) {
		  err(EX_IOERR, "%s: Error opening firewire controller #%d %s", __func__, current_board, devbase);
		}
	}
	/*
	 * display the nodes on this board "-u"
	 * only
	 */
	if (display_board_only)
		list_dev(fd);

	/*
	 * dump_phy_reg "-p" 
	 */
	if (dump_phy_reg)
		dump_phy_registers(fd);
			
	/*
	 * send a BUS_RESET Event "-r"
	 */
	if (send_bus_reset) {
		if(ioctl(fd, FW_IBUSRST, &tmp) < 0)
               		err(EX_IOERR, "%s: Ioctl of bus reset failed for %s", __func__, devbase);
	}
	/*
	 * Print out the CROM for this node "-c"
	 */
	if (display_crom) {
		tmp = str2node(fd, crom_string);
		get_crom(fd, tmp, crom_buf, len);
		show_crom(crom_buf);
		free(crom_string);
	}
	/*
	 * Hex Dump the CROM for this node "-d"
	 */
	if (display_crom_hex) {
		tmp = str2node(fd, crom_string_hex);
		get_crom(fd, tmp, crom_buf_hex, len);
		dump_crom(crom_buf_hex);
		free(crom_string_hex);
	}
	/*
	 * Set Priority Budget to value for this node "-b"
	 */
	if (priority_budget >= 0)
		set_pri_req(fd, priority_budget);

	/*
	 * Explicitly set the root node of this bus to value "-f"
	 */
	if (set_root_node >= 0)
		send_phy_config(fd, set_root_node, -1);

	/*
	 * Set the gap count for this card/bus  "-g"
	 */
	if (set_gap_count >= 0)
		send_phy_config(fd, -1, set_gap_count);

	/*
	 * Load a CROM from a file "-l"
	 */
	if (load_crom_from_file)
		show_crom(crom_buf);
	/*
	 * Set the fwmem target for a node to argument "-m"
	 */
	if (set_fwmem_target) {
		eui.hi = ntohl(*(u_int32_t*)&(target.octet[0]));
		eui.lo = ntohl(*(u_int32_t*)&(target.octet[4]));
#if defined(__FreeBSD__)
		sysctl_set_int("hw.firewire.fwmem.eui64_hi", eui.hi);
		sysctl_set_int("hw.firewire.fwmem.eui64_lo", eui.lo);
#elif defined(__NetBSD__)
		sysctl_set_int("hw.fwmem.eui64_hi", eui.hi);
		sysctl_set_int("hw.fwmem.eui64_lo", eui.lo);
#else
#warning "You need to add support for your OS"
#endif

	}

	/*
	 * Send a link on to this board/bus "-o"
	 */
	if (send_link_on >= 0)
		link_on(fd, send_link_on);

	/*
	 * Send a reset start to this board/bus "-s"
	 */
	if (send_reset_start >= 0)
		reset_start(fd, send_reset_start);

	/*
	 * Dump the node topology for this board/bus "-t"
	 */
	if (dump_topology)
		show_topology_map(fd);

	/*
	 * Receive data file from node "-R"
	 */
#define TAG	(1<<6)
#define CHANNEL	63
	if (recv_data != NULL){
		if (recvfn == NULL) { /* guess... */
			recvfn = detect_recv_fn(fd, TAG | CHANNEL);
			close(fd);
			fd = -1;
		}
		snprintf(devbase, sizeof(devbase), "%s%d.0", device_string, current_board);
		if (open_dev(&fd, devbase) < 0)
		  err(EX_IOERR, "%s: Error opening firewire controller #%d %s in recv_data\n", __func__, current_board, devbase);
		(*recvfn)(fd, recv_data, TAG | CHANNEL, -1);
		free(recv_data);
	}

	/*
	 * Send data file to node "-S"
	 */
	if (send_data != NULL){
		dvsend(fd, send_data, TAG | CHANNEL, -1);
		free(send_data);
	}

	if (fd > 0) {
		close(fd);
		fd = -1;
	}
	return 0;
}
