/*
 * Copyright(c) 2008 Intel Corporation. All rights reserved.
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

#ifndef _FC_ENCODE_H_
#define _FC_ENCODE_H_
#include <asm/unaligned.h>

struct fc_ns_rft {
	struct fc_ns_fid fid;	/* port ID object */
	struct fc_ns_fts fts;	/* FC4-types object */
};

struct fc_ct_req {
	struct fc_ct_hdr hdr;
	union {
		struct fc_ns_gid_ft gid;
		struct fc_ns_rn_id  rn;
		struct fc_ns_rft rft;
	} payload;
};

/**
 * fill FC header fields in specified fc_frame
 */
static inline void fc_fill_fc_hdr(struct fc_frame *fp, enum fc_rctl r_ctl,
				  u32 did, u32 sid, enum fc_fh_type type,
				  u32 f_ctl, u32 parm_offset)
{
	struct fc_frame_header *fh;

	fh = fc_frame_header_get(fp);
	WARN_ON(r_ctl == 0);
	fh->fh_r_ctl = r_ctl;
	hton24(fh->fh_d_id, did);
	hton24(fh->fh_s_id, sid);
	fh->fh_type = type;
	hton24(fh->fh_f_ctl, f_ctl);
	fh->fh_cs_ctl = 0;
	fh->fh_df_ctl = 0;
	fh->fh_parm_offset = htonl(parm_offset);
}

/**
 * fc_ct_hdr_fill- fills ct header and reset ct payload
 * returns pointer to ct request.
 */
static inline struct fc_ct_req *fc_ct_hdr_fill(const struct fc_frame *fp,
					       unsigned int op, size_t req_size)
{
	struct fc_ct_req *ct;
	size_t ct_plen;

	ct_plen  = sizeof(struct fc_ct_hdr) + req_size;
	ct = fc_frame_payload_get(fp, ct_plen);
	memset(ct, 0, ct_plen);
	ct->hdr.ct_rev = FC_CT_REV;
	ct->hdr.ct_fs_type = FC_FST_DIR;
	ct->hdr.ct_fs_subtype = FC_NS_SUBTYPE;
	ct->hdr.ct_cmd = htons((u16) op);
	return ct;
}

/**
 * fc_ct_fill - Fill in a name service request frame
 */
static inline int fc_ct_fill(struct fc_lport *lport, struct fc_frame *fp,
		      unsigned int op, enum fc_rctl *r_ctl, u32 *did,
		      enum fc_fh_type *fh_type)
{
	struct fc_ct_req *ct;

	switch (op) {
	case FC_NS_GPN_FT:
		ct = fc_ct_hdr_fill(fp, op, sizeof(struct fc_ns_gid_ft));
		ct->payload.gid.fn_fc4_type = FC_TYPE_FCP;
		break;

	case FC_NS_RFT_ID:
		ct = fc_ct_hdr_fill(fp, op, sizeof(struct fc_ns_rft));
		hton24(ct->payload.rft.fid.fp_fid,
		       fc_host_port_id(lport->host));
		ct->payload.rft.fts = lport->fcts;
		break;

	case FC_NS_RPN_ID:
		ct = fc_ct_hdr_fill(fp, op, sizeof(struct fc_ns_rn_id));
		hton24(ct->payload.rn.fr_fid.fp_fid,
		       fc_host_port_id(lport->host));
		ct->payload.rft.fts = lport->fcts;
		put_unaligned_be64(lport->wwpn, &ct->payload.rn.fr_wwn);
		break;

	default:
		return -EINVAL;
	}
	*r_ctl = FC_RCTL_DD_UNSOL_CTL;
	*did = FC_FID_DIR_SERV;
	*fh_type = FC_TYPE_CT;
	return 0;
}

/**
 * fc_plogi_fill - Fill in plogi request frame
 */
static inline void fc_plogi_fill(struct fc_lport *lport, struct fc_frame *fp,
				 unsigned int op)
{
	struct fc_els_flogi *plogi;
	struct fc_els_csp *csp;
	struct fc_els_cssp *cp;

	plogi = fc_frame_payload_get(fp, sizeof(*plogi));
	memset(plogi, 0, sizeof(*plogi));
	plogi->fl_cmd = (u8) op;
	put_unaligned_be64(lport->wwpn, &plogi->fl_wwpn);
	put_unaligned_be64(lport->wwnn, &plogi->fl_wwnn);

	csp = &plogi->fl_csp;
	csp->sp_hi_ver = 0x20;
	csp->sp_lo_ver = 0x20;
	csp->sp_bb_cred = htons(10);	/* this gets set by gateway */
	csp->sp_bb_data = htons((u16) lport->mfs);
	cp = &plogi->fl_cssp[3 - 1];	/* class 3 parameters */
	cp->cp_class = htons(FC_CPC_VALID | FC_CPC_SEQ);
	csp->sp_features = htons(FC_SP_FT_CIRO);
	csp->sp_tot_seq = htons(255);	/* seq. we accept */
	csp->sp_rel_off = htons(0x1f);
	csp->sp_e_d_tov = htonl(lport->e_d_tov);

	cp->cp_rdfs = htons((u16) lport->mfs);
	cp->cp_con_seq = htons(255);
	cp->cp_open_seq = 1;
}

/**
 * fc_flogi_fill - Fill in a flogi request frame.
 */
