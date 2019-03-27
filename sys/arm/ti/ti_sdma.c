/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011
 *	Ben Gray <ben.r.gray@gmail.com>.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/interrupt.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/timetc.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/ti/ti_cpuid.h>
#include <arm/ti/ti_prcm.h>
#include <arm/ti/ti_sdma.h>
#include <arm/ti/ti_sdmareg.h>

/**
 *	Kernel functions for using the DMA controller
 *
 *
 *	DMA TRANSFERS:
 *	A DMA transfer block consists of a number of frames (FN). Each frame
 *	consists of a number of elements, and each element can have a size of 8, 16,
 *	or 32 bits.
 *
 *	OMAP44xx and newer chips support linked list (aka scatter gather) transfers,
 *	where a linked list of source/destination pairs can be placed in memory
 *	for the H/W to process.  Earlier chips only allowed you to chain multiple
 *	channels together.  However currently this linked list feature is not
 *	supported by the driver.
 *
 */

/**
 *	Data structure per DMA channel.
 *
 *
 */
struct ti_sdma_channel {

	/* 
	 * The configuration registers for the given channel, these are modified
	 * by the set functions and only written to the actual registers when a
	 * transaction is started.
	 */
	uint32_t		reg_csdp;
	uint32_t		reg_ccr;
	uint32_t		reg_cicr;

	/* Set when one of the configuration registers above change */
	uint32_t		need_reg_write;

	/* Callback function used when an interrupt is tripped on the given channel */
	void (*callback)(unsigned int ch, uint32_t ch_status, void *data);

	/* Callback data passed in the callback ... duh */
	void*			callback_data;

};

/**
 *	DMA driver context, allocated and stored globally, this driver is not
 *	intetned to ever be unloaded (see ti_sdma_sc).
 *
 */
struct ti_sdma_softc {
	device_t		sc_dev;
	struct resource*	sc_irq_res;
	struct resource*	sc_mem_res;

	/* 
	 * I guess in theory we should have a mutex per DMA channel for register
	 * modifications. But since we know we are never going to be run on a SMP
	 * system, we can use just the single lock for all channels.
	 */
	struct mtx		sc_mtx;

	/* Stores the H/W revision read from the registers */
	uint32_t		sc_hw_rev;

	/* 
	 * Bits in the sc_active_channels data field indicate if the channel has
	 * been activated.
	 */
	uint32_t		sc_active_channels;

	struct ti_sdma_channel sc_channel[NUM_DMA_CHANNELS];

};

static struct ti_sdma_softc *ti_sdma_sc = NULL;

/**
 *	Macros for driver mutex locking
 */
#define TI_SDMA_LOCK(_sc)             mtx_lock_spin(&(_sc)->sc_mtx)
#define TI_SDMA_UNLOCK(_sc)           mtx_unlock_spin(&(_sc)->sc_mtx)
#define TI_SDMA_LOCK_INIT(_sc) \
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->sc_dev), \
	         "ti_sdma", MTX_SPIN)
#define TI_SDMA_LOCK_DESTROY(_sc)     mtx_destroy(&_sc->sc_mtx);
#define TI_SDMA_ASSERT_LOCKED(_sc)    mtx_assert(&_sc->sc_mtx, MA_OWNED);
#define TI_SDMA_ASSERT_UNLOCKED(_sc)  mtx_assert(&_sc->sc_mtx, MA_NOTOWNED);

/**
 *	Function prototypes
 *
 */
static void ti_sdma_intr(void *);

/**
 *	ti_sdma_read_4 - reads a 32-bit value from one of the DMA registers
 *	@sc: DMA device context
 *	@off: The offset of a register from the DMA register address range
 *
 *
 *	RETURNS:
 *	32-bit value read from the register.
 */
static inline uint32_t
ti_sdma_read_4(struct ti_sdma_softc *sc, bus_size_t off)
{
	return bus_read_4(sc->sc_mem_res, off);
}

/**
 *	ti_sdma_write_4 - writes a 32-bit value to one of the DMA registers
 *	@sc: DMA device context
 *	@off: The offset of a register from the DMA register address range
 *
 *
 *	RETURNS:
 *	32-bit value read from the register.
 */
static inline void
ti_sdma_write_4(struct ti_sdma_softc *sc, bus_size_t off, uint32_t val)
{
	bus_write_4(sc->sc_mem_res, off, val);
}

/**
 *	ti_sdma_is_omap3_rev - returns true if H/W is from OMAP3 series
 *	@sc: DMA device context
 *
 */
static inline int
ti_sdma_is_omap3_rev(struct ti_sdma_softc *sc)
{
	return (sc->sc_hw_rev == DMA4_OMAP3_REV);
}

/**
 *	ti_sdma_is_omap4_rev - returns true if H/W is from OMAP4 series
 *	@sc: DMA device context
 *
 */
static inline int
ti_sdma_is_omap4_rev(struct ti_sdma_softc *sc)
{
	return (sc->sc_hw_rev == DMA4_OMAP4_REV);
}

