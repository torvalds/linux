/*
 * ng_l2cap_cmds.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * $Id: ng_l2cap_cmds.h,v 1.4 2003/04/01 18:15:26 max Exp $
 * $FreeBSD$
 */

#ifndef _NETGRAPH_L2CAP_CMDS_H_
#define _NETGRAPH_L2CAP_CMDS_H_

/******************************************************************************
 ******************************************************************************
 **                L2CAP to L2CAP signaling command macros
 ******************************************************************************
 ******************************************************************************/

/*
 * Note: All L2CAP implementations are required to support minimal signaling
 *       MTU of 48 bytes. In order to simplify things we will send one command
 *       per one L2CAP packet. Given evrything above we can assume that one
 *       signaling packet will fit into single mbuf.
 */

/* L2CAP_CommandRej */
#define	_ng_l2cap_cmd_rej(_m, _ident, _reason, _mtu, _scid, _dcid)	\
do {									\
	struct _cmd_rej {						\
		ng_l2cap_cmd_hdr_t	 hdr;				\
		ng_l2cap_cmd_rej_cp	 param;				\
		ng_l2cap_cmd_rej_data_t	 data;				\
	} __attribute__ ((packed))	*c = NULL;			\
									\
	MGETHDR((_m), M_NOWAIT, MT_DATA);				\
	if ((_m) == NULL) 						\
		break;							\
									\
	c = mtod((_m), struct _cmd_rej *);				\
	c->hdr.code = NG_L2CAP_CMD_REJ;					\
	c->hdr.ident = (_ident);					\
	c->hdr.length = sizeof(c->param);				\
									\
	c->param.reason = htole16((_reason));				\
									\
	if ((_reason) == NG_L2CAP_REJ_MTU_EXCEEDED) {			\
		c->data.mtu.mtu = htole16((_mtu));			\
		c->hdr.length += sizeof(c->data.mtu);			\
	} else if ((_reason) == NG_L2CAP_REJ_INVALID_CID) {		\
		c->data.cid.scid = htole16((_scid));			\
		c->data.cid.dcid = htole16((_dcid));			\
		c->hdr.length += sizeof(c->data.cid);			\
	}								\
									\
	(_m)->m_pkthdr.len = (_m)->m_len = sizeof(c->hdr) + 		\
					c->hdr.length;			\
									\
	c->hdr.length = htole16(c->hdr.length);				\
} while (0)

/* L2CAP_ConnectReq */
#define	_ng_l2cap_con_req(_m, _ident, _psm, _scid)			\
do {									\
	struct _con_req {						\
		ng_l2cap_cmd_hdr_t	 hdr;				\
		ng_l2cap_con_req_cp	 param;				\
	} __attribute__ ((packed)) 	*c = NULL;			\
									\
	MGETHDR((_m), M_NOWAIT, MT_DATA);				\
	if ((_m) == NULL) 						\
		break;							\
									\
	(_m)->m_pkthdr.len = (_m)->m_len = sizeof(*c);			\
									\
	c = mtod((_m), struct _con_req *);				\
	c->hdr.code = NG_L2CAP_CON_REQ;					\
	c->hdr.ident = (_ident);					\
	c->hdr.length = htole16(sizeof(c->param));			\
									\
	c->param.psm = htole16((_psm));					\
	c->param.scid = htole16((_scid));				\
} while (0)

/* L2CAP_ConnectRsp */
#define _ng_l2cap_con_rsp(_m, _ident, _dcid, _scid, _result, _status)	\
do {									\
	struct _con_rsp {						\
		ng_l2cap_cmd_hdr_t	 hdr;				\
		ng_l2cap_con_rsp_cp	 param;				\
	} __attribute__ ((packed))	*c = NULL;			\
									\
	MGETHDR((_m), M_NOWAIT, MT_DATA);				\
	if ((_m) == NULL) 						\
		break;							\
									\
	(_m)->m_pkthdr.len = (_m)->m_len = sizeof(*c);			\
									\
	c = mtod((_m), struct _con_rsp *);				\
	c->hdr.code = NG_L2CAP_CON_RSP;					\
	c->hdr.ident = (_ident);					\
	c->hdr.length = htole16(sizeof(c->param));			\
									\
	c->param.dcid = htole16((_dcid));				\
	c->param.scid = htole16((_scid));				\
	c->param.result = htole16((_result));				\
	c->param.status = htole16((_status));				\
} while (0)