static inline void fc_flogi_fill(struct fc_lport *lport, struct fc_frame *fp)
{
	struct fc_els_csp *sp;
	struct fc_els_cssp *cp;
	struct fc_els_flogi *flogi;

	flogi = fc_frame_payload_get(fp, sizeof(*flogi));
	memset(flogi, 0, sizeof(*flogi));
	flogi->fl_cmd = (u8) ELS_FLOGI;
	put_unaligned_be64(lport->wwpn, &flogi->fl_wwpn);
	put_unaligned_be64(lport->wwnn, &flogi->fl_wwnn);
	sp = &flogi->fl_csp;
	sp->sp_hi_ver = 0x20;
	sp->sp_lo_ver = 0x20;
	sp->sp_bb_cred = htons(10);	/* this gets set by gateway */
	sp->sp_bb_data = htons((u16) lport->mfs);
	cp = &flogi->fl_cssp[3 - 1];	/* class 3 parameters */
	cp->cp_class = htons(FC_CPC_VALID | FC_CPC_SEQ);
}

/**
 * fc_logo_fill - Fill in a logo request frame.
 */
static inline void fc_logo_fill(struct fc_lport *lport, struct fc_frame *fp)
{
	struct fc_els_logo *logo;

	logo = fc_frame_payload_get(fp, sizeof(*logo));
	memset(logo, 0, sizeof(*logo));
	logo->fl_cmd = ELS_LOGO;
	hton24(logo->fl_n_port_id, fc_host_port_id(lport->host));
	logo->fl_n_port_wwn = htonll(lport->wwpn);
}

/**
 * fc_rtv_fill - Fill in RTV (read timeout value) request frame.
 */
static inline void fc_rtv_fill(struct fc_lport *lport, struct fc_frame *fp)
{
	struct fc_els_rtv *rtv;

	rtv = fc_frame_payload_get(fp, sizeof(*rtv));
	memset(rtv, 0, sizeof(*rtv));
	rtv->rtv_cmd = ELS_RTV;
}

/**
 * fc_rec_fill - Fill in rec request frame
 */
static inline void fc_rec_fill(struct fc_lport *lport, struct fc_frame *fp)
{
	struct fc_els_rec *rec;
	struct fc_exch *ep = fc_seq_exch(fr_seq(fp));

	rec = fc_frame_payload_get(fp, sizeof(*rec));
	memset(rec, 0, sizeof(*rec));
	rec->rec_cmd = ELS_REC;
	hton24(rec->rec_s_id, fc_host_port_id(lport->host));
	rec->rec_ox_id = htons(ep->oxid);
	rec->rec_rx_id = htons(ep->rxid);
}

/**
 * fc_prli_fill - Fill in prli request frame
 */
static inline void fc_prli_fill(struct fc_lport *lport, struct fc_frame *fp)
{
	struct {
		struct fc_els_prli prli;
		struct fc_els_spp spp;
	} *pp;

	pp = fc_frame_payload_get(fp, sizeof(*pp));
	memset(pp, 0, sizeof(*pp));
	pp->prli.prli_cmd = ELS_PRLI;
	pp->prli.prli_spp_len = sizeof(struct fc_els_spp);
	pp->prli.prli_len = htons(sizeof(*pp));
	pp->spp.spp_type = FC_TYPE_FCP;
	pp->spp.spp_flags = FC_SPP_EST_IMG_PAIR;
	pp->spp.spp_params = htonl(lport->service_params);
}

/**
 * fc_scr_fill - Fill in a scr request frame.
 */
static inline void fc_scr_fill(struct fc_lport *lport, struct fc_frame *fp)
{
	struct fc_els_scr *scr;

	scr = fc_frame_payload_get(fp, sizeof(*scr));
	memset(scr, 0, sizeof(*scr));
	scr->scr_cmd = ELS_SCR;
	scr->scr_reg_func = ELS_SCRF_FULL;
}

/**
 * fc_els_fill - Fill in an ELS  request frame
 */
static inline int fc_els_fill(struct fc_lport *lport, struct fc_rport *rport,
		       struct fc_frame *fp, unsigned int op,
		       enum fc_rctl *r_ctl, u32 *did, enum fc_fh_type *fh_type)
{
	switch (op) {
	case ELS_PLOGI:
		fc_plogi_fill(lport, fp, ELS_PLOGI);
		*did = rport->port_id;
		break;

	case ELS_FLOGI:
		fc_flogi_fill(lport, fp);
		*did = FC_FID_FLOGI;
		break;

	case ELS_LOGO:
		fc_logo_fill(lport, fp);
		*did = FC_FID_FLOGI;
		/*
		 * if rport is valid then it
		 * is port logo, therefore
		 * set did to rport id.
		 */
		if (rport)
			*did = rport->port_id;
		break;

	case ELS_RTV:
		fc_rtv_fill(lport, fp);
		*did = rport->port_id;
		break;

	case ELS_REC:
		fc_rec_fill(lport, fp);
		*did = rport->port_id;
		break;

	case ELS_PRLI:
		fc_prli_fill(lport, fp);
		*did = rport->port_id;
		break;

	case ELS_SCR:
		fc_scr_fill(lport, fp);
		*did = FC_FID_FCTRL;
		break;

	default:
		return -EINVAL;
	}

	*r_ctl = FC_RCTL_ELS_REQ;
	*fh_type = FC_TYPE_ELS;
	return 0;
}
#endif /* _FC_ENCODE_H_ */
