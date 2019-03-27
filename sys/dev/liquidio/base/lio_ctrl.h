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

/*   \file lio_ctrl.h
 *   \brief Host NIC Driver: Routine to send network data &
 *   control packet to Octeon.
 */

#ifndef __LIO_CTRL_H__
#define __LIO_CTRL_H__

/* Maximum number of 8-byte words can be sent in a NIC control message. */
#define LIO_MAX_NCTRL_UDD	32

typedef void	(*lio_ctrl_pkt_cb_fn_t)(void *);

/*
 * Structure of control information passed by the NIC module to the OSI
 * layer when sending control commands to Octeon device software.
 */
struct lio_ctrl_pkt {
	/* Command to be passed to the Octeon device software. */
	union octeon_cmd	ncmd;

	/* Send buffer  */
	void			*data;
	uint64_t		dmadata;

	/* Response buffer */
	void			*rdata;
	uint64_t		dmardata;

	/* Additional data that may be needed by some commands. */
	uint64_t		udd[LIO_MAX_NCTRL_UDD];

	/* Input queue to use to send this command. */
	uint64_t		iq_no;

	/*
	 *  Time to wait for Octeon software to respond to this control command.
	 *  If wait_time is 0, OSI assumes no response is expected.
	 */
	size_t			wait_time;

	/* The network device that issued the control command. */
	struct lio		*lio;

	/* Callback function called when the command has been fetched */
	lio_ctrl_pkt_cb_fn_t	cb_fn;
};

/*
 * Structure of data information passed by the NIC module to the OSI
 * layer when forwarding data to Octeon device software.
 */
struct lio_data_pkt {
	/*
	 *  Pointer to information maintained by NIC module for this packet. The
	 *  OSI layer passes this as-is to the driver.
	 */
	void			*buf;

	/* Type of buffer passed in "buf" above. */
	uint32_t		reqtype;

	/* Total data bytes to be transferred in this command. */
	uint32_t		datasize;

	/* Command to be passed to the Octeon device software. */
	union lio_instr_64B	cmd;

	/* Input queue to use to send this command. */
	uint32_t		q_no;

};

/*
 * Structure passed by NIC module to OSI layer to prepare a command to send
 * network data to Octeon.
 */
union lio_cmd_setup {
	struct {
		uint32_t	iq_no:8;
		uint32_t	gather:1;
		uint32_t	timestamp:1;
		uint32_t	ip_csum:1;
		uint32_t	transport_csum:1;
		uint32_t	tnl_csum:1;
		uint32_t	rsvd:19;

		union {
			uint32_t	datasize;
			uint32_t	gatherptrs;
		}	u;
	}	s;

	uint64_t	cmd_setup64;

};

static inline int
lio_iq_is_full(struct octeon_device *oct, uint32_t q_no)
{

	return (atomic_load_acq_int(&oct->instr_queue[q_no]->instr_pending) >=
		(oct->instr_queue[q_no]->max_count - 2));
}

static inline void
lio_prepare_pci_cmd_o3(struct octeon_device *oct, union lio_instr_64B *cmd,
		       union lio_cmd_setup *setup, uint32_t tag)
{
	union octeon_packet_params	packet_params;
	struct octeon_instr_irh		*irh;
	struct octeon_instr_ih3		*ih3;
	struct octeon_instr_pki_ih3	*pki_ih3;
	int	port;

	bzero(cmd, sizeof(union lio_instr_64B));

	ih3 = (struct octeon_instr_ih3 *)&cmd->cmd3.ih3;
	pki_ih3 = (struct octeon_instr_pki_ih3 *)&cmd->cmd3.pki_ih3;

	/*
	 * assume that rflag is cleared so therefore front data will only have
	 * irh and ossp[1] and ossp[2] for a total of 24 bytes
	 */
	ih3->pkind = oct->instr_queue[setup->s.iq_no]->txpciq.s.pkind;
	/* PKI IH */
	ih3->fsz = LIO_PCICMD_O3;

	if (!setup->s.gather) {
		ih3->dlengsz = setup->s.u.datasize;
	} else {
		ih3->gather = 1;
		ih3->dlengsz = setup->s.u.gatherptrs;
	}

	pki_ih3->w = 1;
	pki_ih3->raw = 0;
	pki_ih3->utag = 0;
	pki_ih3->utt = 1;
	pki_ih3->uqpg = oct->instr_queue[setup->s.iq_no]->txpciq.s.use_qpg;

	port = (int)oct->instr_queue[setup->s.iq_no]->txpciq.s.port;

	if (tag)
		pki_ih3->tag = tag;
	else
		pki_ih3->tag = LIO_DATA(port);

	pki_ih3->tagtype = LIO_ORDERED_TAG;
	pki_ih3->qpg = oct->instr_queue[setup->s.iq_no]->txpciq.s.qpg;
	pki_ih3->pm = 0x0;		/* parse from L2 */
	/* sl will be sizeof(pki_ih3) + irh + ossp0 + ossp1 */
	pki_ih3->sl = 32;

	irh = (struct octeon_instr_irh *)&cmd->cmd3.irh;

	irh->opcode = LIO_OPCODE_NIC;
	irh->subcode = LIO_OPCODE_NIC_NW_DATA;

	packet_params.pkt_params32 = 0;

	packet_params.s.ip_csum = setup->s.ip_csum;
	packet_params.s.transport_csum = setup->s.transport_csum;
	packet_params.s.tnl_csum = setup->s.tnl_csum;
	packet_params.s.tsflag = setup->s.timestamp;

	irh->ossp = packet_params.pkt_params32;
}

/*
 * Utility function to prepare a 64B NIC instruction based on a setup command
 * @param oct - Pointer to current octeon device
 * @param cmd - pointer to instruction to be filled in.
 * @param setup - pointer to the setup structure
 * @param q_no - which queue for back pressure
 *
 * Assumes the cmd instruction is pre-allocated, but no fields are filled in.
 */
static inline void
lio_prepare_pci_cmd(struct octeon_device *oct, union lio_instr_64B *cmd,
		    union lio_cmd_setup *setup, uint32_t tag)
{

	lio_prepare_pci_cmd_o3(oct, cmd, setup, tag);
}

/*
 * Send a NIC data packet to the device
 * @param oct - octeon device pointer
 * @param ndata - control structure with queueing, and buffer information
 *
 * @returns LIO_IQ_FAILED if it failed to add to the input queue.
 * LIO_IQ_STOP if it the queue should be stopped,
 * and LIO_IQ_SEND_OK if it sent okay.
 */
int	lio_send_data_pkt(struct octeon_device *oct,
			  struct lio_data_pkt *ndata);

/*
 * Send a NIC control packet to the device
 * @param oct - octeon device pointer
 * @param nctrl - control structure with command, timeout, and callback info
 * @returns IQ_FAILED if it failed to add to the input queue. IQ_STOP if it the
 * queue should be stopped, and LIO_IQ_SEND_OK if it sent okay.
 */
int	lio_send_ctrl_pkt(struct octeon_device *oct,
			  struct lio_ctrl_pkt *nctrl);

#endif	/* __LIO_CTRL_H__ */
