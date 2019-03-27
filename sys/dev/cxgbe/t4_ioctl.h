/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: Navdeep Parhar <np@FreeBSD.org>
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __T4_IOCTL_H__
#define __T4_IOCTL_H__

#include <sys/types.h>
#include <net/ethernet.h>
#include <net/bpf.h>

/*
 * Ioctl commands specific to this driver.
 */
enum {
	T4_GETREG = 0x40,		/* read register */
	T4_SETREG,			/* write register */
	T4_REGDUMP,			/* dump of all registers */
	T4_GET_FILTER_MODE,		/* get global filter mode */
	T4_SET_FILTER_MODE,		/* set global filter mode */
	T4_GET_FILTER,			/* get information about a filter */
	T4_SET_FILTER,			/* program a filter */
	T4_DEL_FILTER,			/* delete a filter */
	T4_GET_SGE_CONTEXT,		/* get SGE context for a queue */
	T4_LOAD_FW,			/* flash firmware */
	T4_GET_MEM,			/* read memory */
	T4_GET_I2C,			/* read from i2c addressible device */
	T4_CLEAR_STATS,			/* clear a port's MAC statistics */
	T4_SET_OFLD_POLICY,		/* Set offload policy */
	T4_SET_SCHED_CLASS,             /* set sched class */
	T4_SET_SCHED_QUEUE,             /* set queue class */
	T4_GET_TRACER,			/* get information about a tracer */
	T4_SET_TRACER,			/* program a tracer */
	T4_LOAD_CFG,			/* copy a config file to card's flash */
	T4_LOAD_BOOT,			/* flash boot rom */
	T4_LOAD_BOOTCFG,		/* flash bootcfg */
	T4_CUDBG_DUMP,			/* debug dump of chip state */
};

struct t4_reg {
	uint32_t addr;
	uint32_t size;
	uint64_t val;
};

#define T4_REGDUMP_SIZE  (160 * 1024)
#define T5_REGDUMP_SIZE  (332 * 1024)
struct t4_regdump {
	uint32_t version;
	uint32_t len; /* bytes */
	uint32_t *data;
};

struct t4_data {
	uint32_t len;
	uint8_t *data;
};

struct t4_bootrom {
	uint32_t pf_offset;
	uint32_t pfidx_addr;
	uint32_t len;
	uint8_t *data;
};

struct t4_i2c_data {
	uint8_t port_id;
	uint8_t dev_addr;
	uint8_t offset;
	uint8_t len;
	uint8_t data[8];
};

/*
 * A hardware filter is some valid combination of these.
 */
#define T4_FILTER_IPv4		0x1	/* IPv4 packet */
#define T4_FILTER_IPv6		0x2	/* IPv6 packet */
#define T4_FILTER_IP_SADDR	0x4	/* Source IP address or network */
#define T4_FILTER_IP_DADDR	0x8	/* Destination IP address or network */
#define T4_FILTER_IP_SPORT	0x10	/* Source IP port */
#define T4_FILTER_IP_DPORT	0x20	/* Destination IP port */
#define T4_FILTER_FCoE		0x40	/* Fibre Channel over Ethernet packet */
#define T4_FILTER_PORT		0x80	/* Physical ingress port */
#define T4_FILTER_VNIC		0x100	/* VNIC id or outer VLAN */
#define T4_FILTER_VLAN		0x200	/* VLAN ID */
#define T4_FILTER_IP_TOS	0x400	/* IPv4 TOS/IPv6 Traffic Class */
#define T4_FILTER_IP_PROTO	0x800	/* IP protocol */
#define T4_FILTER_ETH_TYPE	0x1000	/* Ethernet Type */
#define T4_FILTER_MAC_IDX	0x2000	/* MPS MAC address match index */
#define T4_FILTER_MPS_HIT_TYPE	0x4000	/* MPS match type */
#define T4_FILTER_IP_FRAGMENT	0x8000	/* IP fragment */

#define T4_FILTER_IC_VNIC	0x80000000	/* TP Ingress Config's F_VNIC
						   bit.  It indicates whether
						   T4_FILTER_VNIC bit means VNIC
						   id (PF/VF) or outer VLAN.
						   0 = oVLAN, 1 = VNIC */

/* Filter action */
enum {
	FILTER_PASS = 0,	/* default */
	FILTER_DROP,
	FILTER_SWITCH
};

