/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Shteryana Shopova <syrinx@FreeBSD.org>
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Bridge MIB implementation for SNMPd.
 *
 * $FreeBSD$
 */

#ifndef	SNMP_BRIDGE_H
#define	SNMP_BRIDGE_H

#define	SNMP_BRIDGE_ID_LEN	8

typedef uint8_t	port_id[2];
typedef u_char	bridge_id[SNMP_BRIDGE_ID_LEN];

#define	SNMP_BRIDGE_MAX_PRIORITY	65535

#define	SNMP_BRIDGE_MIN_AGE_TIME	10
#define	SNMP_BRIDGE_MAX_AGE_TIME	1000000

#define	SNMP_BRIDGE_MIN_TXHC		1
#define	SNMP_BRIDGE_MAX_TXHC		10

#define	SNMP_BRIDGE_MIN_MAGE		600
#define	SNMP_BRIDGE_MAX_MAGE		4000

#define	SNMP_BRIDGE_MIN_HTIME		100
#define	SNMP_BRIDGE_MAX_HTIME		1000

#define	SNMP_BRIDGE_MIN_FDELAY		400
#define	SNMP_BRIDGE_MAX_FDELAY		3000

#define	SNMP_PORT_PATHCOST_OBSOLETE	65535
#define	SNMP_PORT_MIN_PATHCOST		0
#define	SNMP_PORT_MAX_PATHCOST		200000000
#define	SNMP_PORT_PATHCOST_AUTO		0

#define	SNMP_BRIDGE_DATA_MAXAGE		10
#define	SNMP_BRIDGE_DATA_MAXAGE_MIN	1
#define	SNMP_BRIDGE_DATA_MAXAGE_MAX	300

/* By default poll kernel data every 5 minutes. */
#define	SNMP_BRIDGE_POLL_INTERVAL	(5 * 60)
#define	SNMP_BRIDGE_POLL_INTERVAL_MIN	1
#define	SNMP_BRIDGE_POLL_INTERVAL_MAX	3600

/* Poll for a topology change once every 30 seconds. */
#define	SNMP_BRIDGE_TC_POLL_INTERVAL	30

struct bridge_if *bridge_get_default(void);

void bridge_set_default(struct bridge_if *bif);

const char *bridge_get_default_name(void);

int bridge_get_data_maxage(void);

/*
 * Bridge Addresses Table.
 */
struct tp_entry {
	uint32_t		sysindex;	/* The bridge if sysindex. */
	int32_t			port_no;
	enum TpFdbStatus	status;
	uint8_t			tp_addr[ETHER_ADDR_LEN];
	uint8_t			flags;
	TAILQ_ENTRY(tp_entry)	tp_e;
};

/*
 * Bridge ports.
 * The bridge port system interface index is used for a
 * port number. Transparent bridging statistics and STP
 * information for a port are also contained here.
 */
struct bridge_port {
	/* dot1dBase subtree objects. */
	uint32_t	sysindex;	/* The bridge interface sysindex. */
	int32_t		port_no;	/* The bridge member system index. */
	int32_t		if_idx;		/* SNMP ifIndex from mibII. */
	int8_t		span_enable;	/* Span flag set - private MIB. */
	struct asn_oid	circuit;	/* Unused. */
	uint32_t	dly_ex_drops;	/* Drops on output. */
	uint32_t	dly_mtu_drops;	/* MTU exceeded drops. */
	int32_t		status;		/* The entry status. */
	enum TruthValue	priv_set;	/* The private flag. */

	/* dot1dStp subtree objects. */
	int32_t		path_cost;
	int32_t		priority;
	int32_t		design_cost;
	uint32_t	fwd_trans;
	char		p_name[IFNAMSIZ]; /* Not in BRIDGE-MIB. */
	enum StpPortState	state;
	enum dot1dStpPortEnable	enable;
	port_id		design_port;
	bridge_id	design_root;
	bridge_id	design_bridge;

	/* rstpMib extensions. */
	int32_t		admin_path_cost;
	enum TruthValue	proto_migr;
	enum TruthValue	admin_edge;
	enum TruthValue	oper_edge;
	enum TruthValue	oper_ptp;
	enum StpPortAdminPointToPointType	admin_ptp;

