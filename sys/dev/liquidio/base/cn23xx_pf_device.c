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

#include "lio_bsd.h"
#include "lio_common.h"
#include "lio_droq.h"
#include "lio_iq.h"
#include "lio_response_manager.h"
#include "lio_device.h"
#include "cn23xx_pf_device.h"
#include "lio_main.h"
#include "lio_rss.h"

static int
lio_cn23xx_pf_soft_reset(struct octeon_device *oct)
{

	lio_write_csr64(oct, LIO_CN23XX_SLI_WIN_WR_MASK_REG, 0xFF);

	lio_dev_dbg(oct, "BIST enabled for CN23XX soft reset\n");

	lio_write_csr64(oct, LIO_CN23XX_SLI_SCRATCH1, 0x1234ULL);

	/* Initiate chip-wide soft reset */
	lio_pci_readq(oct, LIO_CN23XX_RST_SOFT_RST);
	lio_pci_writeq(oct, 1, LIO_CN23XX_RST_SOFT_RST);

	/* Wait for 100ms as Octeon resets. */
	lio_mdelay(100);

	if (lio_read_csr64(oct, LIO_CN23XX_SLI_SCRATCH1)) {
		lio_dev_err(oct, "Soft reset failed\n");
		return (1);
	}

	lio_dev_dbg(oct, "Reset completed\n");

	/* restore the  reset value */
	lio_write_csr64(oct, LIO_CN23XX_SLI_WIN_WR_MASK_REG, 0xFF);

	return (0);
}

static void
lio_cn23xx_pf_enable_error_reporting(struct octeon_device *oct)
{
	uint32_t	corrtable_err_status, uncorrectable_err_mask, regval;

	regval = lio_read_pci_cfg(oct, LIO_CN23XX_CFG_PCIE_DEVCTL);
	if (regval & LIO_CN23XX_CFG_PCIE_DEVCTL_MASK) {
		uncorrectable_err_mask = 0;
		corrtable_err_status = 0;
		uncorrectable_err_mask =
		    lio_read_pci_cfg(oct,
				     LIO_CN23XX_CFG_PCIE_UNCORRECT_ERR_MASK);
		corrtable_err_status =
		    lio_read_pci_cfg(oct,
				     LIO_CN23XX_CFG_PCIE_CORRECT_ERR_STATUS);
		lio_dev_err(oct, "PCI-E Fatal error detected;\n"
			    "\tdev_ctl_status_reg = 0x%08x\n"
			    "\tuncorrectable_error_mask_reg = 0x%08x\n"
			    "\tcorrectable_error_status_reg = 0x%08x\n",
			    regval, uncorrectable_err_mask,
			    corrtable_err_status);
	}

	regval |= 0xf;	/* Enable Link error reporting */

	lio_dev_dbg(oct, "Enabling PCI-E error reporting..\n");
	lio_write_pci_cfg(oct, LIO_CN23XX_CFG_PCIE_DEVCTL, regval);
}

static uint32_t
lio_cn23xx_pf_coprocessor_clock(struct octeon_device *oct)
{
	/*
	 * Bits 29:24 of RST_BOOT[PNR_MUL] holds the ref.clock MULTIPLIER
	 * for SLI.
	 */

	/* TBD: get the info in Hand-shake */
	return (((lio_pci_readq(oct, LIO_CN23XX_RST_BOOT) >> 24) & 0x3f) * 50);
}

uint32_t
lio_cn23xx_pf_get_oq_ticks(struct octeon_device *oct, uint32_t time_intr_in_us)
{
	/* This gives the SLI clock per microsec */
	uint32_t	oqticks_per_us = lio_cn23xx_pf_coprocessor_clock(oct);

	oct->pfvf_hsword.coproc_tics_per_us = oqticks_per_us;

	/* This gives the clock cycles per millisecond */
	oqticks_per_us *= 1000;

	/* This gives the oq ticks (1024 core clock cycles) per millisecond */
	oqticks_per_us /= 1024;

	/*
	 * time_intr is in microseconds. The next 2 steps gives the oq ticks
	 * corresponding to time_intr.
	 */
	oqticks_per_us *= time_intr_in_us;
	oqticks_per_us /= 1000;

	return (oqticks_per_us);
}

static void
lio_cn23xx_pf_setup_global_mac_regs(struct octeon_device *oct)
{
	uint64_t	reg_val;
	uint16_t	mac_no = oct->pcie_port;
	uint16_t	pf_num = oct->pf_num;
	/* programming SRN and TRS for each MAC(0..3)  */

	lio_dev_dbg(oct, "%s: Using pcie port %d\n", __func__, mac_no);
	/* By default, mapping all 64 IOQs to  a single MACs */

	reg_val =
	    lio_read_csr64(oct, LIO_CN23XX_SLI_PKT_MAC_RINFO64(mac_no, pf_num));

	/* setting SRN <6:0>  */
	reg_val = pf_num * LIO_CN23XX_PF_MAX_RINGS;

	/* setting TRS <23:16> */
	reg_val = reg_val |
	    (oct->sriov_info.trs << LIO_CN23XX_PKT_MAC_CTL_RINFO_TRS_BIT_POS);

	/* write these settings to MAC register */
	lio_write_csr64(oct, LIO_CN23XX_SLI_PKT_MAC_RINFO64(mac_no, pf_num),
			reg_val);

	lio_dev_dbg(oct, "SLI_PKT_MAC(%d)_PF(%d)_RINFO : 0x%016llx\n", mac_no,
		    pf_num,
		    LIO_CAST64(lio_read_csr64(oct,
				   LIO_CN23XX_SLI_PKT_MAC_RINFO64(mac_no,
								  pf_num))));
}