/**
 *	ti_sdma_intr - interrupt handler for all 4 DMA IRQs
 *	@arg: ignored
 *
 *	Called when any of the four DMA IRQs are triggered.
 *
 *	LOCKING:
 *	DMA registers protected by internal mutex
 *
 *	RETURNS:
 *	nothing
 */
static void
ti_sdma_intr(void *arg)
{
	struct ti_sdma_softc *sc = ti_sdma_sc;
	uint32_t intr;
	uint32_t csr;
	unsigned int ch, j;
	struct ti_sdma_channel* channel;

	TI_SDMA_LOCK(sc);

	for (j = 0; j < NUM_DMA_IRQS; j++) {

		/* Get the flag interrupts (enabled) */
		intr = ti_sdma_read_4(sc, DMA4_IRQSTATUS_L(j));
		intr &= ti_sdma_read_4(sc, DMA4_IRQENABLE_L(j));
		if (intr == 0x00000000)
			continue;

		/* Loop through checking the status bits */
		for (ch = 0; ch < NUM_DMA_CHANNELS; ch++) {
			if (intr & (1 << ch)) {
				channel = &sc->sc_channel[ch];

				/* Read the CSR regsiter and verify we don't have a spurious IRQ */
				csr = ti_sdma_read_4(sc, DMA4_CSR(ch));
				if (csr == 0) {
					device_printf(sc->sc_dev, "Spurious DMA IRQ for channel "
					              "%d\n", ch);
					continue;
				}

				/* Sanity check this channel is active */
				if ((sc->sc_active_channels & (1 << ch)) == 0) {
					device_printf(sc->sc_dev, "IRQ %d for a non-activated "
					              "channel %d\n", j, ch);
					continue;
				}

				/* Check the status error codes */
				if (csr & DMA4_CSR_DROP)
					device_printf(sc->sc_dev, "Synchronization event drop "
					              "occurred during the transfer on channel %u\n",
								  ch);
				if (csr & DMA4_CSR_SECURE_ERR)
					device_printf(sc->sc_dev, "Secure transaction error event "
					              "on channel %u\n", ch);
				if (csr & DMA4_CSR_MISALIGNED_ADRS_ERR)
					device_printf(sc->sc_dev, "Misaligned address error event "
					              "on channel %u\n", ch);
				if (csr & DMA4_CSR_TRANS_ERR) {
					device_printf(sc->sc_dev, "Transaction error event on "
					              "channel %u\n", ch);
					/* 
					 * Apparently according to linux code, there is an errata
					 * that says the channel is not disabled upon this error.
					 * They explicitly disable the channel here .. since I
					 * haven't seen the errata, I'm going to ignore for now.
					 */
				}

				/* Clear the status flags for the IRQ */
				ti_sdma_write_4(sc, DMA4_CSR(ch), DMA4_CSR_CLEAR_MASK);
				ti_sdma_write_4(sc, DMA4_IRQSTATUS_L(j), (1 << ch));

				/* Call the callback for the given channel */
				if (channel->callback)
					channel->callback(ch, csr, channel->callback_data);
			}
		}
	}

	TI_SDMA_UNLOCK(sc);

	return;
}

/**
 *	ti_sdma_activate_channel - activates a DMA channel
 *	@ch: upon return contains the channel allocated
 *	@callback: a callback function to associate with the channel
 *	@data: optional data supplied when the callback is called
 *
 *	Simply activates a channel be enabling and writing default values to the
 *	channel's register set.  It doesn't start a transaction, just populates the
 *	internal data structures and sets defaults.
 *
 *	Note this function doesn't enable interrupts, for that you need to call
 *	ti_sdma_enable_channel_irq(). If not using IRQ to detect the end of the
 *	transfer, you can use ti_sdma_status_poll() to detect a change in the
 *	status.
 *
 *	A channel must be activated before any of the other DMA functions can be
 *	called on it.
 *
 *	LOCKING:
 *	DMA registers protected by internal mutex
 *
 *	RETURNS:
 *	0 on success, otherwise an error code
 */