/* L2CAP_ConfigReq */
#define	_ng_l2cap_cfg_req(_m, _ident, _dcid, _flags, _data)		\
do {									\
	struct _cfg_req {						\
		ng_l2cap_cmd_hdr_t	 hdr;				\
		ng_l2cap_cfg_req_cp	 param;				\
	} __attribute__ ((packed))	*c = NULL;			\
									\
	MGETHDR((_m), M_NOWAIT, MT_DATA);				\
	if ((_m) == NULL) { 						\
		NG_FREE_M((_data));					\
		break;							\
	}								\
									\
	(_m)->m_pkthdr.len = (_m)->m_len = sizeof(*c);			\
									\
	c = mtod((_m), struct _cfg_req *);				\
	c->hdr.code = NG_L2CAP_CFG_REQ;					\
	c->hdr.ident = (_ident);					\
	c->hdr.length = sizeof(c->param);				\
									\
	c->param.dcid = htole16((_dcid));				\
	c->param.flags = htole16((_flags));				\
	if ((_data) != NULL) {						\
		int	l = (_data)->m_pkthdr.len;			\
									\
		m_cat((_m), (_data));					\
		c->hdr.length += l;					\
		(_m)->m_pkthdr.len += l;				\
	}								\
									\
	c->hdr.length = htole16(c->hdr.length);				\
} while (0)

/* L2CAP_ConfigRsp */
#define _ng_l2cap_cfg_rsp(_m, _ident, _scid, _flags, _result, _data)	\
do {									\
	struct _cfg_rsp {						\
		ng_l2cap_cmd_hdr_t	 hdr;				\
		ng_l2cap_cfg_rsp_cp	 param;				\
	} __attribute__ ((packed))	*c = NULL;			\
									\
	MGETHDR((_m), M_NOWAIT, MT_DATA);				\
	if ((_m) == NULL) { 						\
		NG_FREE_M((_data));					\
		break;							\
	}								\
									\
	(_m)->m_pkthdr.len = (_m)->m_len = sizeof(*c);			\
									\
	c = mtod((_m), struct _cfg_rsp *);				\
	c->hdr.code = NG_L2CAP_CFG_RSP;					\
	c->hdr.ident = (_ident);					\
	c->hdr.length = sizeof(c->param);				\
									\
	c->param.scid = htole16((_scid));				\
	c->param.flags = htole16((_flags));				\
	c->param.result = htole16((_result));				\
	if ((_data) != NULL) {						\
		int	l = (_data)->m_pkthdr.len;			\
									\
		m_cat((_m), (_data));					\
		c->hdr.length += l;					\
		(_m)->m_pkthdr.len += l;				\
	}								\
									\
	c->hdr.length = htole16(c->hdr.length);				\
} while (0)

#define _ng_l2cap_cmd_urs(_m, _ident, _result)	\
do {									\
	struct  _cmd_urs{						\
		ng_l2cap_cmd_hdr_t	 hdr;				\
		uint16_t	 result;				\
	} __attribute__ ((packed))	*c = NULL;			\
									\
	MGETHDR((_m), M_NOWAIT, MT_DATA);				\
									\
	(_m)->m_pkthdr.len = (_m)->m_len = sizeof(*c);			\
									\
	c = mtod((_m), struct _cmd_urs *);				\
	c->hdr.code = NG_L2CAP_CMD_PARAM_UPDATE_RESPONSE;		\
	c->hdr.ident = (_ident);					\
	c->hdr.length = sizeof(c->result);				\
									\
	c->result = htole16((_result));				\
} while (0)

