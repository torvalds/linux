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

/*   \file  lio_droq.h
 *   \brief Implementation of Octeon Output queues. "Output" is with
 *   respect to the Octeon device on the NIC. From this driver's point of
 *   view they are ingress queues.
 */

#ifndef __LIO_DROQ_H__
#define __LIO_DROQ_H__

/*
 *  Octeon descriptor format.
 *  The descriptor ring is made of descriptors which have 2 64-bit values:
 *  -# Physical (bus) address of the data buffer.
 *  -# Physical (bus) address of a lio_droq_info structure.
 *  The Octeon device DMA's incoming packets and its information at the address
 *  given by these descriptor fields.
 */
struct lio_droq_desc {
	/* The buffer pointer */
	uint64_t	buffer_ptr;

	/* The Info pointer */
	uint64_t	info_ptr;
};

#define LIO_DROQ_DESC_SIZE	(sizeof(struct lio_droq_desc))

/*
 *  Information about packet DMA'ed by Octeon.
 *  The format of the information available at Info Pointer after Octeon
 *  has posted a packet. Not all descriptors have valid information. Only
 *  the Info field of the first descriptor for a packet has information
 *  about the packet.
 */
struct lio_droq_info {
	/* The Length of the packet. */
	uint64_t	length;

	/* The Output Receive Header. */
	union		octeon_rh rh;

};

#define LIO_DROQ_INFO_SIZE	(sizeof(struct lio_droq_info))

/*
 *  Pointer to data buffer.
 *  Driver keeps a pointer to the data buffer that it made available to
 *  the Octeon device. Since the descriptor ring keeps physical (bus)
 *  addresses, this field is required for the driver to keep track of
 *  the virtual address pointers.
 */
struct lio_recv_buffer {
	/* Packet buffer, including metadata. */
	void	*buffer;

	/* Data in the packet buffer.  */
	uint8_t	*data;
};

#define LIO_DROQ_RECVBUF_SIZE	(sizeof(struct lio_recv_buffer))

/* Output Queue statistics. Each output queue has four stats fields. */
struct lio_droq_stats {
	/* Number of packets received in this queue. */
	uint64_t	pkts_received;

	/* Bytes received by this queue. */
	uint64_t	bytes_received;

	/* Packets dropped due to no dispatch function. */
	uint64_t	dropped_nodispatch;

	/* Packets dropped due to no memory available. */
	uint64_t	dropped_nomem;

	/* Packets dropped due to large number of pkts to process. */
	uint64_t	dropped_toomany;

	/* Number of packets  sent to stack from this queue. */
	uint64_t	rx_pkts_received;

	/* Number of Bytes sent to stack from this queue. */
	uint64_t	rx_bytes_received;

	/* Num of Packets dropped due to receive path failures. */
	uint64_t	rx_dropped;

	uint64_t	rx_vxlan;

	/* Num of failures of lio_recv_buffer_alloc() */
	uint64_t	rx_alloc_failure;

};

/*
 * The maximum number of buffers that can be dispatched from the
 * output/dma queue. Set to 64 assuming 1K buffers in DROQ and the fact that
 * max packet size from DROQ is 64K.
 */
#define LIO_MAX_RECV_BUFS	64

/*
 *  Receive Packet format used when dispatching output queue packets
 *  with non-raw opcodes.
 *  The received packet will be sent to the upper layers using this
 *  structure which is passed as a parameter to the dispatch function
 */
struct lio_recv_pkt {
	/* Number of buffers in this received packet */
	uint16_t	buffer_count;

	/* Id of the device that is sending the packet up */
	uint16_t	octeon_id;

	/* Length of data in the packet buffer */
	uint32_t	length;

	/* The receive header */
	union octeon_rh	rh;

	/* Pointer to the OS-specific packet buffer */
	struct mbuf	*buffer_ptr[LIO_MAX_RECV_BUFS];

	/* Size of the buffers pointed to by ptr's in buffer_ptr */
	uint32_t	buffer_size[LIO_MAX_RECV_BUFS];
};

#define LIO_RECV_PKT_SIZE	(sizeof(struct lio_recv_pkt))

/*
 *  The first parameter of a dispatch function.
 *  For a raw mode opcode, the driver dispatches with the device
 *  pointer in this structure.
 *  For non-raw mode opcode, the driver dispatches the recv_pkt
 *  created to contain the buffers with data received from Octeon.
 *  ---------------------
 *  |     *recv_pkt ----|---
 *  |-------------------|   |
 *  | 0 or more bytes   |   |
 *  | reserved by driver|   |
 *  |-------------------|<-/
 *  | lio_recv_pkt   |
 *  |                   |
 *  |___________________|
 */
