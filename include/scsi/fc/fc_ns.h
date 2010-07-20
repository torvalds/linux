/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

#ifndef _FC_NS_H_
#define	_FC_NS_H_

#include <linux/types.h>

/*
 * Fibre Channel Services - Name Service (dNS)
 * From T11.org FC-GS-2 Rev 5.3 November 1998.
 */

/*
 * Common-transport sub-type for Name Server.
 */
#define	FC_NS_SUBTYPE	    2	/* fs_ct_hdr.ct_fs_subtype */

/*
 * Name server Requests.
 * Note:  this is an incomplete list, some unused requests are omitted.
 */
enum fc_ns_req {
	FC_NS_GA_NXT =	0x0100,		/* get all next */
	FC_NS_GI_A =	0x0101,		/* get identifiers - scope */
	FC_NS_GPN_ID =	0x0112,		/* get port name by ID */
	FC_NS_GNN_ID =	0x0113,		/* get node name by ID */
	FC_NS_GID_PN =	0x0121,		/* get ID for port name */
	FC_NS_GID_NN =	0x0131,		/* get IDs for node name */
	FC_NS_GID_FT =	0x0171,		/* get IDs by FC4 type */
	FC_NS_GPN_FT =	0x0172,		/* get port names by FC4 type */
	FC_NS_GID_PT =	0x01a1,		/* get IDs by port type */
	FC_NS_RPN_ID =	0x0212,		/* reg port name for ID */
	FC_NS_RNN_ID =	0x0213,		/* reg node name for ID */
	FC_NS_RFT_ID =	0x0217,		/* reg FC4 type for ID */
	FC_NS_RSPN_ID =	0x0218,		/* reg symbolic port name */
	FC_NS_RFF_ID =	0x021f,		/* reg FC4 Features for ID */
	FC_NS_RSNN_NN =	0x0239,		/* reg symbolic node name */
};

/*
 * Port type values.
 */
enum fc_ns_pt {
	FC_NS_UNID_PORT = 0x00,	/* unidentified */
	FC_NS_N_PORT =	0x01,	/* N port */
	FC_NS_NL_PORT =	0x02,	/* NL port */
	FC_NS_FNL_PORT = 0x03,	/* F/NL port */
	FC_NS_NX_PORT =	0x7f,	/* Nx port */
	FC_NS_F_PORT =	0x81,	/* F port */
	FC_NS_FL_PORT =	0x82,	/* FL port */
	FC_NS_E_PORT =	0x84,	/* E port */
	FC_NS_B_PORT =	0x85,	/* B port */
};

/*
 * Port type object.
 */
struct fc_ns_pt_obj {
	__u8		pt_type;
};

/*
 * Port ID object
 */
struct fc_ns_fid {
	__u8		fp_flags;	/* flags for responses only */
	__u8		fp_fid[3];
};

/*
 * fp_flags in port ID object, for responses only.
 */
#define	FC_NS_FID_LAST	0x80		/* last object */

/*
 * FC4-types object.
 */
#define	FC_NS_TYPES	256	/* number of possible FC-4 types */
#define	FC_NS_BPW	32	/* bits per word in bitmap */

struct fc_ns_fts {
	__be32	ff_type_map[FC_NS_TYPES / FC_NS_BPW]; /* bitmap of FC-4 types */
};

/*
 * FC4-features object.
 */
struct fc_ns_ff	{
	__be32	fd_feat[FC_NS_TYPES * 4 / FC_NS_BPW]; /* 4-bits per FC-type */
};

/*
 * GID_PT request.
 */
struct fc_ns_gid_pt {
	__u8		fn_pt_type;
	__u8		fn_domain_id_scope;
	__u8		fn_area_id_scope;
	__u8		fn_resvd;
};

/*
 * GID_FT or GPN_FT request.
 */
struct fc_ns_gid_ft {
	__u8		fn_resvd;
	__u8		fn_domain_id_scope;
	__u8		fn_area_id_scope;
	__u8		fn_fc4_type;
};

/*
 * GPN_FT response.
 */
struct fc_gpn_ft_resp {
	__u8		fp_flags;	/* see fp_flags definitions above */
	__u8		fp_fid[3];	/* port ID */
	__be32		fp_resvd;
	__be64		fp_wwpn;	/* port name */
};

/*
 * GID_PN request
 */
struct fc_ns_gid_pn {
	__be64     fn_wwpn;    /* port name */
};

/*
 * GID_PN response
 */
struct fc_gid_pn_resp {
	__u8      fp_resvd;
	__u8      fp_fid[3];     /* port ID */
};

/*
 * RFT_ID request - register FC-4 types for ID.
 */
struct fc_ns_rft_id {
	struct fc_ns_fid fr_fid;	/* port ID object */
	struct fc_ns_fts fr_fts;	/* FC-4 types object */
};

/*
 * RPN_ID request - register port name for ID.
 * RNN_ID request - register node name for ID.
 */
struct fc_ns_rn_id {
	struct fc_ns_fid fr_fid;	/* port ID object */
	__be64		fr_wwn;		/* node name or port name */
} __attribute__((__packed__));

/*
 * RSNN_NN request - register symbolic node name
 */
struct fc_ns_rsnn {
	__be64		fr_wwn;		/* node name */
	__u8		fr_name_len;
	char		fr_name[];
} __attribute__((__packed__));

/*
 * RSPN_ID request - register symbolic port name
 */
struct fc_ns_rspn {
	struct fc_ns_fid fr_fid;	/* port ID object */
	__u8		fr_name_len;
	char		fr_name[];
} __attribute__((__packed__));

/*
 * RFF_ID request - register FC-4 Features for ID.
 */
struct fc_ns_rff_id {
	struct fc_ns_fid fr_fid;	/* port ID object */
	__u8		fr_resvd[2];
	__u8		fr_feat;	/* FC-4 Feature bits */
	__u8		fr_type;	/* FC-4 type */
} __attribute__((__packed__));

#endif /* _FC_NS_H_ */
