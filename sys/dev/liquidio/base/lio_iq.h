/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

/*   \file  lio_iq.h
 *   \brief Host Driver: Implementation of Octeon input queues. "Input" is
 *   with respect to the Octeon device on the NIC. From this driver's
 *   point of view they are egress queues.
 */

#ifndef __LIO_IQ_H__
#define __LIO_IQ_H__

#define LIO_IQ_SEND_OK          0
#define LIO_IQ_SEND_STOP        1
#define LIO_IQ_SEND_FAILED     -1

/*-------------------------  INSTRUCTION QUEUE --------------------------*/

#define LIO_REQTYPE_NONE                 0
#define LIO_REQTYPE_NORESP_NET           1
#define LIO_REQTYPE_NORESP_NET_SG        2
#define LIO_REQTYPE_RESP_NET             3
#define LIO_REQTYPE_SOFT_COMMAND         4

/*
 * This structure is used by NIC driver to store information required
 * to free the mbuf when the packet has been fetched by Octeon.
 * Bytes offset below assume worst-case of a 64-bit system.
 */
struct lio_mbuf_free_info {
	/* Pointer to mbuf. */
	struct mbuf		*mb;

	/* Pointer to gather list. */
	struct lio_gather	*g;

	bus_dmamap_t		map;
};

struct lio_request_list {
	uint32_t			reqtype;
	void				*buf;
	bus_dmamap_t			map;
	struct lio_mbuf_free_info	finfo;
};

/* Input Queue statistics. Each input queue has four stats fields. */
struct lio_iq_stats {
	uint64_t	instr_posted;		/**< Instructions posted to this queue. */
	uint64_t	instr_processed;	/**< Instructions processed in this queue. */
	uint64_t	instr_dropped;		/**< Instructions that could not be processed */
	uint64_t	bytes_sent;		/**< Bytes sent through this queue. */
	uint64_t	sgentry_sent;		/**< Gather entries sent through this queue. */
	uint64_t	tx_done;		/**< Num of packets sent to network. */
	uint64_t	tx_iq_busy;		/**< Numof times this iq was found to be full. */
	uint64_t	tx_dropped;		/**< Numof pkts dropped dueto xmitpath errors. */
	uint64_t	tx_tot_bytes;		/**< Total count of bytes sento to network. */
	uint64_t	tx_gso;			/* count of tso */
	uint64_t	tx_vxlan;		/* tunnel */
	uint64_t	tx_dmamap_fail;
	uint64_t	tx_restart;
	uint64_t	mbuf_defrag_failed;
};

/*
 *  The instruction (input) queue.
 *  The input queue is used to post raw (instruction) mode data or packet
 *  data to Octeon device from the host. Each input queue for
 *  a Octeon device has one such structure to represent it.
 */
struct lio_instr_queue {
	struct octeon_device	*oct_dev;

	/* A lock to protect access to the input ring.  */
	struct mtx		lock;

	/* A lock to protect while enqueue to the input ring.  */
	struct mtx		enq_lock;

	/* A lock to protect while posting on the ring.  */
	struct mtx		post_lock;

	uint32_t		pkt_in_done;

	/* A lock to protect access to the input ring. */
	struct mtx		iq_flush_running_lock;

	/* Flag that indicates if the queue uses 64 byte commands. */
	uint32_t		iqcmd_64B:1;

	/* Queue info. */
	union octeon_txpciq	txpciq;

	uint32_t		rsvd:17;

	uint32_t		status:8;

	/* Maximum no. of instructions in this queue. */
	uint32_t		max_count;

	/* Index in input ring where the driver should write the next packet */
	uint32_t		host_write_index;

	/*
	 * Index in input ring where Octeon is expected to read the next
	 * packet.
	 */
	uint32_t		octeon_read_index;

	/*
	 * This index aids in finding the window in the queue where Octeon
	 * has read the commands.
	 */
	uint32_t		flush_index;

	/* This field keeps track of the instructions pending in this queue. */
	volatile int		instr_pending;

	uint32_t		reset_instr_cnt;

	/* Pointer to the Virtual Base addr of the input ring. */
	uint8_t			*base_addr;
	bus_dma_tag_t		txtag;

	struct lio_request_list	*request_list;

	struct buf_ring		*br;

	/* Octeon doorbell register for the ring. */
	uint32_t		doorbell_reg;

	/* Octeon instruction count register for this ring. */
	uint32_t		inst_cnt_reg;

	/* Number of instructions pending to be posted to Octeon. */
	uint32_t		fill_cnt;

	/* The last time that the doorbell was rung. */
	uint64_t		last_db_time;

	/*
	 * The doorbell timeout. If the doorbell was not rung for this time
	 * and fill_cnt is non-zero, ring the doorbell again.
	 */
	uint32_t		db_timeout;

	/* Statistics for this input queue. */
	struct lio_iq_stats	stats;

	/* DMA mapped base address of the input descriptor ring. */
	uint64_t		base_addr_dma;

