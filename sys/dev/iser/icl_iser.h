/* $FreeBSD$ */
/*-
 * Copyright (c) 2015, Mellanox Technologies, Inc. All rights reserved.
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
 */

#ifndef ICL_ISER_H
#define ICL_ISER_H

/*
 * iSCSI Common Layer for RDMA.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/sx.h>
#include <sys/uio.h>
#include <sys/taskqueue.h>
#include <sys/bio.h>
#include <vm/uma.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <dev/iscsi/icl.h>
#include <dev/iscsi/iscsi_proto.h>
#include <icl_conn_if.h>
#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_fmr_pool.h>
#include <rdma/rdma_cm.h>


#define	ISER_DBG(X, ...)						\
	do {								\
		if (unlikely(iser_debug > 2))				\
			printf("DEBUG: %s: " X "\n",			\
				__func__, ## __VA_ARGS__);		\
	} while (0)

#define	ISER_INFO(X, ...)						\
	do {								\
		if (unlikely(iser_debug > 1))				\
			printf("INFO: %s: " X "\n",			\
				__func__, ## __VA_ARGS__);		\
	} while (0)

#define	ISER_WARN(X, ...)						\
	do {								\
		if (unlikely(iser_debug > 0)) {				\
			printf("WARNING: %s: " X "\n",			\
				__func__, ## __VA_ARGS__);		\
		}							\
	} while (0)

#define	ISER_ERR(X, ...) 						\
	printf("ERROR: %s: " X "\n", __func__, ## __VA_ARGS__)

#define ISER_VER			0x10
#define ISER_WSV			0x08
#define ISER_RSV			0x04

#define ISER_FASTREG_LI_WRID		0xffffffffffffffffULL
#define ISER_BEACON_WRID		0xfffffffffffffffeULL

#define SHIFT_4K	12
#define SIZE_4K	(1ULL << SHIFT_4K)
#define MASK_4K	(~(SIZE_4K-1))

/* support up to 512KB in one RDMA */
#define ISCSI_ISER_SG_TABLESIZE         (0x80000 >> SHIFT_4K)
#define ISER_DEF_XMIT_CMDS_MAX 256

/* the max RX (recv) WR supported by the iSER QP is defined by                 *
 * max_recv_wr = commands_max + recv_beacon                                    */
#define ISER_QP_MAX_RECV_DTOS  (ISER_DEF_XMIT_CMDS_MAX + 1)
#define ISER_MIN_POSTED_RX		(ISER_DEF_XMIT_CMDS_MAX >> 2)

/* QP settings */
/* Maximal bounds on received asynchronous PDUs */
#define ISER_MAX_RX_MISC_PDUS           4 /* NOOP_IN(2) , ASYNC_EVENT(2)   */
#define ISER_MAX_TX_MISC_PDUS           6 /* NOOP_OUT(2), TEXT(1), SCSI_TMFUNC(2), LOGOUT(1) */

/* the max TX (send) WR supported by the iSER QP is defined by                 *
 * max_send_wr = T * (1 + D) + C ; D is how many inflight dataouts we expect   *
 * to have at max for SCSI command. The tx posting & completion handling code  *
 * supports -EAGAIN scheme where tx is suspended till the QP has room for more *
 * send WR. D=8 comes from 64K/8K                                              */

#define ISER_INFLIGHT_DATAOUTS		8

/* the send_beacon increase the max_send_wr by 1  */
#define ISER_QP_MAX_REQ_DTOS		(ISER_DEF_XMIT_CMDS_MAX *    \
					(1 + ISER_INFLIGHT_DATAOUTS) + \
					ISER_MAX_TX_MISC_PDUS        + \
					ISER_MAX_RX_MISC_PDUS + 1)

#define ISER_GET_MAX_XMIT_CMDS(send_wr) ((send_wr			\
					 - ISER_MAX_TX_MISC_PDUS	\
					 - ISER_MAX_RX_MISC_PDUS - 1) /	\
					 (1 + ISER_INFLIGHT_DATAOUTS))

#define ISER_WC_BATCH_COUNT   16
#define ISER_SIGNAL_CMD_COUNT 32

/* Maximal QP's recommended per CQ. In case we use more QP's per CQ we might   *
 * encounter a CQ overrun state.                                               */