int
ti_sdma_activate_channel(unsigned int *ch,
                          void (*callback)(unsigned int ch, uint32_t status, void *data),
                          void *data)
{
	struct ti_sdma_softc *sc = ti_sdma_sc;
	struct ti_sdma_channel *channel = NULL;
	uint32_t addr;
	unsigned int i;

	/* Sanity check */
	if (sc == NULL)
		return (ENOMEM);

	if (ch == NULL)
		return (EINVAL);

	TI_SDMA_LOCK(sc);

	/* Check to see if all channels are in use */
	if (sc->sc_active_channels == 0xffffffff) {
		TI_SDMA_UNLOCK(sc);
		return (ENOMEM);
	}

	/* Find the first non-active channel */
	for (i = 0; i < NUM_DMA_CHANNELS; i++) {
		if (!(sc->sc_active_channels & (0x1 << i))) {
			sc->sc_active_channels |= (0x1 << i);
			*ch = i;
			break;
		}
	}

	/* Get the channel struct and populate the fields */
	channel = &sc->sc_channel[*ch];

	channel->callback = callback;
	channel->callback_data = data;

	channel->need_reg_write = 1;

	/* Set the default configuration for the DMA channel */
	channel->reg_csdp = DMA4_CSDP_DATA_TYPE(0x2)
		| DMA4_CSDP_SRC_BURST_MODE(0)
		| DMA4_CSDP_DST_BURST_MODE(0)
		| DMA4_CSDP_SRC_ENDIANISM(0)
		| DMA4_CSDP_DST_ENDIANISM(0)
		| DMA4_CSDP_WRITE_MODE(0)
		| DMA4_CSDP_SRC_PACKED(0)
		| DMA4_CSDP_DST_PACKED(0);

	channel->reg_ccr = DMA4_CCR_DST_ADDRESS_MODE(1)
		| DMA4_CCR_SRC_ADDRESS_MODE(1)
		| DMA4_CCR_READ_PRIORITY(0)
		| DMA4_CCR_WRITE_PRIORITY(0)
		| DMA4_CCR_SYNC_TRIGGER(0)
		| DMA4_CCR_FRAME_SYNC(0)
		| DMA4_CCR_BLOCK_SYNC(0);

	channel->reg_cicr = DMA4_CICR_TRANS_ERR_IE
		| DMA4_CICR_SECURE_ERR_IE
		| DMA4_CICR_SUPERVISOR_ERR_IE
		| DMA4_CICR_MISALIGNED_ADRS_ERR_IE;

	/* Clear all the channel registers, this should abort any transaction */
	for (addr = DMA4_CCR(*ch); addr <= DMA4_COLOR(*ch); addr += 4)
		ti_sdma_write_4(sc, addr, 0x00000000);

	TI_SDMA_UNLOCK(sc);

	return 0;
}

/**
 *	ti_sdma_deactivate_channel - deactivates a channel
 *	@ch: the channel to deactivate
 *
 *
 *
 *	LOCKING:
 *	DMA registers protected by internal mutex
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
int
ti_sdma_deactivate_channel(unsigned int ch)
{
	struct ti_sdma_softc *sc = ti_sdma_sc;
	unsigned int j;
	unsigned int addr;

	/* Sanity check */
	if (sc == NULL)
		return (ENOMEM);

	TI_SDMA_LOCK(sc);

	/* First check if the channel is currently active */
	if ((sc->sc_active_channels & (1 << ch)) == 0) {
		TI_SDMA_UNLOCK(sc);
		return (EBUSY);
	}

	/* Mark the channel as inactive */
	sc->sc_active_channels &= ~(1 << ch);

	/* Disable all DMA interrupts for the channel. */
	ti_sdma_write_4(sc, DMA4_CICR(ch), 0);

	/* Make sure the DMA transfer is stopped. */
	ti_sdma_write_4(sc, DMA4_CCR(ch), 0);

	/* Clear the CSR register and IRQ status register */
	ti_sdma_write_4(sc, DMA4_CSR(ch), DMA4_CSR_CLEAR_MASK);
	for (j = 0; j < NUM_DMA_IRQS; j++) {
		ti_sdma_write_4(sc, DMA4_IRQSTATUS_L(j), (1 << ch));
	}

	/* Clear all the channel registers, this should abort any transaction */
	for (addr = DMA4_CCR(ch); addr <= DMA4_COLOR(ch); addr += 4)
		ti_sdma_write_4(sc, addr, 0x00000000);

	TI_SDMA_UNLOCK(sc);

	return 0;
}

/**
 *	ti_sdma_disable_channel_irq - disables IRQ's on the given channel
 *	@ch: the channel to disable IRQ's on
 *
 *	Disable interrupt generation for the given channel.
 *
 *	LOCKING:
 *	DMA registers protected by internal mutex
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
int
ti_sdma_disable_channel_irq(unsigned int ch)
{
	struct ti_sdma_softc *sc = ti_sdma_sc;
	uint32_t irq_enable;
	unsigned int j;

	/* Sanity check */
	if (sc == NULL)
		return (ENOMEM);

	TI_SDMA_LOCK(sc);

	if ((sc->sc_active_channels & (1 << ch)) == 0) {
		TI_SDMA_UNLOCK(sc);
		return (EINVAL);
	}

	/* Disable all the individual error conditions */
	sc->sc_channel[ch].reg_cicr = 0x0000;
	ti_sdma_write_4(sc, DMA4_CICR(ch), 0x0000);

	/* Disable the channel interrupt enable */
	for (j = 0; j < NUM_DMA_IRQS; j++) {
		irq_enable = ti_sdma_read_4(sc, DMA4_IRQENABLE_L(j));
		irq_enable &= ~(1 << ch);

		ti_sdma_write_4(sc, DMA4_IRQENABLE_L(j), irq_enable);
	}

	/* Indicate the registers need to be rewritten on the next transaction */
	sc->sc_channel[ch].need_reg_write = 1;

	TI_SDMA_UNLOCK(sc);

	return (0);
}

