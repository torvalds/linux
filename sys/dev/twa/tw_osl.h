/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-07 Applied Micro Circuits Corporation.
 * Copyright (c) 2004-05 Vinod Kashyap.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

/*
 * AMCC'S 3ware driver for 9000 series storage controllers.
 *
 * Author: Vinod Kashyap
 * Modifications by: Adam Radford
 * Modifications by: Manjunath Ranganathaiah
 */



#ifndef TW_OSL_H

#define TW_OSL_H


/*
 * OS Layer internal macros, structures and functions.
 */


#define TW_OSLI_DEVICE_NAME		"3ware 9000 series Storage Controller"

#define TW_OSLI_MALLOC_CLASS		M_TWA
#define TW_OSLI_MAX_NUM_REQUESTS	TW_CL_MAX_SIMULTANEOUS_REQUESTS
/* Reserve two command packets.  One for ioctls and one for AENs */
#define TW_OSLI_MAX_NUM_IOS		(TW_OSLI_MAX_NUM_REQUESTS - 2)
#define TW_OSLI_MAX_NUM_AENS		0x100

/* Possible values of req->state. */
#define TW_OSLI_REQ_STATE_INIT		0x0	/* being initialized */
#define TW_OSLI_REQ_STATE_BUSY		0x1	/* submitted to CL */
#define TW_OSLI_REQ_STATE_PENDING	0x2	/* in pending queue */
#define TW_OSLI_REQ_STATE_COMPLETE	0x3	/* completed by CL */

/* Possible values of req->flags. */
#define TW_OSLI_REQ_FLAGS_DATA_IN	(1<<0)	/* read request */
#define TW_OSLI_REQ_FLAGS_DATA_OUT	(1<<1)	/* write request */
#define TW_OSLI_REQ_FLAGS_DATA_COPY_NEEDED (1<<2)/* data in ccb is misaligned,
					have to copy to/from private buffer */
#define TW_OSLI_REQ_FLAGS_MAPPED	(1<<3)	/* request has been mapped */
#define TW_OSLI_REQ_FLAGS_IN_PROGRESS	(1<<4)	/* bus_dmamap_load returned
						EINPROGRESS */
#define TW_OSLI_REQ_FLAGS_PASSTHRU	(1<<5)	/* pass through request */
#define TW_OSLI_REQ_FLAGS_SLEEPING	(1<<6)	/* owner sleeping on this cmd */
#define TW_OSLI_REQ_FLAGS_FAILED	(1<<7)	/* bus_dmamap_load() failed */
#define TW_OSLI_REQ_FLAGS_CCB		(1<<8)	/* req is ccb. */


#ifdef TW_OSL_DEBUG
struct tw_osli_q_stats {
	TW_UINT32	cur_len;	/* current # of items in q */
	TW_UINT32	max_len;	/* max value reached by q_length */
};
#endif /* TW_OSL_DEBUG */


/* Queues of OSL internal request context packets. */
#define TW_OSLI_FREE_Q		0	/* free q */
#define TW_OSLI_BUSY_Q		1	/* q of reqs submitted to CL */
#define TW_OSLI_Q_COUNT		2	/* total number of queues */

/* Driver's request packet. */
struct tw_osli_req_context {
	struct tw_cl_req_handle	req_handle;/* tag to track req b/w OSL & CL */
	struct mtx		ioctl_wake_timeout_lock_handle;/* non-spin lock used to detect ioctl timeout */
	struct mtx		*ioctl_wake_timeout_lock;/* ptr to above lock */
	struct twa_softc	*ctlr;	/* ptr to OSL's controller context */
	TW_VOID			*data;	/* ptr to data being passed to CL */
	TW_UINT32		length;	/* length of buf being passed to CL */
	TW_UINT64		deadline;/* request timeout (in absolute time) */

	/*
	 * ptr to, and length of data passed to us from above, in case a buffer
	 * copy was done due to non-compliance to alignment requirements
	 */
	TW_VOID			*real_data;
	TW_UINT32		real_length;

	TW_UINT32		state;	/* request state */
	TW_UINT32		flags;	/* request flags */