#define ISCSI_ISER_MAX_CONN	8
#define ISER_MAX_RX_LEN		(ISER_QP_MAX_RECV_DTOS * ISCSI_ISER_MAX_CONN)
#define ISER_MAX_TX_LEN		(ISER_QP_MAX_REQ_DTOS  * ISCSI_ISER_MAX_CONN)
#define ISER_MAX_CQ_LEN		(ISER_MAX_RX_LEN + ISER_MAX_TX_LEN + \
				 ISCSI_ISER_MAX_CONN)

#define ISER_ZBVA_NOT_SUPPORTED                0x80
#define ISER_SEND_W_INV_NOT_SUPPORTED	0x40

#define	ISCSI_DEF_MAX_RECV_SEG_LEN	8192
#define	ISCSI_OPCODE_MASK		0x3f

#define icl_to_iser_conn(ic) \
	container_of(ic, struct iser_conn, icl_conn)
#define icl_to_iser_pdu(ip) \
	container_of(ip, struct icl_iser_pdu, icl_pdu)

/**
 * struct iser_hdr - iSER header
 *
 * @flags:        flags support (zbva, remote_inv)
 * @rsvd:         reserved
 * @write_stag:   write rkey
 * @write_va:     write virtual address
 * @reaf_stag:    read rkey
 * @read_va:      read virtual address
 */
struct iser_hdr {
	u8      flags;
	u8      rsvd[3];
	__be32  write_stag;
	__be64  write_va;
	__be32  read_stag;
	__be64  read_va;
} __attribute__((packed));

struct iser_cm_hdr {
	u8      flags;
	u8      rsvd[3];
} __packed;

/* Constant PDU lengths calculations */
#define ISER_HEADERS_LEN  (sizeof(struct iser_hdr) + ISCSI_BHS_SIZE)

#define ISER_RECV_DATA_SEG_LEN	128
#define ISER_RX_PAYLOAD_SIZE	(ISER_HEADERS_LEN + ISER_RECV_DATA_SEG_LEN)

#define ISER_RX_LOGIN_SIZE	(ISER_HEADERS_LEN + ISCSI_DEF_MAX_RECV_SEG_LEN)

enum iser_conn_state {
	ISER_CONN_INIT,		   /* descriptor allocd, no conn          */
	ISER_CONN_PENDING,	   /* in the process of being established */
	ISER_CONN_UP,		   /* up and running                      */
	ISER_CONN_TERMINATING,	   /* in the process of being terminated  */
	ISER_CONN_DOWN,		   /* shut down                           */
	ISER_CONN_STATES_NUM
};

enum iser_task_status {
	ISER_TASK_STATUS_INIT = 0,
	ISER_TASK_STATUS_STARTED,
	ISER_TASK_STATUS_COMPLETED
};

enum iser_data_dir {
	ISER_DIR_IN = 0,	   /* to initiator */
	ISER_DIR_OUT,		   /* from initiator */
	ISER_DIRS_NUM
};

/**
 * struct iser_mem_reg - iSER memory registration info
 *
 * @sge:          memory region sg element
 * @rkey:         memory region remote key
 * @mem_h:        pointer to registration context (FMR/Fastreg)
 */
struct iser_mem_reg {
	struct ib_sge	 sge;
	u32		 rkey;
	void		*mem_h;
};

enum iser_desc_type {
	ISCSI_TX_CONTROL ,
	ISCSI_TX_SCSI_COMMAND,
	ISCSI_TX_DATAOUT
};

/**
 * struct iser_data_buf - iSER data buffer
 *
 * @sg:           pointer to the sg list
 * @size:         num entries of this sg
 * @data_len:     total beffer byte len
 * @dma_nents:    returned by dma_map_sg
 * @copy_buf:     allocated copy buf for SGs unaligned
 *                for rdma which are copied
 * @orig_sg:      pointer to the original sg list (in case
 *                we used a copy)
 * @sg_single:    SG-ified clone of a non SG SC or
 *                unaligned SG
 */
struct iser_data_buf {
	struct scatterlist sgl[ISCSI_ISER_SG_TABLESIZE];
	void               *sg;
	int                size;
	unsigned long      data_len;
	unsigned int       dma_nents;
	char               *copy_buf;
	struct scatterlist *orig_sg;
	struct scatterlist sg_single;
  };

/* fwd declarations */
struct iser_conn;
struct ib_conn;
struct iser_device;