	/* dot1dTp subtree objects. */
	int32_t		max_info;
	int32_t		in_frames;
	int32_t		out_frames;
	int32_t		in_drops;

	uint8_t		flags;
	TAILQ_ENTRY(bridge_port) b_p;
};

/*
 * A bridge interface.
 * The system interface index of the bridge is not required neither by the
 * standard BRIDGE-MIB nor by the private BEGEMOT-BRIDGE-MIB, but is used
 * as key for looking up the other info for this bridge.
 */
struct bridge_if {
	/* dot1dBase subtree objects. */
	uint32_t	sysindex;	/* The system interface index. */
	int32_t		num_ports;	/* Number of ports. */
	enum BaseType	br_type;	/* Bridge type. */
	enum RowStatus	if_status;	/* Bridge status. */
	char		bif_name[IFNAMSIZ]; /* Bridge interface name. */
	struct ether_addr br_addr;	/* Bridge address. */
	struct bridge_port *f_bp;	/* This bridge's first entry
					 * in the base ports TAILQ. */
	/* dot1dStp subtree objects. */
	int32_t		priority;
	int32_t		root_cost;
	int32_t		root_port;
	int32_t		max_age;	/* Current max age. */
	int32_t		hello_time;	/* Current hello time. */
	int32_t		fwd_delay;	/* Current forward delay. */
	int32_t		hold_time;
	int32_t		bridge_max_age;	/* Configured max age. */
	int32_t		bridge_hello_time; /* Configured hello time. */
	int32_t		bridge_fwd_delay; /* Configured forward delay. */
	int32_t		tx_hold_count;
	uint32_t	top_changes;
	enum dot1dStpVersion	stp_version;
	enum dot1dStpProtocolSpecification prot_spec;
	struct timeval	last_tc_time;
	bridge_id	design_root;

	/* dot1dTp subtree objects. */
	int32_t		lrnt_drops;	/* Dropped addresses. */
	int32_t		age_time;	/* Address entry timeout. */
	int32_t		num_addrs;	/* Current # of addresses in cache. */
	int32_t		max_addrs;	/* Max # of addresses in cache. */
	struct tp_entry	 *f_tpa;	/* This bridge's first entry in
					 * the tp addresses TAILQ. */

	time_t		entry_age;
	time_t		ports_age;
	time_t		addrs_age;
	TAILQ_ENTRY(bridge_if) b_if;
};

void bridge_ifs_fini(void);

struct bridge_if *bridge_if_find_ifs(uint32_t sysindex);

struct bridge_if *bridge_if_find_ifname(const char *b_name);

const char *bridge_if_find_name(uint32_t sysindex);

int bridge_compare_sysidx(uint32_t i1, uint32_t i2);

int bridge_attach_newif(struct mibif *ifp);

struct bridge_if *bridge_first_bif(void);

struct bridge_if *bridge_next_bif(struct bridge_if *b_pr);

void bridge_remove_bif(struct bridge_if *bif);

void bridge_update_all_ports(void);

void bridge_update_all_addrs(void);

void bridge_update_all_ifs(void);

void bridge_update_all(void *arg);

void bridge_update_tc_time(void *arg);

void bridge_ifs_dump(void);

/* Bridge ports. */
void bridge_ports_update_listage(void);

void bridge_ports_fini(void);

void bridge_members_free(struct bridge_if *bif);

struct bridge_port *bridge_new_port(struct mibif *mif, struct bridge_if *bif);

void bridge_port_remove(struct bridge_port *bp, struct bridge_if *bif);

struct bridge_port *bridge_port_bif_first(struct bridge_if *bif);

struct bridge_port *bridge_port_bif_next(struct bridge_port *bp);

struct bridge_port *bridge_port_find(int32_t if_idx, struct bridge_if *bif);

void bridge_port_getinfo_mibif(struct mibif *m_if, struct bridge_port *bp);

int bridge_getinfo_bif_ports(struct bridge_if *bif);

int bridge_update_memif(struct bridge_if *bif);