static int
lio_cn23xx_pf_reset_io_queues(struct octeon_device *oct)
{
	uint64_t	d64;
	uint32_t	ern, loop = BUSY_READING_REG_PF_LOOP_COUNT;
	uint32_t	q_no, srn;
	int		ret_val = 0;

	srn = oct->sriov_info.pf_srn;
	ern = srn + oct->sriov_info.num_pf_rings;

	/* As per HRM reg description, s/w cant write 0 to ENB. */
	/* to make the queue off, need to set the RST bit. */

	/* Reset the Enable bit for all the 64 IQs.  */
	for (q_no = srn; q_no < ern; q_no++) {
		/* set RST bit to 1. This bit applies to both IQ and OQ */
		d64 = lio_read_csr64(oct,
				     LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
		d64 = d64 | LIO_CN23XX_PKT_INPUT_CTL_RST;
		lio_write_csr64(oct,
				LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no), d64);
	}

	/* wait until the RST bit is clear or the RST and quiet bits are set */
	for (q_no = srn; q_no < ern; q_no++) {
		volatile uint64_t reg_val =
			lio_read_csr64(oct,
				       LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
		while ((reg_val & LIO_CN23XX_PKT_INPUT_CTL_RST) &&
		       !(reg_val & LIO_CN23XX_PKT_INPUT_CTL_QUIET) &&
		       loop) {
			reg_val = lio_read_csr64(oct,
				       LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
			loop--;
		}

		if (!loop) {
			lio_dev_err(oct,
				    "clearing the reset reg failed or setting the quiet reg failed for qno: %u\n",
				    q_no);
			return (-1);
		}

		reg_val &= ~LIO_CN23XX_PKT_INPUT_CTL_RST;
		lio_write_csr64(oct, LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no),
				reg_val);

		reg_val = lio_read_csr64(oct,
					 LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
		if (reg_val & LIO_CN23XX_PKT_INPUT_CTL_RST) {
			lio_dev_err(oct, "clearing the reset failed for qno: %u\n",
				    q_no);
			ret_val = -1;
		}
	}

	return (ret_val);
}

static int
lio_cn23xx_pf_setup_global_input_regs(struct octeon_device *oct)
{
	struct lio_cn23xx_pf	*cn23xx = (struct lio_cn23xx_pf *)oct->chip;
	struct lio_instr_queue	*iq;
	uint64_t		intr_threshold;
	uint64_t		pf_num, reg_val;
	uint32_t		q_no, ern, srn;

	pf_num = oct->pf_num;

	srn = oct->sriov_info.pf_srn;
	ern = srn + oct->sriov_info.num_pf_rings;

	if (lio_cn23xx_pf_reset_io_queues(oct))
		return (-1);

	/*
	 * Set the MAC_NUM and PVF_NUM in IQ_PKT_CONTROL reg
	 * for all queues.Only PF can set these bits.
	 * bits 29:30 indicate the MAC num.
	 * bits 32:47 indicate the PVF num.
	 */
	for (q_no = 0; q_no < ern; q_no++) {
		reg_val = oct->pcie_port <<
			LIO_CN23XX_PKT_INPUT_CTL_MAC_NUM_POS;

		reg_val |= pf_num << LIO_CN23XX_PKT_INPUT_CTL_PF_NUM_POS;

		lio_write_csr64(oct, LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no),
				reg_val);
	}

	/*
	 * Select ES, RO, NS, RDSIZE,DPTR Fomat#0 for
	 * pf queues
	 */
	for (q_no = srn; q_no < ern; q_no++) {
		uint32_t	inst_cnt_reg;

		iq = oct->instr_queue[q_no];
		if (iq != NULL)
			inst_cnt_reg = iq->inst_cnt_reg;
		else
			inst_cnt_reg = LIO_CN23XX_SLI_IQ_INSTR_COUNT64(q_no);

		reg_val =
		    lio_read_csr64(oct, LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no));

		reg_val |= LIO_CN23XX_PKT_INPUT_CTL_MASK;

		lio_write_csr64(oct, LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no),
				reg_val);

		/* Set WMARK level for triggering PI_INT */
		/* intr_threshold = LIO_CN23XX_DEF_IQ_INTR_THRESHOLD & */
		intr_threshold = LIO_GET_IQ_INTR_PKT_CFG(cn23xx->conf) &
		    LIO_CN23XX_PKT_IN_DONE_WMARK_MASK;

		lio_write_csr64(oct, inst_cnt_reg,
				(lio_read_csr64(oct, inst_cnt_reg) &
				 ~(LIO_CN23XX_PKT_IN_DONE_WMARK_MASK <<
				   LIO_CN23XX_PKT_IN_DONE_WMARK_BIT_POS)) |
				(intr_threshold <<
				 LIO_CN23XX_PKT_IN_DONE_WMARK_BIT_POS));
	}
	return (0);
}