struct lio_recv_info {
	void			*rsvd;
	struct lio_recv_pkt	*recv_pkt;
};

#define LIO_RECV_INFO_SIZE	(sizeof(struct lio_recv_info))

/*
 *  Allocate a recv_info structure. The recv_pkt pointer in the recv_info
 *  structure is filled in before this call returns.
 *  @param extra_bytes - extra bytes to be allocated at the end of the recv info
 *                       structure.
 *  @return - pointer to a newly allocated recv_info structure.
 */
static inline struct lio_recv_info *
lio_alloc_recv_info(int extra_bytes)
{
	struct lio_recv_info	*recv_info;
	uint8_t			*buf;

	buf = malloc(LIO_RECV_PKT_SIZE + LIO_RECV_INFO_SIZE +
		     extra_bytes, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (buf == NULL)
		return (NULL);

	recv_info = (struct lio_recv_info *)buf;
	recv_info->recv_pkt = (struct lio_recv_pkt *)(buf + LIO_RECV_INFO_SIZE);
	recv_info->rsvd = NULL;
	if (extra_bytes)
		recv_info->rsvd = buf + LIO_RECV_INFO_SIZE + LIO_RECV_PKT_SIZE;

	return (recv_info);
}

/*
 *  Free a recv_info structure.
 *  @param recv_info - Pointer to receive_info to be freed
 */
static inline void
lio_free_recv_info(struct lio_recv_info *recv_info)
{

	free(recv_info, M_DEVBUF);
}

typedef int	(*lio_dispatch_fn_t)(struct lio_recv_info *, void *);

/*
 * Used by NIC module to register packet handler and to get device
 * information for each octeon device.
 */
struct lio_droq_ops {
	/*
	 *  This registered function will be called by the driver with
	 *  the pointer to buffer from droq and length of
	 *  data in the buffer. The receive header gives the port
	 *  number to the caller.  Function pointer is set by caller.
	 */
	void		(*fptr) (void *, uint32_t, union octeon_rh *, void  *,
				 void *);
	void		*farg;

	/*
	 *  Flag indicating if the DROQ handler should drop packets that
	 *  it cannot handle in one iteration. Set by caller.
	 */
	uint32_t	drop_on_max;
};

/*
 * The Descriptor Ring Output Queue structure.
 *  This structure has all the information required to implement a
 *  Octeon DROQ.
 */
struct lio_droq {
	/* A lock to protect access to this ring. */
	struct mtx		lock;

	uint32_t		q_no;

	uint32_t		pkt_count;

	struct lio_droq_ops	ops;

	struct octeon_device	*oct_dev;

	/* The 8B aligned descriptor ring starts at this address. */
	struct lio_droq_desc	*desc_ring;

	/* Index in the ring where the driver should read the next packet */
	uint32_t		read_idx;

	/*
	 * Index in the ring where the driver will refill the descriptor's
	 * buffer
	 */
	uint32_t		refill_idx;

	/* Packets pending to be processed */
	volatile int		pkts_pending;

	/* Number of  descriptors in this ring. */
	uint32_t		max_count;

	/* The number of descriptors pending refill. */
	uint32_t		refill_count;

	uint32_t		pkts_per_intr;
	uint32_t		refill_threshold;

	/*
	 * The max number of descriptors in DROQ without a buffer.
	 * This field is used to keep track of empty space threshold. If the
	 * refill_count reaches this value, the DROQ cannot accept a max-sized
	 * (64K) packet.
	 */
	uint32_t		max_empty_descs;

	/*
	 * The receive buffer list. This list has the virtual addresses of
	 * the buffers.
	 */
	struct lio_recv_buffer	*recv_buf_list;

	/* The size of each buffer pointed by the buffer pointer. */
	uint32_t		buffer_size;

	/*
	 * Offset to packet credit register.
	 * Host writes number of info/buffer ptrs available to this register
	 */
	uint32_t		pkts_credit_reg;

	/*
	 * Offset packet sent register.
	 * Octeon writes the number of packets DMA'ed to host memory
	 * in this register.
	 */
	uint32_t		pkts_sent_reg;

	struct lio_stailq_head	dispatch_stq_head;

	/* Statistics for this DROQ. */
	struct lio_droq_stats	stats;

	/* DMA mapped address of the DROQ descriptor ring. */
	vm_paddr_t		desc_ring_dma;

	/* application context */
	void			*app_ctx;

	uint32_t		cpu_id;

	struct task		droq_task;
	struct taskqueue	*droq_taskqueue;

	struct lro_ctrl		lro;
};

#define LIO_DROQ_SIZE	(sizeof(struct lio_droq))

/*
 * Allocates space for the descriptor ring for the droq and sets the
 *   base addr, num desc etc in Octeon registers.
 *
 * @param  oct_dev    - pointer to the octeon device structure
 * @param  q_no       - droq no.
 * @param app_ctx     - pointer to application context
 * @return Success: 0    Failure: 1
 */
int	lio_init_droq(struct octeon_device *oct_dev,
		      uint32_t q_no, uint32_t num_descs, uint32_t desc_size,
		      void *app_ctx);

/*
 *  Frees the space for descriptor ring for the droq.
 *
 *  @param oct_dev - pointer to the octeon device structure
 *  @param q_no    - droq no.
 *  @return:    Success: 0    Failure: 1
 */
int	lio_delete_droq(struct octeon_device *oct_dev, uint32_t q_no);

/*
 * Register a change in droq operations. The ops field has a pointer to a
 * function which will called by the DROQ handler for all packets arriving
 * on output queues given by q_no irrespective of the type of packet.
 * The ops field also has a flag which if set tells the DROQ handler to
 * drop packets if it receives more than what it can process in one
 * invocation of the handler.
 * @param oct       - octeon device
 * @param q_no      - octeon output queue number (0 <= q_no <= MAX_OCTEON_DROQ-1
 * @param ops       - the droq_ops settings for this queue
 * @return          - 0 on success, -ENODEV or -EINVAL on error.
 */
int	lio_register_droq_ops(struct octeon_device *oct, uint32_t q_no,
			      struct lio_droq_ops *ops);

/*
 * Resets the function pointer and flag settings made by
 * lio_register_droq_ops(). After this routine is called, the DROQ handler
 * will lookup dispatch function for each arriving packet on the output queue
 * given by q_no.
 * @param oct       - octeon device
 * @param q_no      - octeon output queue number (0 <= q_no <= MAX_OCTEON_DROQ-1
 * @return          - 0 on success, -ENODEV or -EINVAL on error.
 */
int	lio_unregister_droq_ops(struct octeon_device *oct, uint32_t q_no);

/*
 *    Register a dispatch function for a opcode/subcode. The driver will call
 *    this dispatch function when it receives a packet with the given
 *    opcode/subcode in its output queues along with the user specified
 *    argument.
 *    @param  oct        - the octeon device to register with.
 *    @param  opcode     - the opcode for which the dispatch will be registered.
 *    @param  subcode    - the subcode for which the dispatch will be registered
 *    @param  fn         - the dispatch function.
 *    @param  fn_arg     - user specified that will be passed along with the
 *                         dispatch function by the driver.
 *    @return Success: 0; Failure: 1
 */
int	lio_register_dispatch_fn(struct octeon_device *oct, uint16_t opcode,
				 uint16_t subcode, lio_dispatch_fn_t fn,
				 void *fn_arg);

/*
 *   Remove registration for an opcode/subcode. This will delete the mapping for
 *   an opcode/subcode. The dispatch function will be unregistered and will no
 *   longer be called if a packet with the opcode/subcode arrives in the driver
 *   output queues.
 *   @param  oct        -  the octeon device to unregister from.
 *   @param  opcode     -  the opcode to be unregistered.
 *   @param  subcode    -  the subcode to be unregistered.
 *
 *   @return Success: 0; Failure: 1
 */
int	lio_unregister_dispatch_fn(struct octeon_device *oct, uint16_t opcode,
				   uint16_t subcode);

uint32_t	lio_droq_check_hw_for_pkts(struct lio_droq *droq);

int	lio_create_droq(struct octeon_device *oct, uint32_t q_no,
			uint32_t num_descs, uint32_t desc_size, void *app_ctx);

int	lio_droq_process_packets(struct octeon_device *oct,
				 struct lio_droq *droq, uint32_t budget);

uint32_t	lio_droq_refill(struct octeon_device *octeon_dev,
				struct lio_droq *droq);
void	lio_droq_bh(void *ptr, int pending __unused);
#endif	/* __LIO_DROQ_H__ */
