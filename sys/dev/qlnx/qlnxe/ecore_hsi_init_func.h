/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __ECORE_HSI_INIT_FUNC__
#define __ECORE_HSI_INIT_FUNC__ 
/********************************/
/* HSI Init Functions constants */
/********************************/

/* Number of VLAN priorities */
#define NUM_OF_VLAN_PRIORITIES			8

/* Size of CRC8 lookup table */
#ifndef LINUX_REMOVE
#define CRC8_TABLE_SIZE					256
#endif

/*
 * BRB RAM init requirements
 */
struct init_brb_ram_req
{
	u32 guranteed_per_tc /* guaranteed size per TC, in bytes */;
	u32 headroom_per_tc /* headroom size per TC, in bytes */;
	u32 min_pkt_size /* min packet size, in bytes */;
	u32 max_ports_per_engine /* min packet size, in bytes */;
	u8 num_active_tcs[MAX_NUM_PORTS] /* number of active TCs per port */;
};


/*
 * ETS per-TC init requirements
 */
struct init_ets_tc_req
{
	u8 use_sp /* if set, this TC participates in the arbitration with a strict priority (the priority is equal to the TC ID) */;
	u8 use_wfq /* if set, this TC participates in the arbitration with a WFQ weight (indicated by the weight field) */;
	u16 weight /* An arbitration weight. Valid only if use_wfq is set. */;
};

/*
 * ETS init requirements
 */
struct init_ets_req
{
	u32 mtu /* Max packet size (in bytes) */;
	struct init_ets_tc_req tc_req[NUM_OF_TCS] /* ETS initialization requirements per TC. */;
};



/*
 * NIG LB RL init requirements
 */
struct init_nig_lb_rl_req
{
	u16 lb_mac_rate /* Global MAC+LB RL rate (in Mbps). If set to 0, the RL will be disabled. */;
	u16 lb_rate /* Global LB RL rate (in Mbps). If set to 0, the RL will be disabled. */;
	u32 mtu /* Max packet size (in bytes) */;
	u16 tc_rate[NUM_OF_PHYS_TCS] /* RL rate per physical TC (in Mbps). If set to 0, the RL will be disabled. */;
};


/*
 * NIG TC mapping for each priority
 */
struct init_nig_pri_tc_map_entry
{
	u8 tc_id /* the mapped TC ID */;
	u8 valid /* indicates if the mapping entry is valid */;
};


/*
 * NIG priority to TC map init requirements
 */
struct init_nig_pri_tc_map_req
{
	struct init_nig_pri_tc_map_entry pri[NUM_OF_VLAN_PRIORITIES];
};


/*
 * QM per-port init parameters
 */
struct init_qm_port_params
{
	u8 active /* Indicates if this port is active */;
	u8 active_phys_tcs /* Vector of valid bits for active TCs used by this port */;
	u16 num_pbf_cmd_lines /* number of PBF command lines that can be used by this port */;
	u16 num_btb_blocks /* number of BTB blocks that can be used by this port */;
	u16 reserved;
};


/*
 * QM per-PQ init parameters
 */
struct init_qm_pq_params
{
	u8 vport_id /* VPORT ID */;
	u8 tc_id /* TC ID */;
	u8 wrr_group /* WRR group */;
	u8 rl_valid /* Indicates if a rate limiter should be allocated for the PQ (0/1) */;
};


/*
 * QM per-vport init parameters
 */
struct init_qm_vport_params
{
	u32 vport_rl /* rate limit in Mb/sec units. a value of 0 means dont configure. ignored if VPORT RL is globally disabled. */;
	u16 vport_wfq /* WFQ weight. A value of 0 means dont configure. ignored if VPORT WFQ is globally disabled. */;
	u16 first_tx_pq_id[NUM_OF_TCS] /* the first Tx PQ ID associated with this VPORT for each TC. */;
};

#endif /* __ECORE_HSI_INIT_FUNC__ */