/* 802.1q manipulation on FILTER_SWITCH */
enum {
	VLAN_NOCHANGE = 0,	/* default */
	VLAN_REMOVE,
	VLAN_INSERT,
	VLAN_REWRITE
};

/* MPS match type */
enum {
	UCAST_EXACT = 0,       /* exact unicast match */
	UCAST_HASH  = 1,       /* inexact (hashed) unicast match */
	MCAST_EXACT = 2,       /* exact multicast match */
	MCAST_HASH  = 3,       /* inexact (hashed) multicast match */
	PROMISC     = 4,       /* no match but port is promiscuous */
	HYPPROMISC  = 5,       /* port is hypervisor-promisuous + not bcast */
	BCAST       = 6,       /* broadcast packet */
};

/* Rx steering */
enum {
	DST_MODE_QUEUE,        /* queue is directly specified by filter */
	DST_MODE_RSS_QUEUE,    /* filter specifies RSS entry containing queue */
	DST_MODE_RSS,          /* queue selected by default RSS hash lookup */
	DST_MODE_FILT_RSS      /* queue selected by hashing in filter-specified
				  RSS subtable */
};

enum {
	NAT_MODE_NONE = 0,	/* No NAT performed */
	NAT_MODE_DIP,		/* NAT on Dst IP */
	NAT_MODE_DIP_DP,	/* NAT on Dst IP, Dst Port */
	NAT_MODE_DIP_DP_SIP,	/* NAT on Dst IP, Dst Port and Src IP */
	NAT_MODE_DIP_DP_SP,	/* NAT on Dst IP, Dst Port and Src Port */
	NAT_MODE_SIP_SP,	/* NAT on Src IP and Src Port */
	NAT_MODE_DIP_SIP_SP,	/* NAT on Dst IP, Src IP and Src Port */
	NAT_MODE_ALL		/* NAT on entire 4-tuple */
};

struct t4_filter_tuple {
	/*
	 * These are always available.
	 */
	uint8_t sip[16];	/* source IP address (IPv4 in [3:0]) */
	uint8_t dip[16];	/* destination IP address (IPv4 in [3:0]) */
	uint16_t sport;		/* source port */
	uint16_t dport;		/* destination port */

	/*
	 * A combination of these (up to 36 bits) is available.  TP_VLAN_PRI_MAP
	 * is used to select the global mode and all filters are limited to the
	 * set of fields allowed by the global mode.
	 */
	uint16_t vnic;		/* VNIC id (PF/VF) or outer VLAN tag */
	uint16_t vlan;		/* VLAN tag */
	uint16_t ethtype;	/* Ethernet type */
	uint8_t  tos;		/* TOS/Traffic Type */
	uint8_t  proto;		/* protocol type */
	uint32_t fcoe:1;	/* FCoE packet */
	uint32_t iport:3;	/* ingress port */
	uint32_t matchtype:3;	/* MPS match type */
	uint32_t frag:1;	/* fragmentation extension header */
	uint32_t macidx:9;	/* exact match MAC index */
	uint32_t vlan_vld:1;	/* VLAN valid */
	uint32_t ovlan_vld:1;	/* outer VLAN tag valid, value in "vnic" */
	uint32_t pfvf_vld:1;	/* VNIC id (PF/VF) valid, value in "vnic" */
};

struct t4_filter_specification {
	uint32_t hitcnts:1;	/* count filter hits in TCB */
	uint32_t prio:1;	/* filter has priority over active/server */
	uint32_t type:1;	/* 0 => IPv4, 1 => IPv6 */
	uint32_t hash:1;	/* 0 => LE TCAM, 1 => Hash */
	uint32_t action:2;	/* drop, pass, switch */
	uint32_t rpttid:1;	/* report TID in RSS hash field */
	uint32_t dirsteer:1;	/* 0 => RSS, 1 => steer to iq */
	uint32_t iq:10;		/* ingress queue */
	uint32_t maskhash:1;	/* dirsteer=0: steer to an RSS sub-region */
	uint32_t dirsteerhash:1;/* dirsteer=1: 0 => TCB contains RSS hash */
				/*             1 => TCB contains IQ ID */