static void
lio_cn23xx_pf_setup_global_output_regs(struct octeon_device *oct)
{
	struct lio_cn23xx_pf *cn23xx = (struct lio_cn23xx_pf *)oct->chip;
	uint64_t	time_threshold;
	uint32_t	ern, q_no, reg_val, srn;

	srn = oct->sriov_info.pf_srn;
	ern = srn + oct->sriov_info.num_pf_rings;

	if (LIO_GET_IS_SLI_BP_ON_CFG(cn23xx->conf)) {
		lio_write_csr64(oct, LIO_CN23XX_SLI_OQ_WMARK, 32);
	} else {
		/* Set Output queue watermark to 0 to disable backpressure */
		lio_write_csr64(oct, LIO_CN23XX_SLI_OQ_WMARK, 0);
	}

	for (q_no = srn; q_no < ern; q_no++) {
		reg_val = lio_read_csr32(oct,
					 LIO_CN23XX_SLI_OQ_PKT_CONTROL(q_no));

		/* set IPTR & DPTR */
		reg_val |= LIO_CN23XX_PKT_OUTPUT_CTL_DPTR;

		/* reset BMODE */
		reg_val &= ~(LIO_CN23XX_PKT_OUTPUT_CTL_BMODE);

		/*
		 * No Relaxed Ordering, No Snoop, 64-bit Byte swap for
		 * Output Queue ScatterList reset ROR_P, NSR_P
		 */
		reg_val &= ~(LIO_CN23XX_PKT_OUTPUT_CTL_ROR_P);
		reg_val &= ~(LIO_CN23XX_PKT_OUTPUT_CTL_NSR_P);

#if BYTE_ORDER == LITTLE_ENDIAN
		reg_val &= ~(LIO_CN23XX_PKT_OUTPUT_CTL_ES_P);
#else	/* BYTE_ORDER != LITTLE_ENDIAN  */
		reg_val |= (LIO_CN23XX_PKT_OUTPUT_CTL_ES_P);
#endif	/* BYTE_ORDER == LITTLE_ENDIAN */

		/*
		 * No Relaxed Ordering, No Snoop, 64-bit Byte swap for
		 * Output Queue Data reset ROR, NSR
		 */
		reg_val &= ~(LIO_CN23XX_PKT_OUTPUT_CTL_ROR);
		reg_val &= ~(LIO_CN23XX_PKT_OUTPUT_CTL_NSR);
		/* set the ES bit */
		reg_val |= (LIO_CN23XX_PKT_OUTPUT_CTL_ES);

		/* write all the selected settings */
		lio_write_csr32(oct, LIO_CN23XX_SLI_OQ_PKT_CONTROL(q_no),
				reg_val);

		/*
		 * Enabling these interrupt in oct->fn_list.enable_interrupt()
		 * routine which called after IOQ init.
		 * Set up interrupt packet and time thresholds
		 * for all the OQs
		 */
		time_threshold =lio_cn23xx_pf_get_oq_ticks(
		       oct, (uint32_t)LIO_GET_OQ_INTR_TIME_CFG(cn23xx->conf));

		lio_write_csr64(oct, LIO_CN23XX_SLI_OQ_PKT_INT_LEVELS(q_no),
				(LIO_GET_OQ_INTR_PKT_CFG(cn23xx->conf) |
				 (time_threshold << 32)));
	}

	/* Setting the water mark level for pko back pressure * */
	lio_write_csr64(oct, LIO_CN23XX_SLI_OQ_WMARK, 0x40);

	/* Enable channel-level backpressure */
	if (oct->pf_num)
		lio_write_csr64(oct, LIO_CN23XX_SLI_OUT_BP_EN2_W1S,
				0xffffffffffffffffULL);
	else
		lio_write_csr64(oct, LIO_CN23XX_SLI_OUT_BP_EN_W1S,
				0xffffffffffffffffULL);
}

static int
lio_cn23xx_pf_setup_device_regs(struct octeon_device *oct)
{

	lio_cn23xx_pf_enable_error_reporting(oct);

	/* program the MAC(0..3)_RINFO before setting up input/output regs */
	lio_cn23xx_pf_setup_global_mac_regs(oct);

	if (lio_cn23xx_pf_setup_global_input_regs(oct))
		return (-1);

	lio_cn23xx_pf_setup_global_output_regs(oct);

	/*
	 * Default error timeout value should be 0x200000 to avoid host hang
	 * when reads invalid register
	 */
	lio_write_csr64(oct, LIO_CN23XX_SLI_WINDOW_CTL,
			LIO_CN23XX_SLI_WINDOW_CTL_DEFAULT);

	/* set SLI_PKT_IN_JABBER to handle large VXLAN packets */
	lio_write_csr64(oct, LIO_CN23XX_SLI_PKT_IN_JABBER,
			LIO_CN23XX_MAX_INPUT_JABBER);
	return (0);
}

