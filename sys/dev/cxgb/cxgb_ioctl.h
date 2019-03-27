/**************************************************************************
SPDX-License-Identifier: BSD-2-Clause-FreeBSD

Copyright (c) 2007-2008, Chelsio Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Neither the name of the Chelsio Corporation nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

$FreeBSD$

***************************************************************************/
#ifndef __CHIOCTL_H__
#define __CHIOCTL_H__

/*
 * Ioctl commands specific to this driver.
 */
enum {
	CH_SETREG = 0x40,
	CH_GETREG,
	CH_GETMTUTAB,
	CH_SETMTUTAB,
	CH_SET_PM,
	CH_GET_PM,
	CH_READ_TCAM_WORD,
	CH_GET_MEM,
	CH_GET_SGE_CONTEXT,
	CH_GET_SGE_DESC,
	CH_LOAD_FW,
	CH_SET_TRACE_FILTER,
	CH_GET_QSET_PARAMS,
	CH_GET_QSET_NUM,
	CH_SET_PKTSCHED,
	CH_IFCONF_GETREGS,
	CH_GET_MIIREG,
	CH_SET_MIIREG,
	CH_GET_EEPROM,
	CH_SET_HW_SCHED,
	CH_LOAD_BOOT,
	CH_CLEAR_STATS,
	CH_GET_UP_LA,
	CH_GET_UP_IOQS,
	CH_SET_FILTER,
	CH_DEL_FILTER,
	CH_GET_FILTER,
};

/* statistics categories */
enum {
	STATS_PORT  = 1 << 1,
	STATS_QUEUE = 1 << 2,
};
 
struct ch_reg {
	uint32_t addr;
	uint32_t val;
};

struct ch_cntxt {
	uint32_t cntxt_type;
	uint32_t cntxt_id;
	uint32_t data[4];
};

/* context types */
enum { CNTXT_TYPE_EGRESS, CNTXT_TYPE_FL, CNTXT_TYPE_RSP, CNTXT_TYPE_CQ };

struct ch_desc {
	uint32_t queue_num;
	uint32_t idx;
	uint32_t size;
	uint8_t  data[128];
};

struct ch_mem_range {
	uint32_t mem_id;
	uint32_t addr;
	uint32_t len;
	uint32_t version;
	uint8_t  *buf;
};

enum { MEM_CM, MEM_PMRX, MEM_PMTX };   /* ch_mem_range.mem_id values */

struct ch_qset_params {
	uint32_t qset_idx;
	int32_t  txq_size[3];
	int32_t  rspq_size;
	int32_t  fl_size[2];
	int32_t  intr_lat;
	int32_t  polling;
	int32_t  lro;
	int32_t  cong_thres;
	int32_t  vector;
	int32_t  qnum;
};

struct ch_pktsched_params {
	uint8_t  sched;
	uint8_t  idx;
	uint8_t  min;
	uint8_t  max;
	uint8_t  binding;
};

struct ch_hw_sched {
	uint8_t  sched;
	int8_t   mode;
	int8_t   channel;
	int32_t  kbps;        /* rate in Kbps */
	int32_t  class_ipg;   /* tenths of nanoseconds */
	int32_t  flow_ipg;    /* usec */
};

struct ch_mtus {
	uint32_t nmtus;
	uint16_t mtus[NMTUS];
};

struct ch_pm {
	uint32_t tx_pg_sz;
	uint32_t tx_num_pg;
	uint32_t rx_pg_sz;
	uint32_t rx_num_pg;
	uint32_t pm_total;
};

struct ch_tcam_word {
	uint32_t addr;
	uint32_t buf[3];
};

struct ch_trace {
	uint32_t sip;
	uint32_t sip_mask;
	uint32_t dip;
	uint32_t dip_mask;
	uint16_t sport;
	uint16_t sport_mask;
	uint16_t dport;
	uint16_t dport_mask;
	uint32_t vlan:12;
	uint32_t vlan_mask:12;
	uint32_t intf:4;
	uint32_t intf_mask:4;
	uint8_t  proto;
	uint8_t  proto_mask;
	uint8_t  invert_match:1;
	uint8_t  config_tx:1;
	uint8_t  config_rx:1;
	uint8_t  trace_tx:1;
	uint8_t  trace_rx:1;
};

#define REGDUMP_SIZE  (4 * 1024)

struct ch_ifconf_regs {
	uint32_t  version;
	uint32_t  len; /* bytes */
	uint8_t   *data;
};

struct ch_mii_data {
	uint32_t phy_id;
	uint32_t reg_num;
	uint32_t val_in;
	uint32_t val_out;
};

