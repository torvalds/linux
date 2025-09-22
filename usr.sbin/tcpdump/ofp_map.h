/*	$OpenBSD: ofp_map.h,v 1.1 2016/11/18 17:37:03 reyk Exp $	*/

/*
 * Copyright (c) 2013-2016 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef OFP_MAP_H
#define OFP_MAP_H

struct constmap {
	unsigned int	 cm_type;
	const char	*cm_name;
	const char	*cm_descr;
};

/*
 * Each map is generated from lists of #define's in ofp.h, using the format:
 * #define OFP_{MAPNAME}_FLAG	{value}		/ * COMMENT * /
 *
 * Please make sure that the flags in ofp.h match this style (incl. comment)
 */

/* OpenFlow 1.0 maps */
extern struct constmap ofp10_t_map[];
extern struct constmap ofp10_port_map[];
extern struct constmap ofp10_action_map[];
extern struct constmap ofp10_wildcard_map[];
extern struct constmap ofp10_errtype_map[];
extern struct constmap ofp10_errflowmod_map[];

/* OpenFlow 1.3+ maps */
extern struct constmap ofp_v_map[];
extern struct constmap ofp_t_map[];
extern struct constmap ofp_pktin_map[];
extern struct constmap ofp_port_map[];
extern struct constmap ofp_pktout_map[];
extern struct constmap ofp_oxm_c_map[];
extern struct constmap ofp_xm_t_map[];
extern struct constmap ofp_config_map[];
extern struct constmap ofp_controller_maxlen_map[];
extern struct constmap ofp_instruction_t_map[];
extern struct constmap ofp_portstate_map[];
extern struct constmap ofp_portconfig_map[];
extern struct constmap ofp_portmedia_map[];
extern struct constmap ofp_pktin_reason_map[];
extern struct constmap ofp_swcap_map[];
extern struct constmap ofp_table_id_map[];
extern struct constmap ofp_match_map[];
extern struct constmap ofp_mp_t_map[];
extern struct constmap ofp_action_map[];
extern struct constmap ofp_flowcmd_map[];
extern struct constmap ofp_flowflag_map[];
extern struct constmap ofp_flowrem_reason_map[];
extern struct constmap ofp_group_id_map[];
extern struct constmap ofp_errtype_map[];
extern struct constmap ofp_errflowmod_map[];
extern struct constmap ofp_errmatch_map[];
extern struct constmap ofp_errinst_map[];
extern struct constmap ofp_errreq_map[];
extern struct constmap ofp_table_featprop_map[];

#endif /* OFP_MAP_H */