/**
 * struct iser_tx_desc - iSER TX descriptor (for send wr_id)
 *
 * @iser_header:   iser header
 * @iscsi_header:  iscsi header (bhs)
 * @type:          command/control/dataout
 * @dma_addr:      header buffer dma_address
 * @tx_sg:         sg[0] points to iser/iscsi headers
 *                 sg[1] optionally points to either of immediate data
 *                 unsolicited data-out or control
 * @num_sge:       number sges used on this TX task
 * @mapped:        indicates if the descriptor is dma mapped
 */
struct iser_tx_desc {
	struct iser_hdr              iser_header;
	struct iscsi_bhs             iscsi_header __attribute__((packed));
	enum   iser_desc_type        type;
	u64		             dma_addr;
	struct ib_sge		     tx_sg[2];
	int                          num_sge;
	bool                         mapped;
};

#define ISER_RX_PAD_SIZE	(256 - (ISER_RX_PAYLOAD_SIZE + \
					sizeof(u64) + sizeof(struct ib_sge)))
/**
 * struct iser_rx_desc - iSER RX descriptor (for recv wr_id)
 *
 * @iser_header:   iser header
 * @iscsi_header:  iscsi header
 * @data:          received data segment
 * @dma_addr:      receive buffer dma address
 * @rx_sg:         ib_sge of receive buffer
 * @pad:           for sense data TODO: Modify to maximum sense length supported
 */
struct iser_rx_desc {
	struct iser_hdr              iser_header;
	struct iscsi_bhs             iscsi_header;
	char		             data[ISER_RECV_DATA_SEG_LEN];
	u64		             dma_addr;
	struct ib_sge		     rx_sg;
	char		             pad[ISER_RX_PAD_SIZE];
} __attribute__((packed));

struct icl_iser_pdu {
	struct icl_pdu               icl_pdu;
	struct iser_tx_desc          desc;
	struct iser_conn             *iser_conn;
	enum iser_task_status        status;
	struct ccb_scsiio 			 *csio;
	int                          command_sent;
	int                          dir[ISER_DIRS_NUM];
	struct iser_mem_reg          rdma_reg[ISER_DIRS_NUM];
	struct iser_data_buf         data[ISER_DIRS_NUM];
};

/**
 * struct iser_comp - iSER completion context
 *
 * @device:     pointer to device handle
 * @cq:         completion queue
 * @wcs:        work completion array
 * @tq:    	taskqueue handle
 * @task:    	task to run task_fn
 * @active_qps: Number of active QPs attached
 *              to completion context
 */
struct iser_comp {
	struct iser_device      *device;
	struct ib_cq		*cq;
	struct ib_wc		 wcs[ISER_WC_BATCH_COUNT];
	struct taskqueue        *tq;
	struct task             task;
	int                      active_qps;
};

/**
 * struct iser_device - iSER device handle
 *
 * @ib_device:     RDMA device
 * @pd:            Protection Domain for this device
 * @dev_attr:      Device attributes container
 * @mr:            Global DMA memory region
 * @event_handler: IB events handle routine
 * @ig_list:	   entry in devices list
 * @refcount:      Reference counter, dominated by open iser connections
 * @comps_used:    Number of completion contexts used, Min between online
 *                 cpus and device max completion vectors
 * @comps:         Dinamically allocated array of completion handlers
 */
struct iser_device {
	struct ib_device             *ib_device;
	struct ib_pd	             *pd;
	struct ib_device_attr	     dev_attr;
	struct ib_mr	             *mr;
	struct ib_event_handler      event_handler;
	struct list_head             ig_list;
	int                          refcount;
	int			     comps_used;
	struct iser_comp	     *comps;
};

/**
 * struct iser_reg_resources - Fast registration recources
 *
 * @mr:         memory region
 * @mr_valid:   is mr valid indicator
 */
struct iser_reg_resources {
	struct ib_mr                     *mr;
	u8                                mr_valid:1;
};

/**
 * struct fast_reg_descriptor - Fast registration descriptor
 *
 * @list:           entry in connection fastreg pool
 * @rsc:            data buffer registration resources
 */
struct fast_reg_descriptor {
	struct list_head		  list;
	struct iser_reg_resources	  rsc;
};


/**
 * struct iser_beacon - beacon to signal all flush errors were drained
 *
 * @send:           send wr
 * @recv:           recv wr
 * @flush_lock:     protects flush_cv
 * @flush_cv:       condition variable for beacon flush
 */