static void
lio_cn23xx_pf_setup_iq_regs(struct octeon_device *oct, uint32_t iq_no)
{
	struct lio_instr_queue	*iq = oct->instr_queue[iq_no];
	uint64_t		pkt_in_done;

	iq_no += oct->sriov_info.pf_srn;

	/* Write the start of the input queue's ring and its size  */
	lio_write_csr64(oct, LIO_CN23XX_SLI_IQ_BASE_ADDR64(iq_no),
			iq->base_addr_dma);
	lio_write_csr32(oct, LIO_CN23XX_SLI_IQ_SIZE(iq_no), iq->max_count);

	/*
	 * Remember the doorbell & instruction count register addr
	 * for this queue
	 */
	iq->doorbell_reg = LIO_CN23XX_SLI_IQ_DOORBELL(iq_no);
	iq->inst_cnt_reg = LIO_CN23XX_SLI_IQ_INSTR_COUNT64(iq_no);
	lio_dev_dbg(oct, "InstQ[%d]:dbell reg @ 0x%x instcnt_reg @ 0x%x\n",
		    iq_no, iq->doorbell_reg, iq->inst_cnt_reg);

	/*
	 * Store the current instruction counter (used in flush_iq
	 * calculation)
	 */
	pkt_in_done = lio_read_csr64(oct, iq->inst_cnt_reg);

	if (oct->msix_on) {
		/* Set CINT_ENB to enable IQ interrupt   */
		lio_write_csr64(oct, iq->inst_cnt_reg,
				(pkt_in_done | LIO_CN23XX_INTR_CINT_ENB));
	} else {
		/*
		 * Clear the count by writing back what we read, but don't
		 * enable interrupts
		 */
		lio_write_csr64(oct, iq->inst_cnt_reg, pkt_in_done);
	}

	iq->reset_instr_cnt = 0;
}

static void
lio_cn23xx_pf_setup_oq_regs(struct octeon_device *oct, uint32_t oq_no)
{
	struct lio_droq		*droq = oct->droq[oq_no];
	struct lio_cn23xx_pf	*cn23xx = (struct lio_cn23xx_pf *)oct->chip;
	uint64_t		cnt_threshold;
	uint64_t		time_threshold;
	uint32_t		reg_val;

	oq_no += oct->sriov_info.pf_srn;

	lio_write_csr64(oct, LIO_CN23XX_SLI_OQ_BASE_ADDR64(oq_no),
			droq->desc_ring_dma);
	lio_write_csr32(oct, LIO_CN23XX_SLI_OQ_SIZE(oq_no), droq->max_count);

	lio_write_csr32(oct, LIO_CN23XX_SLI_OQ_BUFF_INFO_SIZE(oq_no),
			droq->buffer_size);

	/* pkt_sent and pkts_credit regs */
	droq->pkts_sent_reg = LIO_CN23XX_SLI_OQ_PKTS_SENT(oq_no);
	droq->pkts_credit_reg = LIO_CN23XX_SLI_OQ_PKTS_CREDIT(oq_no);

	if (!oct->msix_on) {
		/*
		 * Enable this output queue to generate Packet Timer
		 * Interrupt
		 */
		reg_val =
		    lio_read_csr32(oct, LIO_CN23XX_SLI_OQ_PKT_CONTROL(oq_no));
		reg_val |= LIO_CN23XX_PKT_OUTPUT_CTL_TENB;
		lio_write_csr32(oct, LIO_CN23XX_SLI_OQ_PKT_CONTROL(oq_no),
				reg_val);

		/*
		 * Enable this output queue to generate Packet Count
		 * Interrupt
		 */
		reg_val =
		    lio_read_csr32(oct, LIO_CN23XX_SLI_OQ_PKT_CONTROL(oq_no));
		reg_val |= LIO_CN23XX_PKT_OUTPUT_CTL_CENB;
		lio_write_csr32(oct, LIO_CN23XX_SLI_OQ_PKT_CONTROL(oq_no),
				reg_val);
	} else {
		time_threshold = lio_cn23xx_pf_get_oq_ticks(oct,
			(uint32_t)LIO_GET_OQ_INTR_TIME_CFG(cn23xx->conf));
		cnt_threshold = (uint32_t)LIO_GET_OQ_INTR_PKT_CFG(cn23xx->conf);

		lio_write_csr64(oct, LIO_CN23XX_SLI_OQ_PKT_INT_LEVELS(oq_no),
				((time_threshold << 32 | cnt_threshold)));
	}
}