void bridge_ports_dump(struct bridge_if *bif);

/* Bridge addresses. */
void bridge_addrs_update_listage(void);

void bridge_addrs_fini(void);

void bridge_addrs_free(struct bridge_if *bif);

struct tp_entry *bridge_new_addrs(uint8_t *mac, struct bridge_if *bif);

void bridge_addrs_remove(struct tp_entry *te, struct bridge_if *bif);

struct tp_entry *bridge_addrs_find(uint8_t *mac, struct bridge_if *bif);

struct tp_entry *bridge_addrs_bif_first(struct bridge_if *bif);

struct tp_entry *bridge_addrs_bif_next(struct tp_entry *te);

int bridge_getinfo_bif_addrs(struct bridge_if *bif);

int bridge_update_addrs(struct bridge_if *bif);

void bridge_addrs_dump(struct bridge_if *bif);

/* Bridge PF. */

void bridge_pf_dump(void);

/* System specific. */

/* Open the socket for the ioctls. */
int bridge_ioctl_init(void);

/* Load bridge kernel module. */
int bridge_kmod_load(void);

/* Get the bridge interface information. */
int bridge_getinfo_bif(struct bridge_if *bif);

/* Get the bridge interface STP parameters. */
int bridge_get_op_param(struct bridge_if *bif);

/* Set the bridge priority. */
int bridge_set_priority(struct bridge_if *bif, int32_t priority);

/* Set the bridge max age. */
int bridge_set_maxage(struct bridge_if *bif, int32_t max_age);

/* Set the bridge hello time.*/
int bridge_set_hello_time(struct bridge_if *bif, int32_t hello_time);

/* Set the bridge forward delay.*/
int bridge_set_forward_delay(struct bridge_if *bif, int32_t fwd_delay);

/* Set the bridge address cache max age. */
int bridge_set_aging_time(struct bridge_if *bif, int32_t age_time);

/* Set the max number of entries in the bridge address cache. */
int bridge_set_max_cache(struct bridge_if *bif, int32_t max_cache);

/* Set the bridge TX hold count. */
int bridge_set_tx_hold_count(struct bridge_if *bif, int32_t tx_hc);

/* Set the bridge STP protocol version. */
int bridge_set_stp_version(struct bridge_if *bif, int32_t stp_proto);

/* Set the bridge interface status to up/down. */
int bridge_set_if_up(const char* b_name, int8_t up);

/* Create a bridge interface. */
int bridge_create(const char *b_name);

/* Destroy a bridge interface. */
int bridge_destroy(const char *b_name);

/* Fetch the bridge mac address. */
u_char *bridge_get_basemac(const char *bif_name, u_char *mac, size_t mlen);

/* Set a bridge member priority. */
int bridge_port_set_priority(const char *bif_name, struct bridge_port *bp,
    int32_t priority);

/* Set a bridge member STP-enabled flag. */
int bridge_port_set_stp_enable(const char *bif_name, struct bridge_port *bp,
    uint32_t enable);

/* Set a bridge member STP path cost. */
int bridge_port_set_path_cost(const char *bif_name, struct bridge_port *bp,
    int32_t path_cost);

/* Set admin point-to-point link. */
int bridge_port_set_admin_ptp(const char *bif_name, struct bridge_port *bp,
    uint32_t admin_ptp);

/* Set admin edge. */
int bridge_port_set_admin_edge(const char *bif_name, struct bridge_port *bp,
    uint32_t enable);

/* Set 'private' flag. */
int bridge_port_set_private(const char *bif_name, struct bridge_port *bp,
    uint32_t priv_set);

/* Add a bridge member port. */
int bridge_port_addm(struct bridge_port *bp, const char *b_name);

/* Delete a bridge member port. */
int bridge_port_delm(struct bridge_port *bp, const char *b_name);

/* Get the current value from the module for bridge PF control. */
int32_t bridge_get_pfval(uint8_t which);

/* Get/Set a bridge PF control. */
int32_t bridge_do_pfctl(int32_t bridge_ctl, enum snmp_op op, int32_t *val);

#endif /* SNMP_BRIDGE_H */