	/*
	 * Switch proxy/rewrite fields.  An ingress packet which matches a
	 * filter with "switch" set will be looped back out as an egress
	 * packet -- potentially with some Ethernet header rewriting.
	 */
	uint32_t eport:2;	/* egress port to switch packet out */
	uint32_t newdmac:1;	/* rewrite destination MAC address */
	uint32_t newsmac:1;	/* rewrite source MAC address */
	uint32_t swapmac:1;	/* swap SMAC/DMAC for loopback packet */
	uint32_t newvlan:2;	/* rewrite VLAN Tag */
	uint32_t nat_mode:3;	/* NAT operation mode */
	uint32_t nat_flag_chk:1;/* check TCP flags before NAT'ing */
	uint32_t nat_seq_chk;	/* sequence value to use for NAT check*/
	uint8_t dmac[ETHER_ADDR_LEN];	/* new destination MAC address */
	uint8_t smac[ETHER_ADDR_LEN];	/* new source MAC address */
	uint16_t vlan;		/* VLAN Tag to insert */

	uint8_t nat_dip[16];	/* destination IP to use after NAT'ing */
	uint8_t nat_sip[16];	/* source IP to use after NAT'ing */
	uint16_t nat_dport;	/* destination port to use after NAT'ing */
	uint16_t nat_sport;	/* source port to use after NAT'ing */

	/*
	 * Filter rule value/mask pairs.
	 */
	struct t4_filter_tuple val;
	struct t4_filter_tuple mask;
};

struct t4_filter {
	uint32_t idx;
	uint16_t l2tidx;
	uint16_t smtidx;
	uint64_t hits;
	struct t4_filter_specification fs;
};

/* Tx Scheduling Class parameters */
struct t4_sched_class_params {
	int8_t   level;		/* scheduler hierarchy level */
	int8_t   mode;		/* per-class or per-flow */
	int8_t   rateunit;	/* bit or packet rate */
	int8_t   ratemode;	/* %port relative or kbps absolute */
	int8_t   channel;	/* scheduler channel [0..N] */
	int8_t   cl;		/* scheduler class [0..N] */
	int32_t  minrate;	/* minimum rate */
	int32_t  maxrate;	/* maximum rate */
	int16_t  weight;	/* percent weight */
	int16_t  pktsize;	/* average packet size */
};

/*
 * Support for "sched-class" command to allow a TX Scheduling Class to be
 * programmed with various parameters.
 */
struct t4_sched_params {
	int8_t   subcmd;		/* sub-command */
	int8_t   type;			/* packet or flow */
	union {
		struct {		/* sub-command SCHED_CLASS_CONFIG */
			int8_t   minmax;	/* minmax enable */
		} config;
		struct t4_sched_class_params params;
		uint8_t     reserved[6 + 8 * 8];
	} u;
};

enum {
	SCHED_CLASS_SUBCMD_CONFIG,	/* config sub-command */
	SCHED_CLASS_SUBCMD_PARAMS,	/* params sub-command */
};

enum {
	SCHED_CLASS_TYPE_PACKET,
};

enum {
	SCHED_CLASS_LEVEL_CL_RL,	/* class rate limiter */
	SCHED_CLASS_LEVEL_CL_WRR,	/* class weighted round robin */
	SCHED_CLASS_LEVEL_CH_RL,	/* channel rate limiter */
};

enum {
	SCHED_CLASS_MODE_CLASS,		/* per-class scheduling */
	SCHED_CLASS_MODE_FLOW,		/* per-flow scheduling */
};

enum {
	SCHED_CLASS_RATEUNIT_BITS,	/* bit rate scheduling */
	SCHED_CLASS_RATEUNIT_PKTS,	/* packet rate scheduling */
};

enum {
	SCHED_CLASS_RATEMODE_REL,	/* percent of port bandwidth */
	SCHED_CLASS_RATEMODE_ABS,	/* Kb/s */
};

/*
 * Support for "sched_queue" command to allow one or more NIC TX Queues to be
 * bound to a TX Scheduling Class.
 */
struct t4_sched_queue {
	uint8_t  port;
	int8_t   queue;	/* queue index; -1 => all queues */
	int8_t   cl;	/* class index; -1 => unbind */
};

#define T4_SGE_CONTEXT_SIZE 24
enum {
	SGE_CONTEXT_EGRESS,
	SGE_CONTEXT_INGRESS,
	SGE_CONTEXT_FLM,
	SGE_CONTEXT_CNM
};

struct t4_sge_context {
	uint32_t mem_id;
	uint32_t cid;
	uint32_t data[T4_SGE_CONTEXT_SIZE / 4];
};

struct t4_mem_range {
	uint32_t addr;
	uint32_t len;
	uint32_t *data;
};