static int
lio_cn23xx_pf_enable_io_queues(struct octeon_device *oct)
{
	uint64_t	reg_val;
	uint32_t	ern, loop = BUSY_READING_REG_PF_LOOP_COUNT;
	uint32_t	q_no, srn;

	srn = oct->sriov_info.pf_srn;
	ern = srn + oct->num_iqs;

	for (q_no = srn; q_no < ern; q_no++) {
		/* set the corresponding IQ IS_64B bit */
		if (oct->io_qmask.iq64B & BIT_ULL(q_no - srn)) {
			reg_val = lio_read_csr64(oct,
					LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
			reg_val = reg_val | LIO_CN23XX_PKT_INPUT_CTL_IS_64B;
			lio_write_csr64(oct,
					LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no),
					reg_val);
		}
		/* set the corresponding IQ ENB bit */
		if (oct->io_qmask.iq & BIT_ULL(q_no - srn)) {
			/*
			 * IOQs are in reset by default in PEM2 mode,
			 * clearing reset bit
			 */
			reg_val = lio_read_csr64(oct,
					LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no));

			if (reg_val & LIO_CN23XX_PKT_INPUT_CTL_RST) {
				while ((reg_val &
					LIO_CN23XX_PKT_INPUT_CTL_RST) &&
				       !(reg_val &
					 LIO_CN23XX_PKT_INPUT_CTL_QUIET) &&
				       loop) {
					reg_val = lio_read_csr64(oct,
					LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
					loop--;
				}
				if (!loop) {
					lio_dev_err(oct, "clearing the reset reg failed or setting the quiet reg failed for qno: %u\n",
						    q_no);
					return (-1);
				}
				reg_val = reg_val &
					~LIO_CN23XX_PKT_INPUT_CTL_RST;
				lio_write_csr64(oct,
					LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no),
					reg_val);

				reg_val = lio_read_csr64(oct,
					LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
				if (reg_val & LIO_CN23XX_PKT_INPUT_CTL_RST) {
					lio_dev_err(oct, "clearing the reset failed for qno: %u\n",
						    q_no);
					return (-1);
				}
			}
			reg_val = lio_read_csr64(oct,
					LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
			reg_val = reg_val | LIO_CN23XX_PKT_INPUT_CTL_RING_ENB;
			lio_write_csr64(oct,
					LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no),
					reg_val);
		}
	}
	for (q_no = srn; q_no < ern; q_no++) {
		uint32_t	reg_val;
		/* set the corresponding OQ ENB bit */
		if (oct->io_qmask.oq & BIT_ULL(q_no - srn)) {
			reg_val = lio_read_csr32(oct,
					LIO_CN23XX_SLI_OQ_PKT_CONTROL(q_no));
			reg_val = reg_val | LIO_CN23XX_PKT_OUTPUT_CTL_RING_ENB;
			lio_write_csr32(oct,
					LIO_CN23XX_SLI_OQ_PKT_CONTROL(q_no),
					reg_val);
		}
	}
	return (0);
}

static void
lio_cn23xx_pf_disable_io_queues(struct octeon_device *oct)
{
	volatile uint64_t	d64;
	volatile uint32_t	d32;
	int			loop;
	unsigned int		q_no;
	uint32_t		ern, srn;

	srn = oct->sriov_info.pf_srn;
	ern = srn + oct->num_iqs;

	/* Disable Input Queues. */
	for (q_no = srn; q_no < ern; q_no++) {
		loop = lio_ms_to_ticks(1000);

		/* start the Reset for a particular ring */
		d64 = lio_read_csr64(oct,
				     LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no));
		d64 &= ~LIO_CN23XX_PKT_INPUT_CTL_RING_ENB;
		d64 |= LIO_CN23XX_PKT_INPUT_CTL_RST;
		lio_write_csr64(oct, LIO_CN23XX_SLI_IQ_PKT_CONTROL64(q_no),
				d64);

		/*
		 * Wait until hardware indicates that the particular IQ
		 * is out of reset.
		 */
		d64 = lio_read_csr64(oct, LIO_CN23XX_SLI_PKT_IOQ_RING_RST);
		while (!(d64 & BIT_ULL(q_no)) && loop--) {
			d64 = lio_read_csr64(oct,
					     LIO_CN23XX_SLI_PKT_IOQ_RING_RST);
			lio_sleep_timeout(1);
			loop--;
		}

		/* Reset the doorbell register for this Input Queue. */
		lio_write_csr32(oct, LIO_CN23XX_SLI_IQ_DOORBELL(q_no),
				0xFFFFFFFF);
		while (((lio_read_csr64(oct,
					LIO_CN23XX_SLI_IQ_DOORBELL(q_no))) !=
			0ULL) && loop--) {
			lio_sleep_timeout(1);
		}
	}

	/* Disable Output Queues. */
	for (q_no = srn; q_no < ern; q_no++) {
		loop = lio_ms_to_ticks(1000);

		/*
		 * Wait until hardware indicates that the particular IQ
		 * is out of reset.It given that SLI_PKT_RING_RST is
		 * common for both IQs and OQs
		 */
		d64 = lio_read_csr64(oct, LIO_CN23XX_SLI_PKT_IOQ_RING_RST);
		while (!(d64 & BIT_ULL(q_no)) && loop--) {
			d64 = lio_read_csr64(oct,
					     LIO_CN23XX_SLI_PKT_IOQ_RING_RST);
			lio_sleep_timeout(1);
			loop--;
		}

		/* Reset the doorbell register for this Output Queue. */
		lio_write_csr32(oct, LIO_CN23XX_SLI_OQ_PKTS_CREDIT(q_no),
				0xFFFFFFFF);
		while ((lio_read_csr64(oct,
				       LIO_CN23XX_SLI_OQ_PKTS_CREDIT(q_no)) !=
			0ULL) && loop--) {
			lio_sleep_timeout(1);
		}

		/* clear the SLI_PKT(0..63)_CNTS[CNT] reg value */
		d32 = lio_read_csr32(oct, LIO_CN23XX_SLI_OQ_PKTS_SENT(q_no));
		lio_write_csr32(oct, LIO_CN23XX_SLI_OQ_PKTS_SENT(q_no),	d32);
	}
}

