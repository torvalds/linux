/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <net/caif/cfctrl.h>
#include <net/caif/cfcnfg.h>
#include <net/caif/caif_dev.h>

int connect_req_to_link_param(struct cfcnfg *cnfg,
				struct caif_connect_request *s,
				struct cfctrl_link_param *l)
{
	struct dev_info *dev_info;
	enum cfcnfg_phy_preference pref;
	int res;

	memset(l, 0, sizeof(*l));
	/* In caif protocol low value is high priority */
	l->priority = CAIF_PRIO_MAX - s->priority + 1;

	if (s->ifindex != 0){
		res = cfcnfg_get_id_from_ifi(cnfg, s->ifindex);
		if (res < 0)
			return res;
		l->phyid = res;
	}
	else {
		switch (s->link_selector) {
		case CAIF_LINK_HIGH_BANDW:
			pref = CFPHYPREF_HIGH_BW;
			break;
		case CAIF_LINK_LOW_LATENCY:
			pref = CFPHYPREF_LOW_LAT;
			break;
		default:
			return -EINVAL;
		}
		dev_info = cfcnfg_get_phyid(cnfg, pref);
		if (dev_info == NULL)
			return -ENODEV;
		l->phyid = dev_info->id;
	}
	switch (s->protocol) {
	case CAIFPROTO_AT:
		l->linktype = CFCTRL_SRV_VEI;
		if (s->sockaddr.u.at.type == CAIF_ATTYPE_PLAIN)
			l->chtype = 0x02;
		else
			l->chtype = s->sockaddr.u.at.type;
		l->endpoint = 0x00;
		break;
	case CAIFPROTO_DATAGRAM:
		l->linktype = CFCTRL_SRV_DATAGRAM;
		l->chtype = 0x00;
		l->u.datagram.connid = s->sockaddr.u.dgm.connection_id;
		break;
	case CAIFPROTO_DATAGRAM_LOOP:
		l->linktype = CFCTRL_SRV_DATAGRAM;
		l->chtype = 0x03;
		l->endpoint = 0x00;
		l->u.datagram.connid = s->sockaddr.u.dgm.connection_id;
		break;
	case CAIFPROTO_RFM:
		l->linktype = CFCTRL_SRV_RFM;
		l->u.datagram.connid = s->sockaddr.u.rfm.connection_id;
		strncpy(l->u.rfm.volume, s->sockaddr.u.rfm.volume,
			sizeof(l->u.rfm.volume)-1);
		l->u.rfm.volume[sizeof(l->u.rfm.volume)-1] = 0;
		break;
	case CAIFPROTO_UTIL:
		l->linktype = CFCTRL_SRV_UTIL;
		l->endpoint = 0x00;
		l->chtype = 0x00;
		strncpy(l->u.utility.name, s->sockaddr.u.util.service,
			sizeof(l->u.utility.name)-1);
		l->u.utility.name[sizeof(l->u.utility.name)-1] = 0;
		caif_assert(sizeof(l->u.utility.name) > 10);
		l->u.utility.paramlen = s->param.size;
		if (l->u.utility.paramlen > sizeof(l->u.utility.params))
			l->u.utility.paramlen = sizeof(l->u.utility.params);

		memcpy(l->u.utility.params, s->param.data,
		       l->u.utility.paramlen);

		break;
	case CAIFPROTO_DEBUG:
		l->linktype = CFCTRL_SRV_DBG;
		l->endpoint = s->sockaddr.u.dbg.service;
		l->chtype = s->sockaddr.u.dbg.type;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