/**
 *	ti_sdma_disable_channel_irq - enables IRQ's on the given channel
 *	@ch: the channel to enable IRQ's on
 *	@flags: bitmask of interrupt types to enable
 *
 *	Flags can be a bitmask of the following options:
 *		DMA_IRQ_FLAG_DROP
 *		DMA_IRQ_FLAG_HALF_FRAME_COMPL
 *		DMA_IRQ_FLAG_FRAME_COMPL
 *		DMA_IRQ_FLAG_START_LAST_FRAME
 *		DMA_IRQ_FLAG_BLOCK_COMPL
 *		DMA_IRQ_FLAG_ENDOF_PKT
 *		DMA_IRQ_FLAG_DRAIN
 *
 *
 *	LOCKING:
 *	DMA registers protected by internal mutex
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
int
ti_sdma_enable_channel_irq(unsigned int ch, uint32_t flags)
{
	struct ti_sdma_softc *sc = ti_sdma_sc;
	uint32_t irq_enable;

	/* Sanity check */
	if (sc == NULL)
		return (ENOMEM);

	TI_SDMA_LOCK(sc);

	if ((sc->sc_active_channels & (1 << ch)) == 0) {
		TI_SDMA_UNLOCK(sc);
		return (EINVAL);
	}

	/* Always enable the error interrupts if we have interrupts enabled */
	flags |= DMA4_CICR_TRANS_ERR_IE | DMA4_CICR_SECURE_ERR_IE |
	         DMA4_CICR_SUPERVISOR_ERR_IE | DMA4_CICR_MISALIGNED_ADRS_ERR_IE;

	sc->sc_channel[ch].reg_cicr = flags;

	/* Write the values to the register */
	ti_sdma_write_4(sc, DMA4_CICR(ch), flags);

	/* Enable the channel interrupt enable */
	irq_enable = ti_sdma_read_4(sc, DMA4_IRQENABLE_L(0));
	irq_enable |= (1 << ch);

	ti_sdma_write_4(sc, DMA4_IRQENABLE_L(0), irq_enable);

	/* Indicate the registers need to be rewritten on the next transaction */
	sc->sc_channel[ch].need_reg_write = 1;

	TI_SDMA_UNLOCK(sc);

	return (0);
}

/**
 *	ti_sdma_get_channel_status - returns the status of a given channel
 *	@ch: the channel number to get the status of
 *	@status: upon return will contain the status bitmask, see below for possible
 *	         values.
 *
 *	      DMA_STATUS_DROP
 *	      DMA_STATUS_HALF
 *	      DMA_STATUS_FRAME
 *	      DMA_STATUS_LAST
 *	      DMA_STATUS_BLOCK
 *	      DMA_STATUS_SYNC
 *	      DMA_STATUS_PKT
 *	      DMA_STATUS_TRANS_ERR
 *	      DMA_STATUS_SECURE_ERR
 *	      DMA_STATUS_SUPERVISOR_ERR
 *	      DMA_STATUS_MISALIGNED_ADRS_ERR
 *	      DMA_STATUS_DRAIN_END
 *
 *
 *	LOCKING:
 *	DMA registers protected by internal mutex
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
int
ti_sdma_get_channel_status(unsigned int ch, uint32_t *status)
{
	struct ti_sdma_softc *sc = ti_sdma_sc;
	uint32_t csr;

	/* Sanity check */
	if (sc == NULL)
		return (ENOMEM);

	TI_SDMA_LOCK(sc);

	if ((sc->sc_active_channels & (1 << ch)) == 0) {
		TI_SDMA_UNLOCK(sc);
		return (EINVAL);
	}

	TI_SDMA_UNLOCK(sc);

	csr = ti_sdma_read_4(sc, DMA4_CSR(ch));

	if (status != NULL)
		*status = csr;

	return (0);
}

/**
 *	ti_sdma_start_xfer - starts a DMA transfer
 *	@ch: the channel number to set the endianness of
 *	@src_paddr: the source phsyical address
 *	@dst_paddr: the destination phsyical address
 *	@frmcnt: the number of frames per block
 *	@elmcnt: the number of elements in a frame, an element is either an 8, 16
 *           or 32-bit value as defined by ti_sdma_set_xfer_burst()
 *
 *
 *	LOCKING:
 *	DMA registers protected by internal mutex
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
int
ti_sdma_start_xfer(unsigned int ch, unsigned int src_paddr,
                    unsigned long dst_paddr,
                    unsigned int frmcnt, unsigned int elmcnt)
{
	struct ti_sdma_softc *sc = ti_sdma_sc;
	struct ti_sdma_channel *channel;
	uint32_t ccr;

	/* Sanity check */
	if (sc == NULL)
		return (ENOMEM);

	TI_SDMA_LOCK(sc);

	if ((sc->sc_active_channels & (1 << ch)) == 0) {
		TI_SDMA_UNLOCK(sc);
		return (EINVAL);
	}

	channel = &sc->sc_channel[ch];

	/* a) Write the CSDP register */
	ti_sdma_write_4(sc, DMA4_CSDP(ch),
	    channel->reg_csdp | DMA4_CSDP_WRITE_MODE(1));

	/* b) Set the number of element per frame CEN[23:0] */
	ti_sdma_write_4(sc, DMA4_CEN(ch), elmcnt);

	/* c) Set the number of frame per block CFN[15:0] */
	ti_sdma_write_4(sc, DMA4_CFN(ch), frmcnt);

	/* d) Set the Source/dest start address index CSSA[31:0]/CDSA[31:0] */
	ti_sdma_write_4(sc, DMA4_CSSA(ch), src_paddr);
	ti_sdma_write_4(sc, DMA4_CDSA(ch), dst_paddr);

	/* e) Write the CCR register */
	ti_sdma_write_4(sc, DMA4_CCR(ch), channel->reg_ccr);

	/* f)  - Set the source element index increment CSEI[15:0] */
	ti_sdma_write_4(sc, DMA4_CSE(ch), 0x0001);

	/*     - Set the source frame index increment CSFI[15:0] */
	ti_sdma_write_4(sc, DMA4_CSF(ch), 0x0001);

	/*     - Set the destination element index increment CDEI[15:0]*/
	ti_sdma_write_4(sc, DMA4_CDE(ch), 0x0001);

	/* - Set the destination frame index increment CDFI[31:0] */
	ti_sdma_write_4(sc, DMA4_CDF(ch), 0x0001);

	/* Clear the status register */
	ti_sdma_write_4(sc, DMA4_CSR(ch), 0x1FFE);

	/* Write the start-bit and away we go */
	ccr = ti_sdma_read_4(sc, DMA4_CCR(ch));
	ccr |= (1 << 7);
	ti_sdma_write_4(sc, DMA4_CCR(ch), ccr);

	/* Clear the reg write flag */
	channel->need_reg_write = 0;

	TI_SDMA_UNLOCK(sc);

	return (0);
}