	/* Application context */
	void			*app_ctx;

	/* network stack queue index */
	int			q_index;

	/* os ifidx associated with this queue */
	int			ifidx;

};

/*----------------------  INSTRUCTION FORMAT ----------------------------*/

struct lio_instr3_64B {
	/* Pointer where the input data is available. */
	uint64_t	dptr;

	/* Instruction Header. */
	uint64_t	ih3;

	/* Instruction Header. */
	uint64_t	pki_ih3;

	/* Input Request Header. */
	uint64_t	irh;

	/* opcode/subcode specific parameters */
	uint64_t	ossp[2];

	/* Return Data Parameters */
	uint64_t	rdp;

	/*
	 * Pointer where the response for a RAW mode packet will be written
	 * by Octeon.
	 */
	uint64_t	rptr;

};

union lio_instr_64B {
	struct lio_instr3_64B	cmd3;
};

/* The size of each buffer in soft command buffer pool */
#define LIO_SOFT_COMMAND_BUFFER_SIZE	2048

struct lio_soft_command {
	/* Soft command buffer info. */
	struct lio_stailq_node	node;
	uint64_t		dma_addr;
	uint32_t		size;

	/* Command and return status */
	union lio_instr_64B	cmd;

#define COMPLETION_WORD_INIT    0xffffffffffffffffULL
	uint64_t		*status_word;

	/* Data buffer info */
	void			*virtdptr;
	uint64_t		dmadptr;
	uint32_t		datasize;

	/* Return buffer info */
	void			*virtrptr;
	uint64_t		dmarptr;
	uint32_t		rdatasize;

	/* Context buffer info */
	void			*ctxptr;
	uint32_t		ctxsize;

	/* Time out and callback */
	int			wait_time;
	int			timeout;
	uint32_t		iq_no;
	void			(*callback) (struct octeon_device *, uint32_t,
					     void *);
	void			*callback_arg;
};

/* Maximum number of buffers to allocate into soft command buffer pool */
#define LIO_MAX_SOFT_COMMAND_BUFFERS	256

/* Head of a soft command buffer pool. */
struct lio_sc_buffer_pool {
	/* List structure to add delete pending entries to */
	struct lio_stailq_head	head;

	/* A lock for this response list */
	struct mtx		lock;

	volatile uint32_t	alloc_buf_count;
};

#define LIO_INCR_INSTRQUEUE_PKT_COUNT(octeon_dev_ptr, iq_no, field, count) \
		(((octeon_dev_ptr)->instr_queue[iq_no]->stats.field) += count)

int	lio_setup_sc_buffer_pool(struct octeon_device *oct);
int	lio_free_sc_buffer_pool(struct octeon_device *oct);
struct lio_soft_command	*lio_alloc_soft_command(struct octeon_device *oct,
						uint32_t datasize,
						uint32_t rdatasize,
						uint32_t ctxsize);
void	lio_free_soft_command(struct octeon_device *oct,
			      struct lio_soft_command *sc);

/*
 *  lio_init_instr_queue()
 *  @param octeon_dev      - pointer to the octeon device structure.
 *  @param txpciq          - queue to be initialized (0 <= q_no <= 3).
 *
 *  Called at driver init time for each input queue. iq_conf has the
 *  configuration parameters for the queue.
 *
 *  @return  Success: 0   Failure: 1
 */
int	lio_init_instr_queue(struct octeon_device *octeon_dev,
			     union octeon_txpciq txpciq, uint32_t num_descs);

/*
 *  lio_delete_instr_queue()
 *  @param octeon_dev      - pointer to the octeon device structure.
 *  @param iq_no           - queue to be deleted
 *
 *  Called at driver unload time for each input queue. Deletes all
 *  allocated resources for the input queue.
 *
 *  @return  Success: 0   Failure: 1
 */
int	lio_delete_instr_queue(struct octeon_device *octeon_dev,
			       uint32_t iq_no);

int	lio_wait_for_instr_fetch(struct octeon_device *oct);

int	lio_process_iq_request_list(struct octeon_device *oct,
				    struct lio_instr_queue *iq,
				    uint32_t budget);

int	lio_send_command(struct octeon_device *oct, uint32_t iq_no,
			 uint32_t force_db, void *cmd, void *buf,
			 uint32_t datasize, uint32_t reqtype);

void	lio_prepare_soft_command(struct octeon_device *oct,
				 struct lio_soft_command *sc,
				 uint8_t opcode, uint8_t subcode,
				 uint32_t irh_ossp, uint64_t ossp0,
				 uint64_t ossp1);

int	lio_send_soft_command(struct octeon_device *oct,
			      struct lio_soft_command *sc);

int	lio_setup_iq(struct octeon_device *oct, int ifidx,
		     int q_index, union octeon_txpciq iq_no,
		     uint32_t num_descs);
int	lio_flush_iq(struct octeon_device *oct, struct lio_instr_queue *iq,
		     uint32_t budget);
#endif	/* __LIO_IQ_H__ */