struct iser_beacon {
	union {
		struct ib_send_wr	send;
		struct ib_recv_wr	recv;
	};
	struct mtx		     flush_lock;
	struct cv		     flush_cv;
};

/**
 * struct ib_conn - Infiniband related objects
 *
 * @cma_id:              rdma_cm connection maneger handle
 * @qp:                  Connection Queue-pair
 * @device:              reference to iser device
 * @comp:                iser completion context
  */
struct ib_conn {
	struct rdma_cm_id           *cma_id;
	struct ib_qp	            *qp;
	int                          post_recv_buf_count;
	u8                           sig_count;
	struct ib_recv_wr	     rx_wr[ISER_MIN_POSTED_RX];
	struct iser_device          *device;
	struct iser_comp	    *comp;
	struct iser_beacon	     beacon;
	struct mtx               lock;
	union {
		struct {
			struct ib_fmr_pool      *pool;
			struct iser_page_vec	*page_vec;
		} fmr;
		struct {
			struct list_head	 pool;
			int			 pool_size;
		} fastreg;
	};
};

struct iser_conn {
	struct icl_conn             icl_conn;
	struct ib_conn               ib_conn;
	struct cv                    up_cv;
	struct list_head             conn_list;
	struct sx		     		 state_mutex;
	enum iser_conn_state	     state;
	int		     				 qp_max_recv_dtos;
	int		     				 min_posted_rx;
	u16                          max_cmds;
	char  			     *login_buf;
	char			     *login_req_buf, *login_resp_buf;
	u64			     login_req_dma, login_resp_dma;
	unsigned int 		     rx_desc_head;
	struct iser_rx_desc	     *rx_descs;
	u32                          num_rx_descs;
	bool                         handoff_done;
};

/**
 * struct iser_global: iSER global context
 *
 * @device_list_mutex:    protects device_list
 * @device_list:          iser devices global list
 * @connlist_mutex:       protects connlist
 * @connlist:             iser connections global list
 * @desc_cache:           kmem cache for tx dataout
 * @close_conns_mutex:    serializes conns closure
 */
struct iser_global {
	struct sx        device_list_mutex;
	struct list_head  device_list;
	struct mtx        connlist_mutex;
	struct list_head  connlist;
	struct sx         close_conns_mutex;
};

extern struct iser_global ig;
extern int iser_debug;

void
iser_create_send_desc(struct iser_conn *, struct iser_tx_desc *);

int
iser_post_recvl(struct iser_conn *);

int
iser_post_recvm(struct iser_conn *, int);

int
iser_alloc_login_buf(struct iser_conn *iser_conn);

void
iser_free_login_buf(struct iser_conn *iser_conn);

int
iser_post_send(struct ib_conn *, struct iser_tx_desc *, bool);

void
iser_snd_completion(struct iser_tx_desc *, struct ib_conn *);

void
iser_rcv_completion(struct iser_rx_desc *, unsigned long,
		    struct ib_conn *);

void
iser_pdu_free(struct icl_conn *, struct icl_pdu *);

struct icl_pdu *
iser_new_pdu(struct icl_conn *ic, int flags);

int
iser_alloc_rx_descriptors(struct iser_conn *, int);

void
iser_free_rx_descriptors(struct iser_conn *);

int
iser_initialize_headers(struct icl_iser_pdu *, struct iser_conn *);

int
iser_send_control(struct iser_conn *, struct icl_iser_pdu *);

int
iser_send_command(struct iser_conn *, struct icl_iser_pdu *);

int
iser_reg_rdma_mem(struct icl_iser_pdu *, enum iser_data_dir);

void
iser_unreg_rdma_mem(struct icl_iser_pdu *, enum iser_data_dir);

int
iser_create_fastreg_pool(struct ib_conn *, unsigned);

void
iser_free_fastreg_pool(struct ib_conn *);

int
iser_dma_map_task_data(struct icl_iser_pdu *,
		       struct iser_data_buf *, enum iser_data_dir,
		       enum dma_data_direction);

int
iser_conn_terminate(struct iser_conn *);

void
iser_free_ib_conn_res(struct iser_conn *, bool);

void
iser_dma_unmap_task_data(struct icl_iser_pdu *, struct iser_data_buf *,
			 enum dma_data_direction);

int
iser_cma_handler(struct rdma_cm_id *, struct rdma_cm_event *);

#endif /* !ICL_ISER_H */
