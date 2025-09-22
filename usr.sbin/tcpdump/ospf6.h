/*	$OpenBSD: ospf6.h,v 1.1 2000/04/26 21:35:39 jakob Exp $	*/

/*
 * Copyright (c) 1991, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * OSPF support contributed by Jeffrey Honig (jch@mitchell.cit.cornell.edu)
 */
#define	OSPF_TYPE_UMD	0	/* UMd's special monitoring packets */
#define	OSPF_TYPE_HELLO	1	/* Hello */
#define	OSPF_TYPE_DB	2	/* Database Description */
#define	OSPF_TYPE_LSR	3	/* Link State Request */
#define	OSPF_TYPE_LSU	4	/* Link State Update */
#define	OSPF_TYPE_LSA	5	/* Link State Ack */
#define	OSPF_TYPE_MAX	6

/* Options *_options	*/
#define OSPF6_OPTION_V6	0x01	/* V6 bit: A bit for peeping tom */
#define OSPF6_OPTION_E	0x02	/* E bit: External routes advertised	*/
#define OSPF6_OPTION_MC	0x04	/* MC bit: Multicast capable */
#define OSPF6_OPTION_N	0x08	/* N bit: For type-7 LSA */
#define OSPF6_OPTION_R	0x10	/* R bit: Router bit */
#define OSPF6_OPTION_DC	0x20	/* DC bit: Demand circuits */


/* db_flags	*/
#define	OSPF6_DB_INIT		0x04	    /*	*/
#define	OSPF6_DB_MORE		0x02
#define	OSPF6_DB_MASTER		0x01

/* ls_type	*/
#define	LS_TYPE_ROUTER		1   /* router link */
#define	LS_TYPE_NETWORK		2   /* network link */
#define	LS_TYPE_INTER_AP	3   /* Inter-Area-Prefix */
#define	LS_TYPE_INTER_AR	4   /* Inter-Area-Router */
#define	LS_TYPE_ASE		5   /* ASE */
#define	LS_TYPE_GROUP		6   /* Group membership */
#define	LS_TYPE_TYPE7		7   /* Type 7 LSA */
#define	LS_TYPE_LINK		8   /* Link LSA */
#define	LS_TYPE_INTRA_AP	9   /* Intra-Area-Prefix */
#define	LS_TYPE_MAX		10
#define LS_TYPE_MASK		0x1fff

#define LS_SCOPE_LINKLOCAL	0x0000
#define LS_SCOPE_AREA		0x2000
#define LS_SCOPE_AS		0x4000
#define LS_SCOPE_MASK		0x6000

/*************************************************
 *
 * is the above a bug in the documentation?
 *
 *************************************************/


/* rla_link.link_type	*/
#define	RLA_TYPE_ROUTER		1   /* point-to-point to another router	*/
#define	RLA_TYPE_TRANSIT	2   /* connection to transit network	*/
#define RLA_TYPE_VIRTUAL	4   /* virtual link			*/

/* rla_flags	*/
#define	RLA_FLAG_B	0x01
#define	RLA_FLAG_E	0x02
#define	RLA_FLAG_V	0x04
#define	RLA_FLAG_W	0x08

/* sla_tosmetric breakdown	*/
#define	SLA_MASK_TOS		0x7f000000
#define	SLA_MASK_METRIC		0x00ffffff
#define SLA_SHIFT_TOS		24

/* asla_tosmetric breakdown	*/
#define	ASLA_FLAG_EXTERNAL	0x80000000
#define	ASLA_MASK_TOS		0x7f000000
#define	ASLA_SHIFT_TOS		24
#define	ASLA_MASK_METRIC	0x00ffffff

/* multicast vertex type */
#define	MCLA_VERTEX_ROUTER	1
#define	MCLA_VERTEX_NETWORK	2

typedef u_int32_t rtrid_t;

/* link state advertisement header */
struct lsa_hdr {
    u_int16_t ls_age;
    u_int16_t ls_type;
    rtrid_t ls_stateid;
    rtrid_t ls_router;
    u_int32_t ls_seq;
    u_int16_t ls_chksum;
    u_int16_t ls_length;
} ;

struct lsa_prefix {
    u_int8_t lsa_p_len;
    u_int8_t lsa_p_opt;
    u_int16_t lsa_p_mbz;
    u_int8_t lsa_p_prefix[4];
};

/* link state advertisement */
struct lsa {
    struct lsa_hdr ls_hdr;