/**
 *	ti_sdma_start_xfer_packet - starts a packet DMA transfer
 *	@ch: the channel number to use for the transfer
 *	@src_paddr: the source physical address
 *	@dst_paddr: the destination physical address
 *	@frmcnt: the number of frames to transfer
 *	@elmcnt: the number of elements in a frame, an element is either an 8, 16
 *           or 32-bit value as defined by ti_sdma_set_xfer_burst()
 *	@pktsize: the number of elements in each transfer packet
 *
 *	The @frmcnt and @elmcnt define the overall number of bytes to transfer,
 *	typically @frmcnt is 1 and @elmcnt contains the total number of elements.
 *	@pktsize is the size of each individual packet, there might be multiple
 *	packets per transfer.  i.e. for the following with element size of 32-bits
 *
 *		frmcnt = 1, elmcnt = 512, pktsize = 128
 *
 *	       Total transfer bytes = 1 * 512 = 512 elements or 2048 bytes
 *	       Packets transferred   = 128 / 512 = 4
 *
 *
 *	LOCKING:
 *	DMA registers protected by internal mutex
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
int
ti_sdma_start_xfer_packet(unsigned int ch, unsigned int src_paddr,
                           unsigned long dst_paddr, unsigned int frmcnt,
                           unsigned int elmcnt, unsigned int pktsize)
{
	struct ti_sdma_softc *sc = ti_sdma_sc;
	struct ti_sdma_channel *channel;
	uint32_t ccr;

	/* Sanity check */
	if (sc == NULL)
		return (ENOMEM);

	TI_SDMA_LOCK(sc);

	if ((sc->sc_active_channels & (1 << ch)) == 0) {
		TI_SDMA_UNLOCK(sc);
		return (EINVAL);
	}

	channel = &sc->sc_channel[ch];

	/* a) Write the CSDP register */
	if (channel->need_reg_write)
		ti_sdma_write_4(sc, DMA4_CSDP(ch),
		    channel->reg_csdp | DMA4_CSDP_WRITE_MODE(1));

	/* b) Set the number of elements to transfer CEN[23:0] */
	ti_sdma_write_4(sc, DMA4_CEN(ch), elmcnt);

	/* c) Set the number of frames to transfer CFN[15:0] */
	ti_sdma_write_4(sc, DMA4_CFN(ch), frmcnt);

	/* d) Set the Source/dest start address index CSSA[31:0]/CDSA[31:0] */
	ti_sdma_write_4(sc, DMA4_CSSA(ch), src_paddr);
	ti_sdma_write_4(sc, DMA4_CDSA(ch), dst_paddr);

	/* e) Write the CCR register */
	ti_sdma_write_4(sc, DMA4_CCR(ch),
	    channel->reg_ccr | DMA4_CCR_PACKET_TRANS);

	/* f)  - Set the source element index increment CSEI[15:0] */
	ti_sdma_write_4(sc, DMA4_CSE(ch), 0x0001);

	/*     - Set the packet size, this is dependent on the sync source */
	if (channel->reg_ccr & DMA4_CCR_SEL_SRC_DST_SYNC(1))
		ti_sdma_write_4(sc, DMA4_CSF(ch), pktsize);
	else
		ti_sdma_write_4(sc, DMA4_CDF(ch), pktsize);

	/* - Set the destination frame index increment CDFI[31:0] */
	ti_sdma_write_4(sc, DMA4_CDE(ch), 0x0001);

	/* Clear the status register */
	ti_sdma_write_4(sc, DMA4_CSR(ch), 0x1FFE);

	/* Write the start-bit and away we go */
	ccr = ti_sdma_read_4(sc, DMA4_CCR(ch));
	ccr |= (1 << 7);
	ti_sdma_write_4(sc, DMA4_CCR(ch), ccr);

	/* Clear the reg write flag */
	channel->need_reg_write = 0;

	TI_SDMA_UNLOCK(sc);

	return (0);
}

