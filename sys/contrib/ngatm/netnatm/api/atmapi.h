/*
 * Copyright (c) 2003-2004
 *	Hartmut Brandt
 *	All rights reserved.
 *
 * Copyright (c) 2001-2002
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE AND DOCUMENTATION IS PROVIDED BY THE AUTHORS
 * AND ITS CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHORS OR ITS CONTRIBUTORS  BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Begemot: libunimsg/netnatm/api/atmapi.h,v 1.1 2004/07/08 08:21:48 brandt Exp $
 *
 * ATM API as defined per af-saa-0108
 */
#ifndef _NETNATM_API_ATMAPI_H_
#define _NETNATM_API_ATMAPI_H_

#include <sys/types.h>

/* size of an endpointlen including trailing \0 */
#define	ATM_EPNAMSIZ	65

enum atmstate {
	ATM_A0,		/* non existent */
	ATM_A1,		/* initial */
	ATM_A2,		/* outgoing call preparation */
	ATM_A3,		/* outgoing call requested */
	ATM_A4,		/* incoming call preparation */
	ATM_A5,		/* wait incoming call */
	ATM_A6,		/* incoming call present */
	ATM_A7,		/* incoming call requested */
	ATM_A8,		/* p2p data transfer */
	ATM_A9,		/* p2mp root data transfer */
	ATM_A10,	/* p2mp leaf data transfer */
	ATM_A11,	/* terminated */
};

enum atmop {
	ATMOP_RESP,				/* 0 */
	ATMOP_ABORT_CONNECTION,
	ATMOP_ACCEPT_INCOMING_CALL,
	ATMOP_ADD_PARTY,
	ATMOP_ADD_PARTY_REJECT,
	ATMOP_ADD_PARTY_SUCCESS,		/* 5 */
	ATMOP_ARRIVAL_OF_INCOMING_CALL,
	ATMOP_CALL_RELEASE,
	ATMOP_CONNECT_OUTGOING_CALL,
	ATMOP_DROP_PARTY,
	ATMOP_GET_LOCAL_PORT_INFO,		/* 10 */
	ATMOP_P2MP_CALL_ACTIVE,
	ATMOP_P2P_CALL_ACTIVE,
	ATMOP_PREPARE_INCOMING_CALL,
	ATMOP_PREPARE_OUTGOING_CALL,
	ATMOP_QUERY_CONNECTION_ATTRIBUTES,	/* 15 */
	ATMOP_REJECT_INCOMING_CALL,
	ATMOP_SET_CONNECTION_ATTRIBUTES,
	ATMOP_WAIT_ON_INCOMING_CALL,
	ATMOP_SET_CONNECTION_ATTRIBUTES_X,
	ATMOP_QUERY_CONNECTION_ATTRIBUTES_X,	/* 20 */
	ATMOP_QUERY_STATE,
};

#define ATM_DEFINE_ERRORS						\
   DEF(ATMERR_OK,		 0, "OK")				\
   DEF(ATMERR_SYS,		-1, "syscall error")			\
   DEF(ATMERR_BAD_OP,		-2, "bad operation")			\
   DEF(ATMERR_BAD_ARGS,		-3, "bad arguments for operation")	\
   DEF(ATMERR_BAD_STATE,	-4, "operation in bad state")		\
   DEF(ATMERR_BAD_ATTR,		-5, "unknown attribute")		\
   DEF(ATMERR_BAD_VALUE,	-6, "bad attribute value")		\
   DEF(ATMERR_BUSY,		-7, "busy")				\
   DEF(ATMERR_RDONLY,		-8, "read-only attribute")		\
   DEF(ATMERR_BAD_SAP,		-9, "bad SAP")				\
   DEF(ATMERR_OVERLAP,		-10,"overlaping SAP")			\
   DEF(ATMERR_BAD_ENDPOINT,	-11,"bad ATM endpoint")			\
   DEF(ATMERR_PREVIOUSLY_ABORTED,-12,"previously aborted")		\
   DEF(ATMERR_NO_CALL,		-13,"no incoming call")			\
   DEF(ATMERR_BAD_LEAF_IDENT,	-14,"bad leaf identifier")		\
   DEF(ATMERR_BAD_PORT,		-15,"unknown port")			\
   DEF(ATMERR_BAD_SIGNAL,	-29-UNIAPI_ERROR_BAD_SIGNAL, "bad signal")\
   DEF(ATMERR_BADCU,		-29-UNIAPI_ERROR_BADCU, "bad coordinator state")\
   DEF(ATMERR_BAD_CALLSTATE,	-29-UNIAPI_ERROR_BAD_CALLSTATE, "bad call state")\
   DEF(ATMERR_BAD_EPSTATE,	-29-UNIAPI_ERROR_BAD_EPSTATE, "bad party state")\
   DEF(ATMERR_BAD_UNIARG,	-29-UNIAPI_ERROR_BAD_ARG, "bad uni argument")\
   DEF(ATMERR_BAD_CALL,		-29-UNIAPI_ERROR_BAD_CALL, "unknown call")\
   DEF(ATMERR_BAD_PARTY,	-29-UNIAPI_ERROR_BAD_PARTY, "unknown party")\
   DEF(ATMERR_BAD_CTYPE,	-29-UNIAPI_ERROR_BAD_CTYPE, "wrong call type")\
   DEF(ATMERR_BAD_IE,		-29-UNIAPI_ERROR_BAD_IE, "bad information element")\
   DEF(ATMERR_EPREF_INUSE,	-29-UNIAPI_ERROR_EPREF_INUSE, "endpoint reference in use")\
   DEF(ATMERR_MISSING_IE,	-29-UNIAPI_ERROR_MISSING_IE, "missing information element")\
   DEF(ATMERR_ENCODING,		-29-UNIAPI_ERROR_ENCODING, "encoding error")\
   DEF(ATMERR_NOMEM,		-29-UNIAPI_ERROR_NOMEM, "no memory")\
   DEF(ATMERR_UNIBUSY,		-29-UNIAPI_ERROR_BUSY, "uni process busy")

