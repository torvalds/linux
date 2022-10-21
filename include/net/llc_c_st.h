#ifndef LLC_C_ST_H
#define LLC_C_ST_H
/*
 * Copyright (c) 1997 by Procom Technology,Inc.
 *		2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */

#include <net/llc_c_ac.h>
#include <net/llc_c_ev.h>

/* Connection component state management */
/* connection states */
#define LLC_CONN_OUT_OF_SVC		 0	/* prior to allocation */
 
#define LLC_CONN_STATE_ADM		 1	/* disc, initial state */
#define LLC_CONN_STATE_SETUP		 2	/* disconnected state */
#define LLC_CONN_STATE_NORMAL		 3	/* connected state */
#define LLC_CONN_STATE_BUSY		 4	/* connected state */
#define LLC_CONN_STATE_REJ		 5	/* connected state */
#define LLC_CONN_STATE_AWAIT		 6	/* connected state */
#define LLC_CONN_STATE_AWAIT_BUSY	 7	/* connected state */
#define LLC_CONN_STATE_AWAIT_REJ	 8	/* connected state */
#define LLC_CONN_STATE_D_CONN		 9	/* disconnected state */
#define LLC_CONN_STATE_RESET		10	/* disconnected state */
#define LLC_CONN_STATE_ERROR		11	/* disconnected state */
#define LLC_CONN_STATE_TEMP		12	/* disconnected state */

#define NBR_CONN_STATES			12	/* size of state table */
#define NO_STATE_CHANGE			100

/* Connection state table structure */
struct llc_conn_state_trans {
	llc_conn_ev_t	   ev;
	u8		   next_state;
	const llc_conn_ev_qfyr_t *ev_qualifiers;
	const llc_conn_action_t  *ev_actions;
};

struct llc_conn_state {
	u8			    current_state;
	struct llc_conn_state_trans **transitions;
};

extern struct llc_conn_state llc_conn_state_table[];
#endif /* LLC_C_ST_H */