/**
 *	ti_sdma_stop_xfer - stops any currently active transfers
 *	@ch: the channel number to set the endianness of
 *
 *	This function call is effectively a NOP if no transaction is in progress.
 *
 *	LOCKING:
 *	DMA registers protected by internal mutex
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
int
ti_sdma_stop_xfer(unsigned int ch)
{
	struct ti_sdma_softc *sc = ti_sdma_sc;
	unsigned int j;

	/* Sanity check */
	if (sc == NULL)
		return (ENOMEM);

	TI_SDMA_LOCK(sc);

	if ((sc->sc_active_channels & (1 << ch)) == 0) {
		TI_SDMA_UNLOCK(sc);
		return (EINVAL);
	}

	/* Disable all DMA interrupts for the channel. */
	ti_sdma_write_4(sc, DMA4_CICR(ch), 0);

	/* Make sure the DMA transfer is stopped. */
	ti_sdma_write_4(sc, DMA4_CCR(ch), 0);

	/* Clear the CSR register and IRQ status register */
	ti_sdma_write_4(sc, DMA4_CSR(ch), DMA4_CSR_CLEAR_MASK);
	for (j = 0; j < NUM_DMA_IRQS; j++) {
		ti_sdma_write_4(sc, DMA4_IRQSTATUS_L(j), (1 << ch));
	}

	/* Configuration registers need to be re-written on the next xfer */
	sc->sc_channel[ch].need_reg_write = 1;

	TI_SDMA_UNLOCK(sc);

	return (0);
}

/**
 *	ti_sdma_set_xfer_endianess - sets the endianness of subsequent transfers
 *	@ch: the channel number to set the endianness of
 *	@src: the source endianness (either DMA_ENDIAN_LITTLE or DMA_ENDIAN_BIG)
 *	@dst: the destination endianness (either DMA_ENDIAN_LITTLE or DMA_ENDIAN_BIG)
 *
 *
 *	LOCKING:
 *	DMA registers protected by internal mutex
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
int
ti_sdma_set_xfer_endianess(unsigned int ch, unsigned int src, unsigned int dst)
{
	struct ti_sdma_softc *sc = ti_sdma_sc;

	/* Sanity check */
	if (sc == NULL)
		return (ENOMEM);

	TI_SDMA_LOCK(sc);

	if ((sc->sc_active_channels & (1 << ch)) == 0) {
		TI_SDMA_UNLOCK(sc);
		return (EINVAL);
	}

	sc->sc_channel[ch].reg_csdp &= ~DMA4_CSDP_SRC_ENDIANISM(1);
	sc->sc_channel[ch].reg_csdp |= DMA4_CSDP_SRC_ENDIANISM(src);

	sc->sc_channel[ch].reg_csdp &= ~DMA4_CSDP_DST_ENDIANISM(1);
	sc->sc_channel[ch].reg_csdp |= DMA4_CSDP_DST_ENDIANISM(dst);

	sc->sc_channel[ch].need_reg_write = 1;

	TI_SDMA_UNLOCK(sc);

	return 0;
}

/**
 *	ti_sdma_set_xfer_burst - sets the source and destination element size
 *	@ch: the channel number to set the burst settings of
 *	@src: the source endianness (either DMA_BURST_NONE, DMA_BURST_16, DMA_BURST_32
 *	      or DMA_BURST_64)
 *	@dst: the destination endianness (either DMA_BURST_NONE, DMA_BURST_16,
 *	      DMA_BURST_32 or DMA_BURST_64)
 *
 *	This function sets the size of the elements for all subsequent transfers.
 *
 *	LOCKING:
 *	DMA registers protected by internal mutex
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
int
ti_sdma_set_xfer_burst(unsigned int ch, unsigned int src, unsigned int dst)
{
	struct ti_sdma_softc *sc = ti_sdma_sc;

	/* Sanity check */
	if (sc == NULL)
		return (ENOMEM);

	TI_SDMA_LOCK(sc);

	if ((sc->sc_active_channels & (1 << ch)) == 0) {
		TI_SDMA_UNLOCK(sc);
		return (EINVAL);
	}

	sc->sc_channel[ch].reg_csdp &= ~DMA4_CSDP_SRC_BURST_MODE(0x3);
	sc->sc_channel[ch].reg_csdp |= DMA4_CSDP_SRC_BURST_MODE(src);

	sc->sc_channel[ch].reg_csdp &= ~DMA4_CSDP_DST_BURST_MODE(0x3);
	sc->sc_channel[ch].reg_csdp |= DMA4_CSDP_DST_BURST_MODE(dst);

	sc->sc_channel[ch].need_reg_write = 1;

	TI_SDMA_UNLOCK(sc);

	return 0;
}