struct ch_eeprom {
	uint32_t magic;
	uint32_t offset;
	uint32_t len;
	uint8_t  *data;
};

#define LA_BUFSIZE	(2 * 1024)
struct ch_up_la {
	uint32_t stopped;
	uint32_t idx;
	uint32_t bufsize;
	uint32_t *data;
};

struct t3_ioq_entry {
	uint32_t ioq_cp;
	uint32_t ioq_pp;
	uint32_t ioq_alen;
	uint32_t ioq_stats;
};

#define IOQS_BUFSIZE	(1024)
struct ch_up_ioqs {
	uint32_t ioq_rx_enable;
	uint32_t ioq_tx_enable;
	uint32_t ioq_rx_status;
	uint32_t ioq_tx_status;
	uint32_t bufsize;
	struct t3_ioq_entry *data;
};

struct ch_filter_tuple {
	uint32_t sip;
	uint32_t dip;
	uint16_t sport;
	uint16_t dport;
	uint16_t vlan:12;
	uint16_t vlan_prio:3;
};

struct ch_filter {
	uint32_t filter_id;
	struct ch_filter_tuple val;
	struct ch_filter_tuple mask;
	uint16_t mac_addr_idx;
	uint8_t mac_hit:1;
	uint8_t proto:2;

	uint8_t want_filter_id:1;
	uint8_t pass:1;
	uint8_t rss:1;
	uint8_t qset;
};

#define CHELSIO_SETREG		_IOW('f', CH_SETREG, struct ch_reg)
#define CHELSIO_GETREG		_IOWR('f', CH_GETREG, struct ch_reg)
#define CHELSIO_GETMTUTAB	_IOR('f', CH_GETMTUTAB, struct ch_mtus)
#define CHELSIO_SETMTUTAB	_IOW('f', CH_SETMTUTAB, struct ch_mtus)
#define CHELSIO_SET_PM		_IOW('f', CH_SET_PM, struct ch_pm)
#define CHELSIO_GET_PM		_IOR('f', CH_GET_PM, struct ch_pm)
#define CHELSIO_READ_TCAM_WORD	_IOWR('f', CH_READ_TCAM_WORD, struct ch_tcam_word)
#define CHELSIO_GET_MEM		_IOWR('f', CH_GET_MEM, struct ch_mem_range)
#define CHELSIO_GET_SGE_CONTEXT	_IOWR('f', CH_GET_SGE_CONTEXT, struct ch_cntxt)
#define CHELSIO_GET_SGE_DESC	_IOWR('f', CH_GET_SGE_DESC, struct ch_desc)
#define CHELSIO_LOAD_FW		_IOWR('f', CH_LOAD_FW, struct ch_mem_range)
#define CHELSIO_SET_TRACE_FILTER _IOW('f', CH_SET_TRACE_FILTER, struct ch_trace)
#define CHELSIO_GET_QSET_PARAMS	_IOWR('f', CH_GET_QSET_PARAMS, struct ch_qset_params)
#define CHELSIO_GET_QSET_NUM	_IOR('f', CH_GET_QSET_NUM, struct ch_reg)
#define CHELSIO_SET_PKTSCHED	_IOW('f', CH_SET_PKTSCHED, struct ch_pktsched_params)
#define CHELSIO_SET_HW_SCHED	_IOW('f', CH_SET_HW_SCHED, struct ch_hw_sched)
#define CHELSIO_LOAD_BOOT	_IOW('f', CH_LOAD_BOOT, struct ch_mem_range)
#define CHELSIO_CLEAR_STATS	_IO('f', CH_CLEAR_STATS)
#define CHELSIO_IFCONF_GETREGS	_IOWR('f', CH_IFCONF_GETREGS, struct ch_ifconf_regs)
#define CHELSIO_GET_MIIREG	_IOWR('f', CH_GET_MIIREG, struct ch_mii_data)
#define CHELSIO_SET_MIIREG	_IOW('f', CH_SET_MIIREG, struct ch_mii_data)
#define CHELSIO_GET_EEPROM	_IOWR('f', CH_GET_EEPROM, struct ch_eeprom)
#define CHELSIO_GET_UP_LA	_IOWR('f', CH_GET_UP_LA, struct ch_up_la)
#define CHELSIO_GET_UP_IOQS	_IOWR('f', CH_GET_UP_IOQS, struct ch_up_ioqs)
#define CHELSIO_SET_FILTER	_IOW('f', CH_SET_FILTER, struct ch_filter)
#define CHELSIO_DEL_FILTER	_IOW('f', CH_DEL_FILTER, struct ch_filter)
#define CHELSIO_GET_FILTER	_IOWR('f', CH_GET_FILTER, struct ch_filter)
#endif