static uint64_t
lio_cn23xx_pf_msix_interrupt_handler(void *dev)
{
	struct lio_ioq_vector	*ioq_vector = (struct lio_ioq_vector *)dev;
	struct octeon_device	*oct = ioq_vector->oct_dev;
	struct lio_droq		*droq = oct->droq[ioq_vector->droq_index];
	uint64_t		pkts_sent;
	uint64_t		ret = 0;

	if (droq == NULL) {
		lio_dev_err(oct, "23XX bringup FIXME: oct pfnum:%d ioq_vector->ioq_num :%d droq is NULL\n",
			    oct->pf_num, ioq_vector->ioq_num);
		return (0);
	}
	pkts_sent = lio_read_csr64(oct, droq->pkts_sent_reg);

	/*
	 * If our device has interrupted, then proceed. Also check
	 * for all f's if interrupt was triggered on an error
	 * and the PCI read fails.
	 */
	if (!pkts_sent || (pkts_sent == 0xFFFFFFFFFFFFFFFFULL))
		return (ret);

	/* Write count reg in sli_pkt_cnts to clear these int. */
	if (pkts_sent & LIO_CN23XX_INTR_PO_INT)
		ret |= LIO_MSIX_PO_INT;

	if (pkts_sent & LIO_CN23XX_INTR_PI_INT)
		/* We will clear the count when we update the read_index. */
		ret |= LIO_MSIX_PI_INT;

	/*
	 * Never need to handle msix mbox intr for pf. They arrive on the last
	 * msix
	 */
	return (ret);
}

static void
lio_cn23xx_pf_interrupt_handler(void *dev)
{
	struct octeon_device	*oct = (struct octeon_device *)dev;
	struct lio_cn23xx_pf	*cn23xx = (struct lio_cn23xx_pf *)oct->chip;
	uint64_t		intr64;

	lio_dev_dbg(oct, "In %s octeon_dev @ %p\n", __func__, oct);
	intr64 = lio_read_csr64(oct, cn23xx->intr_sum_reg64);

	oct->int_status = 0;

	if (intr64 & LIO_CN23XX_INTR_ERR)
		lio_dev_err(oct, "Error Intr: 0x%016llx\n",
			    LIO_CAST64(intr64));

	if (oct->msix_on != LIO_FLAG_MSIX_ENABLED) {
		if (intr64 & LIO_CN23XX_INTR_PKT_DATA)
			oct->int_status |= LIO_DEV_INTR_PKT_DATA;
	}

	if (intr64 & (LIO_CN23XX_INTR_DMA0_FORCE))
		oct->int_status |= LIO_DEV_INTR_DMA0_FORCE;

	if (intr64 & (LIO_CN23XX_INTR_DMA1_FORCE))
		oct->int_status |= LIO_DEV_INTR_DMA1_FORCE;

	/* Clear the current interrupts */
	lio_write_csr64(oct, cn23xx->intr_sum_reg64, intr64);
}

static void
lio_cn23xx_pf_bar1_idx_setup(struct octeon_device *oct, uint64_t core_addr,
			     uint32_t idx, int valid)
{
	volatile uint64_t	bar1;
	uint64_t		reg_adr;

	if (!valid) {
		reg_adr = lio_pci_readq(oct,
				LIO_CN23XX_PEM_BAR1_INDEX_REG(oct->pcie_port,
							      idx));
		bar1 = reg_adr;
		lio_pci_writeq(oct, (bar1 & 0xFFFFFFFEULL),
			       LIO_CN23XX_PEM_BAR1_INDEX_REG(oct->pcie_port,
							     idx));
		reg_adr = lio_pci_readq(oct,
				LIO_CN23XX_PEM_BAR1_INDEX_REG(oct->pcie_port,
							      idx));
		bar1 = reg_adr;
		return;
	}
	/*
	 *  The PEM(0..3)_BAR1_INDEX(0..15)[ADDR_IDX]<23:4> stores
	 *  bits <41:22> of the Core Addr
	 */
	lio_pci_writeq(oct, (((core_addr >> 22) << 4) | LIO_PCI_BAR1_MASK),
		       LIO_CN23XX_PEM_BAR1_INDEX_REG(oct->pcie_port, idx));

	bar1 = lio_pci_readq(oct, LIO_CN23XX_PEM_BAR1_INDEX_REG(oct->pcie_port,
								idx));
}

static void
lio_cn23xx_pf_bar1_idx_write(struct octeon_device *oct, uint32_t idx,
			     uint32_t mask)
{

	lio_pci_writeq(oct, mask,
		       LIO_CN23XX_PEM_BAR1_INDEX_REG(oct->pcie_port, idx));
}

static uint32_t
lio_cn23xx_pf_bar1_idx_read(struct octeon_device *oct, uint32_t idx)
{

	return ((uint32_t)lio_pci_readq(oct,
				LIO_CN23XX_PEM_BAR1_INDEX_REG(oct->pcie_port,
							      idx)));
}

