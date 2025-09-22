/*	$OpenBSD: ospf.h,v 1.11 2019/04/14 00:37:31 dlg Exp $	*/

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
#define OSPF_OPTION_T	0x01	/* T bit: TOS support	*/
#define OSPF_OPTION_E	0x02	/* E bit: External routes advertised	*/
#define	OSPF_OPTION_MC	0x04	/* MC bit: Multicast capable */

/* ospf_authtype	*/
#define	OSPF_AUTH_NONE		0	/* No auth-data */
#define	OSPF_AUTH_SIMPLE	1	/* Simple password */
#define OSPF_AUTH_MD5		2	/* MD5 authentication */
#define     OSPF_AUTH_MD5_LEN	16	/* length of MD5 authentication */


/* db_flags	*/
#define	OSPF_DB_INIT		0x04	    /*	*/
#define	OSPF_DB_MORE		0x02
#define	OSPF_DB_MASTER		0x01

/* ls_type	*/
#define	LS_TYPE_ROUTER		1   /* router link */
#define	LS_TYPE_NETWORK		2   /* network link */
#define	LS_TYPE_SUM_IP		3   /* summary link */
#define	LS_TYPE_SUM_ABR		4   /* summary area link */
#define	LS_TYPE_ASE		5   /* ASE  */
#define	LS_TYPE_GROUP		6   /* Group membership (multicast */
				    /* extensions 23 July 1991) */
#define	LS_TYPE_MAX		7

/*************************************************
 *
 * is the above a bug in the documentation?
 *
 *************************************************/


/* rla_link.link_type	*/
#define	RLA_TYPE_ROUTER		1   /* point-to-point to another router	*/
#define	RLA_TYPE_TRANSIT	2   /* connection to transit network	*/
#define	RLA_TYPE_STUB		3   /* connection to stub network	*/
#define RLA_TYPE_VIRTUAL	4   /* virtual link			*/

/* rla_flags	*/
#define	RLA_FLAG_B	0x01
#define	RLA_FLAG_E	0x02
#define	RLA_FLAG_W1	0x04
#define	RLA_FLAG_W2	0x08

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

/* link state advertisement header */
struct lsa_hdr {
    u_short ls_age;
    u_char ls_options;
    u_char ls_type;
    struct in_addr ls_stateid;
    struct in_addr ls_router;
    u_int32_t ls_seq;
    u_short ls_chksum;
    u_short ls_length;
} ;

/* link state advertisement */
struct lsa {
    struct lsa_hdr ls_hdr;

    /* Link state types */
    union {
	/* Router links advertisements */
	struct {
	    u_char rla_flags;
	    u_char rla_zero[1];
	    u_short rla_count;
	    struct rlalink {
		struct in_addr link_id;
		struct in_addr link_data;
		u_char link_type;
		u_char link_toscount;
		u_short link_tos0metric;
	    } rla_link[1];		/* may repeat	*/
	} un_rla;

	/* Network links advertisements */
	struct {
	    struct in_addr nla_mask;
	    struct in_addr nla_router[1];	/* may repeat	*/
	} un_nla;

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
		u_int32_t asla_tag;
	    } asla_metric[1];		/* may repeat	*/
	} un_asla;

	/* Multicast group membership */
	struct mcla {
	    u_int32_t mcla_vtype;
	    struct in_addr mcla_vid;
	} un_mcla[1];
    } lsa_un;
} ;


/*
 * TOS metric struct (will be 0 or more in router links update)
 */
struct tos_metric {
    u_char tos_type;
    u_char tos_zero;
    u_short tos_metric;
} ;

#define	OSPF_AUTH_SIZE	8

/*
 * the main header
 */
struct ospfhdr {
    u_char ospf_version;
    u_char ospf_type;
    u_short ospf_len;
    struct in_addr ospf_routerid;
    struct in_addr ospf_areaid;
    u_short ospf_chksum;
    u_short ospf_authtype;
    u_char ospf_authdata[OSPF_AUTH_SIZE];
    union {

	/* Hello packet */
	struct {
	    struct in_addr hello_mask;
	    u_short hello_helloint;
	    u_char hello_options;
	    u_char hello_priority;
	    u_int32_t hello_deadint;
	    struct in_addr hello_dr;
	    struct in_addr hello_bdr;
	    struct in_addr hello_neighbor[1]; /* may repeat	*/
	} un_hello;

	/* Database Description packet */
	struct {
	    u_int16_t db_mtu;
	    u_char db_options;
	    u_char db_flags;
	    u_int32_t db_seq;
	    struct lsa_hdr db_lshdr[1]; /* may repeat	*/
	} un_db;

	/* Link State Request */
	struct lsr {
	    u_int32_t ls_type;
	    struct in_addr ls_stateid;
	    struct in_addr ls_router;
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
    } ospf_un ;
} ;

struct ospf_md5_authdata {
	uint16_t	auth_md5_offset;
	uint8_t		auth_keyid;
	uint8_t		auth_len;
	uint32_t	auth_seq;
};

#define	ospf_hello	ospf_un.un_hello
#define	ospf_db		ospf_un.un_db
#define	ospf_lsr	ospf_un.un_lsr
#define	ospf_lsu	ospf_un.un_lsu
#define	ospf_lsa	ospf_un.un_lsa