/* Build configuration options */
#define _ng_l2cap_build_cfg_options(_m, _mtu, _flush_timo, _flow)	\
do {									\
	u_int8_t	*p = NULL;					\
									\
	MGETHDR((_m), M_NOWAIT, MT_DATA);				\
	if ((_m) == NULL)						\
		break;							\
									\
	(_m)->m_pkthdr.len = (_m)->m_len = 0;				\
	p = mtod((_m), u_int8_t *);					\
									\
	if ((_mtu) != NULL) {						\
		struct _cfg_opt_mtu {					\
			ng_l2cap_cfg_opt_t	 hdr;			\
			u_int16_t		 val;			\
		} __attribute__ ((packed))	*o = NULL;		\
									\
		o = (struct _cfg_opt_mtu *) p;				\
		o->hdr.type = NG_L2CAP_OPT_MTU;				\
		o->hdr.length = sizeof(o->val);				\
		o->val = htole16(*(u_int16_t *)(_mtu));			\
									\
		(_m)->m_pkthdr.len += sizeof(*o);			\
		p += sizeof(*o);					\
	}								\
									\
	if ((_flush_timo) != NULL) {					\
		struct _cfg_opt_flush {					\
			ng_l2cap_cfg_opt_t	 hdr;			\
			u_int16_t		 val;			\
		} __attribute__ ((packed))	*o = NULL;		\
									\
		o = (struct _cfg_opt_flush *) p;			\
		o->hdr.type = NG_L2CAP_OPT_FLUSH_TIMO;			\
		o->hdr.length = sizeof(o->val);				\
		o->val = htole16(*(u_int16_t *)(_flush_timo));		\
									\
		(_m)->m_pkthdr.len += sizeof(*o);			\
		p += sizeof(*o);					\
	}								\
									\
	if ((_flow) != NULL) {						\
		struct _cfg_opt_flow {					\
			ng_l2cap_cfg_opt_t	 hdr;			\
			ng_l2cap_flow_t		 val;			\
		} __attribute__ ((packed))	*o = NULL;		\
									\
		o = (struct _cfg_opt_flow *) p;				\
		o->hdr.type = NG_L2CAP_OPT_QOS;				\
		o->hdr.length = sizeof(o->val);				\
		o->val.flags = ((ng_l2cap_flow_p)(_flow))->flags;	\
		o->val.service_type = ((ng_l2cap_flow_p)		\
				(_flow))->service_type;			\
		o->val.token_rate =					\
			htole32(((ng_l2cap_flow_p)(_flow))->token_rate);\
		o->val.token_bucket_size =				\
			htole32(((ng_l2cap_flow_p)			\
				(_flow))->token_bucket_size);		\
		o->val.peak_bandwidth = 				\
			htole32(((ng_l2cap_flow_p)			\
				(_flow))->peak_bandwidth);		\
		o->val.latency = htole32(((ng_l2cap_flow_p)		\
				(_flow))->latency);			\
		o->val.delay_variation = 				\
			htole32(((ng_l2cap_flow_p)			\
				(_flow))->delay_variation);		\
									\
		(_m)->m_pkthdr.len += sizeof(*o);			\
	}								\
									\
	(_m)->m_len = (_m)->m_pkthdr.len;				\
} while (0)

/* L2CAP_DisconnectReq */
#define	_ng_l2cap_discon_req(_m, _ident, _dcid, _scid)			\
do {									\
	struct _discon_req {						\
		ng_l2cap_cmd_hdr_t	 hdr;				\
		ng_l2cap_discon_req_cp	 param;				\
	} __attribute__ ((packed))	*c = NULL;			\
									\
	MGETHDR((_m), M_NOWAIT, MT_DATA);				\
	if ((_m) == NULL)						\
		break;							\
									\
	(_m)->m_pkthdr.len = (_m)->m_len = sizeof(*c);			\
									\
	c = mtod((_m), struct _discon_req *);				\
	c->hdr.code = NG_L2CAP_DISCON_REQ;				\
	c->hdr.ident = (_ident);					\
	c->hdr.length = htole16(sizeof(c->param));			\
									\
	c->param.dcid = htole16((_dcid));				\
	c->param.scid = htole16((_scid));				\
} while (0)