    /* Link state types */
    union {
	/* Router links advertisements */
	struct {
	    union {
		u_int8_t flg;
		u_int32_t opt;
	    } rla_flgandopt;
#define rla_flags	rla_flgandopt.flg
#define rla_options	rla_flgandopt.opt
	    struct rlalink {
		u_int8_t link_type;
		u_int8_t link_zero[1];
		u_int16_t link_metric;
		u_int32_t link_ifid;
		u_int32_t link_nifid;
		rtrid_t link_nrtid;
	    } rla_link[1];		/* may repeat	*/
	} un_rla;

	/* Network links advertisements */
	struct {
	    u_int32_t nla_options;
	    rtrid_t nla_router[1];	/* may repeat	*/
	} un_nla;

	/* Inter Area Prefix LSA */
	struct {
	    u_int32_t inter_ap_metric;
	    struct lsa_prefix inter_ap_prefix[1];
	} un_inter_ap;

#if 0
	/* Summary links advertisements */
	struct {
	    struct in_addr sla_mask;
	    u_int32_t sla_tosmetric[1];	/* may repeat	*/
	} un_sla;

	/* AS external links advertisements */
	struct {
	    struct in_addr asla_mask;
	    struct aslametric {
		u_int32_t asla_tosmetric;
		struct in_addr asla_forward;
		struct in_addr asla_tag;
	    } asla_metric[1];		/* may repeat	*/
	} un_asla;

	/* Multicast group membership */
	struct mcla {
	    u_int32_t mcla_vtype;
	    struct in_addr mcla_vid;
	} un_mcla[1];
#endif

	/* Type 7 LSA */

	/* Link LSA */
	struct llsa {
	    union {
		u_int8_t pri;
		u_int32_t opt;
	    } llsa_priandopt;
#define llsa_priority	llsa_priandopt.pri
#define llsa_options	llsa_priandopt.opt
	    struct in6_addr llsa_lladdr;
	    u_int32_t llsa_nprefix;
	    struct lsa_prefix llsa_prefix[1];
	} un_llsa;

	/* Intra-Area-Prefix */
	struct {
	    u_int16_t intra_ap_nprefix;
	    u_int16_t intra_ap_lstype;
	    rtrid_t intra_ap_lsid;
	    rtrid_t intra_ap_rtid;
	    struct lsa_prefix intra_ap_prefix[1];
	} un_intra_ap;
    } lsa_un;
} ;


/*
 * TOS metric struct (will be 0 or more in router links update)
 */
struct tos_metric {
    u_int8_t tos_type;
    u_int8_t tos_zero;
    u_int16_t tos_metric;
} ;

#define	OSPF_AUTH_SIZE	8

/*
 * the main header
 */
struct ospf6hdr {
    u_int8_t ospf6_version;
    u_int8_t ospf6_type;
    u_int16_t ospf6_len;
    rtrid_t ospf6_routerid;
    rtrid_t ospf6_areaid;
    u_int16_t ospf6_chksum;
    u_int8_t ospf6_instanceid;
    u_int8_t ospf6_rsvd;
    union {

	/* Hello packet */
	struct {
	    u_int32_t hello_ifid;
	    union {
		u_int8_t pri;
		u_int32_t opt;
	    } hello_priandopt;
#define hello_priority	hello_priandopt.pri
#define hello_options	hello_priandopt.opt
	    u_int16_t hello_helloint;
	    u_int16_t hello_deadint;
	    rtrid_t hello_dr;
	    rtrid_t hello_bdr;
	    rtrid_t hello_neighbor[1]; /* may repeat	*/
	} un_hello;

	/* Database Description packet */
	struct {
	    u_int32_t db_options;
	    u_int16_t db_mtu;
	    u_int8_t db_mbz;
	    u_int8_t db_flags;
	    u_int32_t db_seq;
	    struct lsa_hdr db_lshdr[1]; /* may repeat	*/
	} un_db;

	/* Link State Request */
	struct lsr {
	    u_int16_t ls_mbz;
	    u_int16_t ls_type;
	    rtrid_t ls_stateid;
	    rtrid_t ls_router;
	} un_lsr[1];		/* may repeat	*/

	/* Link State Update */
	struct {
	    u_int32_t lsu_count;
	    struct lsa lsu_lsa[1]; /* may repeat	*/
	} un_lsu;

	/* Link State Acknowledgement */
	struct {
	    struct lsa_hdr lsa_lshdr[1]; /* may repeat	*/
	} un_lsa ;
    } ospf6_un ;
} ;

#define	ospf6_hello	ospf6_un.un_hello
#define	ospf6_db	ospf6_un.un_db
#define	ospf6_lsr	ospf6_un.un_lsr
#define	ospf6_lsu	ospf6_un.un_lsu
#define	ospf6_lsa	ospf6_un.un_lsa