/**
 *	ti_sdma_set_xfer_data_type - driver attach function
 *	@ch: the channel number to set the endianness of
 *	@type: the xfer data type (either DMA_DATA_8BITS_SCALAR, DMA_DATA_16BITS_SCALAR
 *	       or DMA_DATA_32BITS_SCALAR)
 *
 *
 *	LOCKING:
 *	DMA registers protected by internal mutex
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
int
ti_sdma_set_xfer_data_type(unsigned int ch, unsigned int type)
{
	struct ti_sdma_softc *sc = ti_sdma_sc;

	/* Sanity check */
	if (sc == NULL)
		return (ENOMEM);

	TI_SDMA_LOCK(sc);

	if ((sc->sc_active_channels & (1 << ch)) == 0) {
		TI_SDMA_UNLOCK(sc);
		return (EINVAL);
	}

	sc->sc_channel[ch].reg_csdp &= ~DMA4_CSDP_DATA_TYPE(0x3);
	sc->sc_channel[ch].reg_csdp |= DMA4_CSDP_DATA_TYPE(type);

	sc->sc_channel[ch].need_reg_write = 1;

	TI_SDMA_UNLOCK(sc);

	return 0;
}

/**
 *	ti_sdma_set_callback - driver attach function
 *	@dev: dma device handle
 *
 *
 *
 *	LOCKING:
 *	DMA registers protected by internal mutex
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
int
ti_sdma_set_callback(unsigned int ch,
                      void (*callback)(unsigned int ch, uint32_t status, void *data),
                      void *data)
{
	struct ti_sdma_softc *sc = ti_sdma_sc;

	/* Sanity check */
	if (sc == NULL)
		return (ENOMEM);

	TI_SDMA_LOCK(sc);

	if ((sc->sc_active_channels & (1 << ch)) == 0) {
		TI_SDMA_UNLOCK(sc);
		return (EINVAL);
	}

	sc->sc_channel[ch].callback = callback;
	sc->sc_channel[ch].callback_data = data;

	sc->sc_channel[ch].need_reg_write = 1;

	TI_SDMA_UNLOCK(sc);

	return 0;
}

/**
 *	ti_sdma_sync_params - sets channel sync settings
 *	@ch: the channel number to set the sync on
 *	@trigger: the number of the sync trigger, this depends on what other H/W
 *	          module is triggering/receiving the DMA transactions
 *	@mode: flags describing the sync mode to use, it may have one or more of
 *	          the following bits set; TI_SDMA_SYNC_FRAME,
 *	          TI_SDMA_SYNC_BLOCK, TI_SDMA_SYNC_TRIG_ON_SRC.
 *
 *
 *
 *	LOCKING:
 *	DMA registers protected by internal mutex
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
int
ti_sdma_sync_params(unsigned int ch, unsigned int trigger, unsigned int mode)
{
	struct ti_sdma_softc *sc = ti_sdma_sc;
	uint32_t ccr;

	/* Sanity check */
	if (sc == NULL)
		return (ENOMEM);

	TI_SDMA_LOCK(sc);

	if ((sc->sc_active_channels & (1 << ch)) == 0) {
		TI_SDMA_UNLOCK(sc);
		return (EINVAL);
	}

	ccr = sc->sc_channel[ch].reg_ccr;

	ccr &= ~DMA4_CCR_SYNC_TRIGGER(0x7F);
	ccr |= DMA4_CCR_SYNC_TRIGGER(trigger + 1);

	if (mode & TI_SDMA_SYNC_FRAME)
		ccr |= DMA4_CCR_FRAME_SYNC(1);
	else
		ccr &= ~DMA4_CCR_FRAME_SYNC(1);

	if (mode & TI_SDMA_SYNC_BLOCK)
		ccr |= DMA4_CCR_BLOCK_SYNC(1);
	else
		ccr &= ~DMA4_CCR_BLOCK_SYNC(1);

	if (mode & TI_SDMA_SYNC_TRIG_ON_SRC)
		ccr |= DMA4_CCR_SEL_SRC_DST_SYNC(1);
	else
		ccr &= ~DMA4_CCR_SEL_SRC_DST_SYNC(1);

	sc->sc_channel[ch].reg_ccr = ccr;

	sc->sc_channel[ch].need_reg_write = 1;

	TI_SDMA_UNLOCK(sc);

	return 0;
}

/**
 *	ti_sdma_set_addr_mode - driver attach function
 *	@ch: the channel number to set the endianness of
 *	@rd_mode: the xfer source addressing mode (either DMA_ADDR_CONSTANT,
 *	          DMA_ADDR_POST_INCREMENT, DMA_ADDR_SINGLE_INDEX or
 *	          DMA_ADDR_DOUBLE_INDEX)
 *	@wr_mode: the xfer destination addressing mode (either DMA_ADDR_CONSTANT,
 *	          DMA_ADDR_POST_INCREMENT, DMA_ADDR_SINGLE_INDEX or
 *	          DMA_ADDR_DOUBLE_INDEX)
 *
 *
 *	LOCKING:
 *	DMA registers protected by internal mutex
 *
 *	RETURNS:
 *	EH_HANDLED or EH_NOT_HANDLED
 */