/* L2CA_DisconnectRsp */
#define	_ng_l2cap_discon_rsp(_m, _ident, _dcid, _scid)			\
do {									\
	struct _discon_rsp {						\
		ng_l2cap_cmd_hdr_t	 hdr;				\
		ng_l2cap_discon_rsp_cp	 param;				\
	} __attribute__ ((packed))	*c = NULL;			\
									\
	MGETHDR((_m), M_NOWAIT, MT_DATA);				\
	if ((_m) == NULL)						\
		break;							\
									\
	(_m)->m_pkthdr.len = (_m)->m_len = sizeof(*c);			\
									\
	c = mtod((_m), struct _discon_rsp *);				\
	c->hdr.code = NG_L2CAP_DISCON_RSP;				\
	c->hdr.ident = (_ident);					\
	c->hdr.length = htole16(sizeof(c->param));			\
									\
	c->param.dcid = htole16((_dcid));				\
	c->param.scid = htole16((_scid));				\
} while (0)

/* L2CAP_EchoReq */
#define	_ng_l2cap_echo_req(_m, _ident, _data, _size)			\
do {									\
	ng_l2cap_cmd_hdr_t	*c = NULL;				\
									\
	MGETHDR((_m), M_NOWAIT, MT_DATA);				\
	if ((_m) == NULL) 						\
		break;							\
									\
	(_m)->m_pkthdr.len = (_m)->m_len = sizeof(*c);			\
									\
	c = mtod((_m), ng_l2cap_cmd_hdr_t *);				\
	c->code = NG_L2CAP_ECHO_REQ;					\
	c->ident = (_ident);						\
	c->length = 0;							\
									\
	if ((_data) != NULL) {						\
		m_copyback((_m), sizeof(*c), (_size), (_data));		\
		c->length += (_size);					\
	}								\
									\
	c->length = htole16(c->length);					\
} while (0)

/* L2CAP_InfoReq */
#define	_ng_l2cap_info_req(_m, _ident, _type)				\
do {									\
	struct _info_req {						\
		ng_l2cap_cmd_hdr_t	 hdr;				\
		ng_l2cap_info_req_cp	 param;				\
	} __attribute__ ((packed))	*c = NULL;			\
									\
	MGETHDR((_m), M_NOWAIT, MT_DATA);				\
	if ((_m) == NULL)						\
		break;							\
									\
	(_m)->m_pkthdr.len = (_m)->m_len = sizeof(*c);			\
									\
	c = mtod((_m), struct _info_req *);				\
	c->hdr.code = NG_L2CAP_INFO_REQ;				\
	c->hdr.ident = (_ident);					\
	c->hdr.length = htole16(sizeof(c->param));			\
									\
	c->param.type = htole16((_type));				\
} while (0)

/* L2CAP_InfoRsp */
#define	_ng_l2cap_info_rsp(_m, _ident, _type, _result, _mtu)		\
do {									\
	struct _info_rsp {						\
		ng_l2cap_cmd_hdr_t	 hdr;				\
		ng_l2cap_info_rsp_cp	 param;				\
		ng_l2cap_info_rsp_data_t data;				\
	} __attribute__ ((packed))	*c = NULL;			\
									\
	MGETHDR((_m), M_NOWAIT, MT_DATA);				\
	if ((_m) == NULL) 						\
		break;							\
									\
	c = mtod((_m), struct _info_rsp *);				\
	c->hdr.code = NG_L2CAP_INFO_RSP;				\
	c->hdr.ident = (_ident);					\
	c->hdr.length = sizeof(c->param);				\
									\
	c->param.type = htole16((_type));				\
	c->param.result = htole16((_result));				\
									\
	if ((_result) == NG_L2CAP_SUCCESS) {				\
		switch ((_type)) {					\
		case NG_L2CAP_CONNLESS_MTU:				\
			c->data.mtu.mtu = htole16((_mtu));		\
			c->hdr.length += sizeof((c->data.mtu.mtu));	\
			break;						\
		}							\
	}								\
									\
	(_m)->m_pkthdr.len = (_m)->m_len = sizeof(c->hdr) +		\
					c->hdr.length;			\
									\
	c->hdr.length = htole16(c->hdr.length);		 		\
} while (0)

void ng_l2cap_con_wakeup              (ng_l2cap_con_p);
void ng_l2cap_con_fail                (ng_l2cap_con_p, u_int16_t);
void ng_l2cap_process_command_timeout (node_p, hook_p, void *, int);

#endif /* ndef _NETGRAPH_L2CAP_CMDS_H_ */

