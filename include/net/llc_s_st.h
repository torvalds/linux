/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LLC_S_ST_H
#define LLC_S_ST_H
/*
 * Copyright (c) 1997 by Procom Technology,Inc.
 * 		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 */

#include <linux/types.h>
#include <net/llc_s_ac.h>
#include <net/llc_s_ev.h>

struct llc_sap_state_trans;

#define LLC_NR_SAP_STATES	2       /* size of state table */

/* structures and types */
/* SAP state table structure */
struct llc_sap_state_trans {
	llc_sap_ev_t	  ev;
	u8		  next_state;
	const llc_sap_action_t *ev_actions;
};

struct llc_sap_state {
	u8				 curr_state;
	const struct llc_sap_state_trans **transitions;
};

/* only access to SAP state table */
extern struct llc_sap_state llc_sap_state_table[LLC_NR_SAP_STATES];
#endif /* LLC_S_ST_H */
