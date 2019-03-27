/*-
 * Copyright (C) 2016 Centre for Advanced Internet Architectures,
 *  Swinburne University of Technology, Melbourne, Australia.
 * Portions of this code were made possible in part by a gift from 
 *  The Comcast Innovation Fund.
 * Implemented by Rasool Al-Saadi <ralsaadi@swin.edu.au>
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
 */

/*
 * API for writing an Active Queue Management algorithm for Dummynet
 *
 * $FreeBSD$
 */

#ifndef _IP_DN_AQM_H
#define _IP_DN_AQM_H


/* NOW is the current time in millisecond*/
#define NOW ((dn_cfg.curr_time * tick) / 1000)

#define AQM_UNOW (dn_cfg.curr_time * tick)
#define AQM_TIME_1US ((aqm_time_t)(1))
#define AQM_TIME_1MS ((aqm_time_t)(1000))
#define AQM_TIME_1S ((aqm_time_t)(AQM_TIME_1MS * 1000))

/* aqm time allows to store up to 4294 seconds */
typedef uint32_t aqm_time_t;
typedef int32_t aqm_stime_t;

#define DN_AQM_MTAG_TS 55345

/* Macro for variable bounding */
#define BOUND_VAR(x,l,h)  ((x) > (h)? (h) : ((x) > (l)? (x) : (l)))

/* sysctl variable to count number of dropped packets */
extern unsigned long io_pkt_drop; 

/*
 * Structure for holding data and function pointers that together represent a
 * AQM algorithm.
 */
 struct dn_aqm {
#define DN_AQM_NAME_MAX 50
	char			name[DN_AQM_NAME_MAX];	/* name of AQM algorithm */
	uint32_t	type;	/* AQM type number */
	
	/* Methods implemented by AQM algorithm:
	 * 
	 * enqueue	enqueue packet 'm' on queue 'q'.
	 * 	Return 0 on success, 1 on drop.
	 * 
	 * dequeue	dequeue a packet from queue 'q'.
	 * 	Return a packet, NULL if no packet available.
	 * 
	 * config	configure AQM algorithm
	 * If required, this function should allocate space to store 
	 * the configurations and set 'fs->aqmcfg' to point to this space.
	 * 'dn_extra_parms' includes array of parameters send
	 * from ipfw userland command.
	 * 	Return 0 on success, non-zero otherwise.
	 * 
	 * deconfig	deconfigure AQM algorithm.
	 * The allocated configuration memory space should be freed here.
	 * 	Return 0 on success, non-zero otherwise.
	 * 
	 * init	initialise AQM status variables of queue 'q'
	 * This function is used to allocate space and init AQM status for a
	 * queue and q->aqm_status to point to this space.
	 * 	Return 0 on success, non-zero otherwise.
	 * 
	 * cleanup	cleanup AQM status variables of queue 'q'
	 * The allocated memory space for AQM status should be freed here.
	 * 	Return 0 on success, non-zero otherwise.
	 * 
	 * getconfig	retrieve AQM configurations 
	 * This function is used to return AQM parameters to userland
	 * command. The function should fill 'dn_extra_parms' struct with 
	 * the AQM configurations using 'par' array.
	 * 
	 */
	
	int (*enqueue)(struct dn_queue *, struct mbuf *);
	struct mbuf * (*dequeue)(struct dn_queue *);
	int (*config)(struct dn_fsk *, struct dn_extra_parms *ep, int);
	int (*deconfig)(struct dn_fsk *);
	int (*init)(struct dn_queue *);
	int (*cleanup)(struct dn_queue *);
	int (*getconfig)(struct dn_fsk *, struct dn_extra_parms *);

	int	ref_count; /*Number of queues instances in the system */
	int	cfg_ref_count;	/*Number of AQM instances in the system */
	SLIST_ENTRY (dn_aqm) next; /* Next AQM in the list */
};

/* Helper function to update queue and scheduler statistics.
 * negative len + drop -> drop
 * negative len -> dequeue
 * positive len -> enqueue
 * positive len + drop -> drop during enqueue
 */
__inline static void
update_stats(struct dn_queue *q, int len, int drop)
{
	int inc = 0;
	struct dn_flow *sni;
	struct dn_flow *qni;
	
	sni = &q->_si->ni;
	qni = &q->ni;

	if (len < 0)
			inc = -1;
	else if(len > 0)
			inc = 1;

	if (drop) {
			qni->drops++;
			sni->drops++;
			io_pkt_drop++;
	} else {
		/*update queue stats */
		qni->length += inc;
		qni->len_bytes += len;

		/*update scheduler instance stats */
		sni->length += inc;
		sni->len_bytes += len;
	}
	/* tot_pkts  is updated in dn_enqueue function */
}


/* kernel module related function */
int
dn_aqm_modevent(module_t mod, int cmd, void *arg);

#define DECLARE_DNAQM_MODULE(name, dnaqm)			\
	static moduledata_t name##_mod = {			\
		#name, dn_aqm_modevent, dnaqm		\
	};							\
	DECLARE_MODULE(name, name##_mod, 			\
		SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY); 	\
        MODULE_DEPEND(name, dummynet, 3, 3, 3)

#endif