#define ATM_MKUNIERR(E)	(-29 - (E))

enum atm_error {
#define DEF(NAME,VAL,STR)	NAME = (VAL),
ATM_DEFINE_ERRORS
#undef DEF
};

enum atm_attribute {
	ATM_ATTR_NONE = 0,
	ATM_ATTR_BLLI_SELECTOR,
	ATM_ATTR_BLLI,
	ATM_ATTR_BEARER,
	ATM_ATTR_TRAFFIC,
	ATM_ATTR_QOS,
	ATM_ATTR_EXQOS,
	ATM_ATTR_CALLED,
	ATM_ATTR_CALLEDSUB,
	ATM_ATTR_CALLING,
	ATM_ATTR_CALLINGSUB,
	ATM_ATTR_AAL,
	ATM_ATTR_EPREF,
	ATM_ATTR_CONNED,
	ATM_ATTR_CONNEDSUB,
	ATM_ATTR_EETD,
	ATM_ATTR_ABRSETUP,
	ATM_ATTR_ABRADD,
	ATM_ATTR_CONNID,
	ATM_ATTR_MDCR,
};

struct atm_resp {
	int32_t		resp;
	uint32_t	data;		/* type of attached data */
};
enum {
	ATMRESP_NONE,			/* no data */
	ATMRESP_ATTRS,			/* attribute(s) */
	ATMRESP_PORTS,			/* port info */
	ATMRESP_STATE,			/* endpoint state */
	ATMRESP_EXSTAT,			/* extended status */
};

struct atm_abort_connection {
	struct uni_ie_cause cause;
};

struct atm_query_connection_attributes {
	uint32_t	attr;
};
struct atm_set_connection_attributes {
	uint32_t	attr;
};
struct atm_query_connection_attributes_x {
	uint32_t	count;
#if defined(__GNUC__) && __GNUC__ < 3
	uint32_t	attr[0];
#else
	uint32_t	attr[];
#endif
};
struct atm_set_connection_attributes_x {
	uint32_t	count;
#if defined(__GNUC__) && __GNUC__ < 3
	uint32_t	attr[0];
#else
	uint32_t	attr[];
#endif
};
struct atm_prepare_incoming_call {
	struct uni_sap	sap;
	uint32_t	queue_size;
};
struct atm_connect_outgoing_call {
	struct uni_ie_called	called;
};
struct atm_call_release {
	struct uni_ie_cause	cause[2];
};
struct atm_p2p_call_active {
	struct uni_ie_connid	connid;
};
struct atm_p2mp_call_active {
	struct uni_ie_connid	connid;
};
struct atm_accept_incoming_call {
	char	newep[ATM_EPNAMSIZ];
};
struct atm_reject_incoming_call {
	struct uni_ie_cause	cause;
};
struct atm_add_party {
	uint16_t		leaf_ident;
	struct uni_ie_called	called;
};
struct atm_add_party_success {
	uint16_t		leaf_ident;
};
struct atm_add_party_reject {
	uint16_t		leaf_ident;
	struct uni_ie_cause	cause;
};
struct atm_drop_party {
	uint16_t		leaf_ident;
	struct uni_ie_cause	cause;
};

/*
 * Get local port info. If port is 0, information on all ports is returned,
 * otherwise only on the named port.
 * The response consists of a header with two counters, a list of ports
 * (struct atm_port_info) and a list of addresses (struct uni_addr).
 * The port to which an address belongs is implicit in the num_addrs field
 * of the port.
 */
struct atm_get_local_port_info {
	uint32_t	port;
};

struct atm_port_list {
	uint32_t	num_ports;	/* number of ports */
	uint32_t	num_addrs;	/* total number of addresses */
};

struct atm_port_info {
	uint32_t	port;
	uint32_t	pcr;
	uint32_t	max_vpi_bits;
	uint32_t	max_vci_bits;
	uint32_t	max_svpc_vpi;
	uint32_t	max_svcc_vpi;
	uint32_t	min_svcc_vci;
	u_char		esi[6];
	uint32_t	num_addrs;	/* number of addresses on this port */
};

/*
 * Endpoint state info
 */
struct atm_epstate {
	char	name[ATM_EPNAMSIZ];
	uint8_t	state;
};

/*
 * Extended status information.
 */
struct atm_exstatus {
	uint32_t	neps;		/* endpoints */
	uint32_t	nports;		/* ports */
	uint32_t	nconns;		/* connections */
	uint32_t	nparties;	/* number of parties */
};
struct atm_exstatus_ep {
	char		name[ATM_EPNAMSIZ];
	uint8_t		state;		/* Ux */
};
struct atm_exstatus_port {
	uint32_t	portno;
	uint8_t		state;
};
struct atm_exstatus_conn {
	uint32_t	id;
	uint32_t	cref;		/* (flag << 23) | cref */
	uint32_t	port;
	char		ep[ATM_EPNAMSIZ];	/* \0 - none */
	uint8_t		state;		/* Cx */
};
struct atm_exstatus_party {
	uint32_t	connid;
	uint16_t	epref;
	uint8_t		state;		/* Px */
};
#endif