	/* error encountered before request submission to CL */
	TW_UINT32		error_code;

	/* ptr to orig req for use during callback */
	TW_VOID			*orig_req;

	struct tw_cl_link	link;	/* to link this request in a list */
	bus_dmamap_t		dma_map;/* DMA map for data */
	struct tw_cl_req_packet	req_pkt;/* req pkt understood by CL */
};


/* Per-controller structure. */
struct twa_softc {
	struct tw_cl_ctlr_handle	ctlr_handle;
	struct tw_osli_req_context	*req_ctx_buf;

	/* Controller state. */
	TW_UINT8		open;
	TW_UINT32		flags;

	TW_INT32		device_id;
	TW_UINT32		alignment;
	TW_UINT32		sg_size_factor;

	TW_VOID			*non_dma_mem;
	TW_VOID			*dma_mem;
	TW_UINT64		dma_mem_phys;

	/* Request queues and arrays. */
	struct tw_cl_link	req_q_head[TW_OSLI_Q_COUNT];

	struct task		deferred_intr_callback;/* taskqueue function */
	struct mtx		io_lock_handle;/* general purpose lock */
	struct mtx		*io_lock;/* ptr to general purpose lock */
	struct mtx		q_lock_handle;	/* queue manipulation lock */
	struct mtx		*q_lock;/* ptr to queue manipulation lock */
	struct mtx		sim_lock_handle;/* sim lock shared with cam */
	struct mtx		*sim_lock;/* ptr to sim lock */

	struct callout		watchdog_callout[2]; /* For command timeout */
	TW_UINT32		watchdog_index;

#ifdef TW_OSL_DEBUG
	struct tw_osli_q_stats	q_stats[TW_OSLI_Q_COUNT];/* queue statistics */
#endif /* TW_OSL_DEBUG */
    
	device_t		bus_dev;	/* bus device */
	struct cdev		*ctrl_dev;	/* control device */
	struct resource		*reg_res;	/* register interface window */
	TW_INT32		reg_res_id;	/* register resource id */
	bus_space_handle_t	bus_handle;	/* bus space handle */
	bus_space_tag_t		bus_tag;	/* bus space tag */
	bus_dma_tag_t		parent_tag;	/* parent DMA tag */
	bus_dma_tag_t		cmd_tag; /* DMA tag for CL's DMA'able mem */
	bus_dma_tag_t		dma_tag; /* data buffer DMA tag */
	bus_dma_tag_t		ioctl_tag; /* ioctl data buffer DMA tag */
	bus_dmamap_t		cmd_map; /* DMA map for CL's DMA'able mem */
	bus_dmamap_t		ioctl_map; /* DMA map for ioctl data buffers */
	struct resource		*irq_res;	/* interrupt resource */
	TW_INT32		irq_res_id;	/* register resource id */
	TW_VOID			*intr_handle;	/* interrupt handle */

	struct sysctl_ctx_list	sysctl_ctxt;	/* sysctl context */
	struct sysctl_oid	*sysctl_tree;	/* sysctl oid */

	struct cam_sim		*sim;	/* sim for this controller */
	struct cam_path		*path;	/* peripheral, path, tgt, lun
					associated with this controller */
};



/*
 * Queue primitives.
 */

#ifdef TW_OSL_DEBUG

#define TW_OSLI_Q_INIT(sc, q_type)	do {				\
	(sc)->q_stats[q_type].cur_len = 0;				\
	(sc)->q_stats[q_type].max_len = 0;				\
} while(0)


#define TW_OSLI_Q_INSERT(sc, q_type)	do {				\
	struct tw_osli_q_stats *q_stats = &((sc)->q_stats[q_type]);	\
									\
	if (++(q_stats->cur_len) > q_stats->max_len)			\
		q_stats->max_len = q_stats->cur_len;			\
} while(0)


#define TW_OSLI_Q_REMOVE(sc, q_type)					\
	(sc)->q_stats[q_type].cur_len--


#else /* TW_OSL_DEBUG */

#define TW_OSLI_Q_INIT(sc, q_index)
#define TW_OSLI_Q_INSERT(sc, q_index)
#define TW_OSLI_Q_REMOVE(sc, q_index)