#define T4_TRACE_LEN 112
struct t4_trace_params {
	uint32_t data[T4_TRACE_LEN / 4];
	uint32_t mask[T4_TRACE_LEN / 4];
	uint16_t snap_len;
	uint16_t min_len;
	uint8_t skip_ofst;
	uint8_t skip_len;
	uint8_t invert;
	uint8_t port;
};

struct t4_tracer {
	uint8_t idx;
	uint8_t enabled;
	uint8_t valid;
	struct t4_trace_params tp;
};

struct t4_cudbg_dump {
	uint8_t wr_flash;
	uint8_t	bitmap[16];
	uint32_t len;
	uint8_t *data;
};

enum {
	OPEN_TYPE_LISTEN = 'L',
	OPEN_TYPE_ACTIVE = 'A',
	OPEN_TYPE_PASSIVE = 'P',
	OPEN_TYPE_DONTCARE = 'D',
};

struct offload_settings {
	int8_t offload;
	int8_t rx_coalesce;
	int8_t cong_algo;
	int8_t sched_class;
	int8_t tstamp;
	int8_t sack;
	int8_t nagle;
	int8_t ecn;
	int8_t ddp;
	int8_t tls;
	int16_t txq;
	int16_t rxq;
	int16_t mss;
};

struct offload_rule {
	char open_type;
	struct offload_settings settings;
	struct bpf_program bpf_prog;	/* compiled program/filter */
};

/*
 * An offload policy consists of a set of rules matched in sequence.  The
 * settings of the first rule that matches are applied to that connection.
 */
struct t4_offload_policy {
	uint32_t nrules;
	struct offload_rule *rule;
};

#define CHELSIO_T4_GETREG	_IOWR('f', T4_GETREG, struct t4_reg)
#define CHELSIO_T4_SETREG	_IOW('f', T4_SETREG, struct t4_reg)
#define CHELSIO_T4_REGDUMP	_IOWR('f', T4_REGDUMP, struct t4_regdump)
#define CHELSIO_T4_GET_FILTER_MODE _IOWR('f', T4_GET_FILTER_MODE, uint32_t)
#define CHELSIO_T4_SET_FILTER_MODE _IOW('f', T4_SET_FILTER_MODE, uint32_t)
#define CHELSIO_T4_GET_FILTER	_IOWR('f', T4_GET_FILTER, struct t4_filter)
#define CHELSIO_T4_SET_FILTER	_IOWR('f', T4_SET_FILTER, struct t4_filter)
#define CHELSIO_T4_DEL_FILTER	_IOW('f', T4_DEL_FILTER, struct t4_filter)
#define CHELSIO_T4_GET_SGE_CONTEXT _IOWR('f', T4_GET_SGE_CONTEXT, \
    struct t4_sge_context)
#define CHELSIO_T4_LOAD_FW	_IOW('f', T4_LOAD_FW, struct t4_data)
#define CHELSIO_T4_GET_MEM	_IOW('f', T4_GET_MEM, struct t4_mem_range)
#define CHELSIO_T4_GET_I2C	_IOWR('f', T4_GET_I2C, struct t4_i2c_data)
#define CHELSIO_T4_CLEAR_STATS	_IOW('f', T4_CLEAR_STATS, uint32_t)
#define CHELSIO_T4_SCHED_CLASS  _IOW('f', T4_SET_SCHED_CLASS, \
    struct t4_sched_params)
#define CHELSIO_T4_SCHED_QUEUE  _IOW('f', T4_SET_SCHED_QUEUE, \
    struct t4_sched_queue)
#define CHELSIO_T4_GET_TRACER	_IOWR('f', T4_GET_TRACER, struct t4_tracer)
#define CHELSIO_T4_SET_TRACER	_IOW('f', T4_SET_TRACER, struct t4_tracer)
#define CHELSIO_T4_LOAD_CFG	_IOW('f', T4_LOAD_CFG, struct t4_data)
#define CHELSIO_T4_LOAD_BOOT	_IOW('f', T4_LOAD_BOOT, struct t4_bootrom)
#define CHELSIO_T4_LOAD_BOOTCFG	_IOW('f', T4_LOAD_BOOTCFG, struct t4_data)
#define CHELSIO_T4_CUDBG_DUMP	_IOWR('f', T4_CUDBG_DUMP, struct t4_cudbg_dump)
#define CHELSIO_T4_SET_OFLD_POLICY _IOW('f', T4_SET_OFLD_POLICY, struct t4_offload_policy)
#endif
