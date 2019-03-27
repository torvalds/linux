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

#ifndef _LIO_RXTX_H_
#define _LIO_RXTX_H_

/* Bit mask values for lio->ifstate */
#define LIO_IFSTATE_DROQ_OPS	0x01
#define LIO_IFSTATE_REGISTERED	0x02
#define LIO_IFSTATE_RUNNING	0x04
#define LIO_IFSTATE_DETACH	0x08
#define LIO_IFSTATE_RESETTING	0x10


/*
 * Structure of a node in list of gather components maintained by
 * NIC driver for each network device.
 */
struct lio_gather {
	/* List manipulation. Next and prev pointers. */
	struct lio_stailq_node	node;

	/* Size of the gather component at sg in bytes. */
	int	sg_size;

	/*
	 * Gather component that can accommodate max sized fragment list
	 * received from the IP layer.
	 */
	struct lio_sg_entry	*sg;

	uint64_t		sg_dma_ptr;
};

union lio_tx_info {
	uint64_t	tx_info64;
	struct {
#if _BYTE_ORDER == _BIG_ENDIAN
		uint16_t	gso_size;
		uint16_t	gso_segs;
		uint32_t	reserved;
#else	/* _BYTE_ORDER == _LITTLE_ENDIAN */
		uint32_t	reserved;
		uint16_t	gso_segs;
		uint16_t	gso_size;
#endif
	}	s;
};

int	lio_xmit(struct lio *lio, struct lio_instr_queue *iq,
		 struct mbuf **m_headp);
int	lio_mq_start_locked(struct ifnet *ifp, struct lio_instr_queue *iq);
int	lio_mq_start(struct ifnet *ifp, struct mbuf *m);
void	lio_qflush(struct ifnet *ifp);
#endif	/* _LIO_RXTX_H_ */