#endif /* TW_OSL_DEBUG */



/* Initialize a queue of requests. */
static __inline	TW_VOID
tw_osli_req_q_init(struct twa_softc *sc, TW_UINT8 q_type)
{
	TW_CL_Q_INIT(&(sc->req_q_head[q_type]));
	TW_OSLI_Q_INIT(sc, q_type);
}



/* Insert the given request at the head of the given queue (q_type). */
static __inline	TW_VOID
tw_osli_req_q_insert_head(struct tw_osli_req_context *req, TW_UINT8 q_type)
{
	mtx_lock_spin(req->ctlr->q_lock);
	TW_CL_Q_INSERT_HEAD(&(req->ctlr->req_q_head[q_type]), &(req->link));
	TW_OSLI_Q_INSERT(req->ctlr, q_type);
	mtx_unlock_spin(req->ctlr->q_lock);
}



/* Insert the given request at the tail of the given queue (q_type). */
static __inline	TW_VOID
tw_osli_req_q_insert_tail(struct tw_osli_req_context *req, TW_UINT8 q_type)
{
	mtx_lock_spin(req->ctlr->q_lock);
	TW_CL_Q_INSERT_TAIL(&(req->ctlr->req_q_head[q_type]), &(req->link));
	TW_OSLI_Q_INSERT(req->ctlr, q_type);
	mtx_unlock_spin(req->ctlr->q_lock);
}



/* Remove and return the request at the head of the given queue (q_type). */
static __inline struct tw_osli_req_context *
tw_osli_req_q_remove_head(struct twa_softc *sc, TW_UINT8 q_type)
{
	struct tw_osli_req_context	*req = NULL;
	struct tw_cl_link		*link;

	mtx_lock_spin(sc->q_lock);
	if ((link = TW_CL_Q_FIRST_ITEM(&(sc->req_q_head[q_type]))) !=
		TW_CL_NULL) {
		req = TW_CL_STRUCT_HEAD(link,
			struct tw_osli_req_context, link);
		TW_CL_Q_REMOVE_ITEM(&(sc->req_q_head[q_type]), &(req->link));
		TW_OSLI_Q_REMOVE(sc, q_type);
	}
	mtx_unlock_spin(sc->q_lock);
	return(req);
}



/* Remove the given request from the given queue (q_type). */
static __inline TW_VOID
tw_osli_req_q_remove_item(struct tw_osli_req_context *req, TW_UINT8 q_type)
{
	mtx_lock_spin(req->ctlr->q_lock);
	TW_CL_Q_REMOVE_ITEM(&(req->ctlr->req_q_head[q_type]), &(req->link));
	TW_OSLI_Q_REMOVE(req->ctlr, q_type);
	mtx_unlock_spin(req->ctlr->q_lock);
}



#ifdef TW_OSL_DEBUG

extern TW_INT32	TW_DEBUG_LEVEL_FOR_OSL;

#define tw_osli_dbg_dprintf(dbg_level, sc, fmt, args...)		\
	if (dbg_level <= TW_DEBUG_LEVEL_FOR_OSL)			\
		device_printf(sc->bus_dev, "%s: " fmt "\n",		\
			__func__, ##args)


#define tw_osli_dbg_printf(dbg_level, fmt, args...)			\
	if (dbg_level <= TW_DEBUG_LEVEL_FOR_OSL)			\
		printf("%s: " fmt "\n",	__func__, ##args)

#else /* TW_OSL_DEBUG */

#define tw_osli_dbg_dprintf(dbg_level, sc, fmt, args...)
#define tw_osli_dbg_printf(dbg_level, fmt, args...)

#endif /* TW_OSL_DEBUG */


/* For regular printing. */
#define twa_printf(sc, fmt, args...)					\
	device_printf(((struct twa_softc *)(sc))->bus_dev, fmt, ##args)

/* For printing in the "consistent error reporting" format. */
#define tw_osli_printf(sc, err_specific_desc, args...)			\
	device_printf((sc)->bus_dev,					\
		"%s: (0x%02X: 0x%04X): %s: " err_specific_desc "\n", ##args)



#endif /* TW_OSL_H */