/* always call with lock held */
static uint32_t
lio_cn23xx_pf_update_read_index(struct lio_instr_queue *iq)
{
	struct octeon_device	*oct = iq->oct_dev;
	uint32_t	new_idx;
	uint32_t	last_done;
	uint32_t	pkt_in_done = lio_read_csr32(oct, iq->inst_cnt_reg);

	last_done = pkt_in_done - iq->pkt_in_done;
	iq->pkt_in_done = pkt_in_done;

	/*
	 * Modulo of the new index with the IQ size will give us
	 * the new index.  The iq->reset_instr_cnt is always zero for
	 * cn23xx, so no extra adjustments are needed.
	 */
	new_idx = (iq->octeon_read_index +
		   ((uint32_t)(last_done & LIO_CN23XX_PKT_IN_DONE_CNT_MASK))) %
	    iq->max_count;

	return (new_idx);
}

static void
lio_cn23xx_pf_enable_interrupt(struct octeon_device *oct, uint8_t intr_flag)
{
	struct lio_cn23xx_pf	*cn23xx = (struct lio_cn23xx_pf *)oct->chip;
	uint64_t		intr_val = 0;

	/* Divide the single write to multiple writes based on the flag. */
	/* Enable Interrupt */
	if (intr_flag == OCTEON_ALL_INTR) {
		lio_write_csr64(oct, cn23xx->intr_enb_reg64,
				cn23xx->intr_mask64);
	} else if (intr_flag & OCTEON_OUTPUT_INTR) {
		intr_val = lio_read_csr64(oct, cn23xx->intr_enb_reg64);
		intr_val |= LIO_CN23XX_INTR_PKT_DATA;
		lio_write_csr64(oct, cn23xx->intr_enb_reg64, intr_val);
	}
}

static void
lio_cn23xx_pf_disable_interrupt(struct octeon_device *oct, uint8_t intr_flag)
{
	struct lio_cn23xx_pf	*cn23xx = (struct lio_cn23xx_pf *)oct->chip;
	uint64_t		intr_val = 0;

	/* Disable Interrupts */
	if (intr_flag == OCTEON_ALL_INTR) {
		lio_write_csr64(oct, cn23xx->intr_enb_reg64, 0);
	} else if (intr_flag & OCTEON_OUTPUT_INTR) {
		intr_val = lio_read_csr64(oct, cn23xx->intr_enb_reg64);
		intr_val &= ~LIO_CN23XX_INTR_PKT_DATA;
		lio_write_csr64(oct, cn23xx->intr_enb_reg64, intr_val);
	}
}

static void
lio_cn23xx_pf_get_pcie_qlmport(struct octeon_device *oct)
{
	oct->pcie_port = (lio_read_csr32(oct,
					 LIO_CN23XX_SLI_MAC_NUMBER)) & 0xff;

	lio_dev_dbg(oct, "CN23xx uses PCIE Port %d\n",
		    oct->pcie_port);
}

static void
lio_cn23xx_pf_get_pf_num(struct octeon_device *oct)
{
	uint32_t	fdl_bit;

	/* Read Function Dependency Link reg to get the function number */
	fdl_bit = lio_read_pci_cfg(oct, LIO_CN23XX_PCIE_SRIOV_FDL);
	oct->pf_num = ((fdl_bit >> LIO_CN23XX_PCIE_SRIOV_FDL_BIT_POS) &
		       LIO_CN23XX_PCIE_SRIOV_FDL_MASK);
}

static void
lio_cn23xx_pf_setup_reg_address(struct octeon_device *oct)
{
	struct lio_cn23xx_pf	*cn23xx = (struct lio_cn23xx_pf *)oct->chip;

	oct->reg_list.pci_win_wr_addr = LIO_CN23XX_SLI_WIN_WR_ADDR64;

	oct->reg_list.pci_win_rd_addr_hi = LIO_CN23XX_SLI_WIN_RD_ADDR_HI;
	oct->reg_list.pci_win_rd_addr_lo = LIO_CN23XX_SLI_WIN_RD_ADDR64;
	oct->reg_list.pci_win_rd_addr = LIO_CN23XX_SLI_WIN_RD_ADDR64;

	oct->reg_list.pci_win_wr_data_hi = LIO_CN23XX_SLI_WIN_WR_DATA_HI;
	oct->reg_list.pci_win_wr_data_lo = LIO_CN23XX_SLI_WIN_WR_DATA_LO;
	oct->reg_list.pci_win_wr_data = LIO_CN23XX_SLI_WIN_WR_DATA64;

	oct->reg_list.pci_win_rd_data = LIO_CN23XX_SLI_WIN_RD_DATA64;

	lio_cn23xx_pf_get_pcie_qlmport(oct);

	cn23xx->intr_mask64 = LIO_CN23XX_INTR_MASK;
	if (!oct->msix_on)
		cn23xx->intr_mask64 |= LIO_CN23XX_INTR_PKT_TIME;

	cn23xx->intr_sum_reg64 =
	    LIO_CN23XX_SLI_MAC_PF_INT_SUM64(oct->pcie_port, oct->pf_num);
	cn23xx->intr_enb_reg64 =
	    LIO_CN23XX_SLI_MAC_PF_INT_ENB64(oct->pcie_port, oct->pf_num);
}