int
ti_sdma_set_addr_mode(unsigned int ch, unsigned int src_mode,
                       unsigned int dst_mode)
{
	struct ti_sdma_softc *sc = ti_sdma_sc;
	uint32_t ccr;

	/* Sanity check */
	if (sc == NULL)
		return (ENOMEM);

	TI_SDMA_LOCK(sc);

	if ((sc->sc_active_channels & (1 << ch)) == 0) {
		TI_SDMA_UNLOCK(sc);
		return (EINVAL);
	}

	ccr = sc->sc_channel[ch].reg_ccr;

	ccr &= ~DMA4_CCR_SRC_ADDRESS_MODE(0x3);
	ccr |= DMA4_CCR_SRC_ADDRESS_MODE(src_mode);

	ccr &= ~DMA4_CCR_DST_ADDRESS_MODE(0x3);
	ccr |= DMA4_CCR_DST_ADDRESS_MODE(dst_mode);

	sc->sc_channel[ch].reg_ccr = ccr;

	sc->sc_channel[ch].need_reg_write = 1;

	TI_SDMA_UNLOCK(sc);

	return 0;
}

/**
 *	ti_sdma_probe - driver probe function
 *	@dev: dma device handle
 *
 *
 *
 *	RETURNS:
 *	Always returns 0.
 */
static int
ti_sdma_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ti,omap4430-sdma"))
		return (ENXIO);

	device_set_desc(dev, "TI sDMA Controller");
	return (0);
}

/**
 *	ti_sdma_attach - driver attach function
 *	@dev: dma device handle
 *
 *	Initialises memory mapping/pointers to the DMA register set and requests
 *	IRQs. This is effectively the setup function for the driver.
 *
 *	RETURNS:
 *	0 on success or a negative error code failure.
 */
static int
ti_sdma_attach(device_t dev)
{
	struct ti_sdma_softc *sc = device_get_softc(dev);
	unsigned int timeout;
	unsigned int i;
	int      rid;
	void    *ihl;
	int      err;

	/* Setup the basics */
	sc->sc_dev = dev;

	/* No channels active at the moment */
	sc->sc_active_channels = 0x00000000;

	/* Mutex to protect the shared data structures */
	TI_SDMA_LOCK_INIT(sc);

	/* Get the memory resource for the register mapping */
	rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->sc_mem_res == NULL)
		panic("%s: Cannot map registers", device_get_name(dev));

	/* Enable the interface and functional clocks */
	ti_prcm_clk_enable(SDMA_CLK);

	/* Read the sDMA revision register and sanity check it's known */
	sc->sc_hw_rev = ti_sdma_read_4(sc, DMA4_REVISION);
	device_printf(dev, "sDMA revision %08x\n", sc->sc_hw_rev);

	if (!ti_sdma_is_omap4_rev(sc) && !ti_sdma_is_omap3_rev(sc)) {
		device_printf(sc->sc_dev, "error - unknown sDMA H/W revision\n");
		return (EINVAL);
	}

	/* Disable all interrupts */
	for (i = 0; i < NUM_DMA_IRQS; i++) {
		ti_sdma_write_4(sc, DMA4_IRQENABLE_L(i), 0x00000000);
	}

	/* Soft-reset is only supported on pre-OMAP44xx devices */
	if (ti_sdma_is_omap3_rev(sc)) {

		/* Soft-reset */
		ti_sdma_write_4(sc, DMA4_OCP_SYSCONFIG, 0x0002);

		/* Set the timeout to 100ms*/
		timeout = (hz < 10) ? 1 : ((100 * hz) / 1000);

		/* Wait for DMA reset to complete */
		while ((ti_sdma_read_4(sc, DMA4_SYSSTATUS) & 0x1) == 0x0) {

			/* Sleep for a tick */
			pause("DMARESET", 1);

			if (timeout-- == 0) {
				device_printf(sc->sc_dev, "sDMA reset operation timed out\n");
				return (EINVAL);
			}
		}
	}

	/* 
	 * Install interrupt handlers for the for possible interrupts. Any channel
	 * can trip one of the four IRQs
	 */
	rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->sc_irq_res == NULL)
		panic("Unable to setup the dma irq handler.\n");

	err = bus_setup_intr(dev, sc->sc_irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, ti_sdma_intr, NULL, &ihl);
	if (err)
		panic("%s: Cannot register IRQ", device_get_name(dev));

	/* Store the DMA structure globally ... this driver should never be unloaded */
	ti_sdma_sc = sc;

	return (0);
}

static device_method_t ti_sdma_methods[] = {
	DEVMETHOD(device_probe, ti_sdma_probe),
	DEVMETHOD(device_attach, ti_sdma_attach),
	{0, 0},
};

static driver_t ti_sdma_driver = {
	"ti_sdma",
	ti_sdma_methods,
	sizeof(struct ti_sdma_softc),
};
static devclass_t ti_sdma_devclass;

DRIVER_MODULE(ti_sdma, simplebus, ti_sdma_driver, ti_sdma_devclass, 0, 0);
MODULE_DEPEND(ti_sdma, ti_prcm, 1, 1, 1);