static int
lio_cn23xx_pf_sriov_config(struct octeon_device *oct)
{
	struct lio_cn23xx_pf	*cn23xx = (struct lio_cn23xx_pf *)oct->chip;
	uint32_t		num_pf_rings, total_rings, max_rings;
	cn23xx->conf = (struct lio_config *)lio_get_config_info(oct, LIO_23XX);

	max_rings = LIO_CN23XX_PF_MAX_RINGS;

	if (oct->sriov_info.num_pf_rings) {
		num_pf_rings = oct->sriov_info.num_pf_rings;
		if (num_pf_rings > max_rings) {
			num_pf_rings = min(mp_ncpus, max_rings);
			lio_dev_warn(oct, "num_queues_per_pf requested %u is more than available rings (%u). Reducing to %u\n",
				     oct->sriov_info.num_pf_rings,
				     max_rings, num_pf_rings);
		}
	} else {
#ifdef RSS
		num_pf_rings = min(rss_getnumbuckets(), mp_ncpus);
#else
		num_pf_rings = min(mp_ncpus, max_rings);
#endif

	}

	total_rings = num_pf_rings;
	oct->sriov_info.trs = total_rings;
	oct->sriov_info.pf_srn = total_rings - num_pf_rings;
	oct->sriov_info.num_pf_rings = num_pf_rings;

	lio_dev_dbg(oct, "trs:%d pf_srn:%d num_pf_rings:%d\n",
		    oct->sriov_info.trs, oct->sriov_info.pf_srn,
		    oct->sriov_info.num_pf_rings);

	return (0);
}

int
lio_cn23xx_pf_setup_device(struct octeon_device *oct)
{
	uint64_t	BAR0, BAR1;
	uint32_t	data32;

	data32 = lio_read_pci_cfg(oct, 0x10);
	BAR0 = (uint64_t)(data32 & ~0xf);
	data32 = lio_read_pci_cfg(oct, 0x14);
	BAR0 |= ((uint64_t)data32 << 32);
	data32 = lio_read_pci_cfg(oct, 0x18);
	BAR1 = (uint64_t)(data32 & ~0xf);
	data32 = lio_read_pci_cfg(oct, 0x1c);
	BAR1 |= ((uint64_t)data32 << 32);

	if (!BAR0 || !BAR1) {
		if (!BAR0)
			lio_dev_err(oct, "Device BAR0 unassigned\n");

		if (!BAR1)
			lio_dev_err(oct, "Device BAR1 unassigned\n");

		return (1);
	}

	if (lio_map_pci_barx(oct, 0))
		return (1);

	if (lio_map_pci_barx(oct, 1)) {
		lio_dev_err(oct, "%s CN23XX BAR1 map failed\n", __func__);
		lio_unmap_pci_barx(oct, 0);
		return (1);
	}

	lio_cn23xx_pf_get_pf_num(oct); 

	if (lio_cn23xx_pf_sriov_config(oct)) {
		lio_unmap_pci_barx(oct, 0);
		lio_unmap_pci_barx(oct, 1);
		return (1);
	}
	lio_write_csr64(oct, LIO_CN23XX_SLI_MAC_CREDIT_CNT,
			0x3F802080802080ULL);

	oct->fn_list.setup_iq_regs = lio_cn23xx_pf_setup_iq_regs;
	oct->fn_list.setup_oq_regs = lio_cn23xx_pf_setup_oq_regs;
	oct->fn_list.process_interrupt_regs = lio_cn23xx_pf_interrupt_handler;
	oct->fn_list.msix_interrupt_handler =
		lio_cn23xx_pf_msix_interrupt_handler;

	oct->fn_list.soft_reset = lio_cn23xx_pf_soft_reset;
	oct->fn_list.setup_device_regs = lio_cn23xx_pf_setup_device_regs;
	oct->fn_list.update_iq_read_idx = lio_cn23xx_pf_update_read_index;

	oct->fn_list.bar1_idx_setup = lio_cn23xx_pf_bar1_idx_setup;
	oct->fn_list.bar1_idx_write = lio_cn23xx_pf_bar1_idx_write;
	oct->fn_list.bar1_idx_read = lio_cn23xx_pf_bar1_idx_read;

	oct->fn_list.enable_interrupt = lio_cn23xx_pf_enable_interrupt;
	oct->fn_list.disable_interrupt = lio_cn23xx_pf_disable_interrupt;

	oct->fn_list.enable_io_queues = lio_cn23xx_pf_enable_io_queues;
	oct->fn_list.disable_io_queues = lio_cn23xx_pf_disable_io_queues;

	lio_cn23xx_pf_setup_reg_address(oct);

	oct->coproc_clock_rate = 1000000ULL *
		lio_cn23xx_pf_coprocessor_clock(oct);

	return (0);
}

int
lio_cn23xx_pf_fw_loaded(struct octeon_device *oct)
{
	uint64_t	val;

	val = lio_read_csr64(oct, LIO_CN23XX_SLI_SCRATCH2);
	return ((val >> SCR2_BIT_FW_LOADED) & 1ULL);
}

