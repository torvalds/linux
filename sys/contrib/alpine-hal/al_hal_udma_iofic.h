/*-
*******************************************************************************
Copyright (C) 2015 Annapurna Labs Ltd.

This file may be licensed under the terms of the Annapurna Labs Commercial
License Agreement.

Alternatively, this file can be distributed under the terms of the GNU General
Public License V2 as published by the Free Software Foundation and can be
found at http://www.gnu.org/licenses/gpl-2.0.html

Alternatively, redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following conditions are
met:

    *     Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

    *     Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

/**
 * @defgroup group_udma_interrupts UDMA I/O Fabric Interrupt Controller
 * @ingroup group_udma_api
 *  UDMA IOFIC API
 *  @{
 * @file   al_hal_udma_iofic.h
 *
 * @brief C Header file for programming the interrupt controller that found
 * in UDMA based units. These APIs rely and use some the Interrupt controller
 * API under al_hal_iofic.h
 */

#ifndef __AL_HAL_UDMA_IOFIC_H__
#define __AL_HAL_UDMA_IOFIC_H__

#include <al_hal_common.h>
#include <al_hal_iofic.h>
#include <al_hal_udma_regs.h>

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

/**
 * Interrupt Mode
 * This is the interrupt mode for the primary interrupt level The secondary
 * interrupt level does not have mode and it is always a level sensitive
 * interrupt that is reflected in group D of the primary.
 */
enum al_iofic_mode {
	AL_IOFIC_MODE_LEGACY, /**< level-sensitive interrupt wire */
	AL_IOFIC_MODE_MSIX_PER_Q, /**< per UDMA queue MSI-X interrupt */
	AL_IOFIC_MODE_MSIX_PER_GROUP
};

/** interrupt controller level (primary/secondary) */
enum al_udma_iofic_level {
	AL_UDMA_IOFIC_LEVEL_PRIMARY,
	AL_UDMA_IOFIC_LEVEL_SECONDARY
};

/*
 * The next four groups represents the standard 4 groups in the primary
 * interrupt controller of each bus-master unit in the I/O Fabric.
 * The first two groups can be used when accessing the secondary interrupt
 * controller as well.
 */
#define AL_INT_GROUP_A		0 /**< summary of the below events */
#define AL_INT_GROUP_B		1 /**< RX completion queues */
#define AL_INT_GROUP_C		2 /**< TX completion queues */
#define AL_INT_GROUP_D		3 /**< Misc */

/*******************************************************************************
 * Primary interrupt controller, group A bits
 ******************************************************************************/
/* Group A bits which are just summary bits of GROUP B, C and D */
#define AL_INT_GROUP_A_GROUP_B_SUM	AL_BIT(0)
#define AL_INT_GROUP_A_GROUP_C_SUM	AL_BIT(1)
#define AL_INT_GROUP_A_GROUP_D_SUM	AL_BIT(2)

/*******************************************************************************
 * MSIX entry indices
 ******************************************************************************/
/** MSIX entry index for summary of group D in group A */
#define AL_INT_MSIX_GROUP_A_SUM_D_IDX 	2
/** MSIX entry index for RX completion queue 0 */
#define AL_INT_MSIX_RX_COMPLETION_START	3

/*******************************************************************************
 * Primary interrupt controller, group D bits
 ******************************************************************************/
#define AL_INT_GROUP_D_CROSS_MAIL_BOXES	\
			(AL_BIT(0) | AL_BIT(1) | AL_BIT(2) | AL_BIT(3))
/** Summary of secondary interrupt controller, group A) */
#define AL_INT_GROUP_D_M2S	AL_BIT(8)
/** Summary of secondary interrupt controller, group B) */
#define AL_INT_GROUP_D_S2M	AL_BIT(9)
#define AL_INT_GROUP_D_SW_TIMER_INT	AL_BIT(10)
#define AL_INT_GROUP_D_APP_EXT_INT	AL_BIT(11)
#define AL_INT_GROUP_D_ALL			\
			AL_INT_GROUP_D_CROSS_MAIL_BOXES | \
			AL_INT_GROUP_D_M2S | \
			AL_INT_GROUP_D_S2M | \
			AL_INT_GROUP_D_SW_TIMER_INT | \
			AL_INT_GROUP_D_APP_EXT_INT

/*
 * Until this point, all description above is for Groups A/B/C/D in the PRIMARY
 * Interrupt controller.
 * Following are definitions related to the secondary interrupt controller with
 * two cause registers (group A and group B) that covers UDMA M2S/S2M errors.
 * Secondary interrupt controller summary bits are not mapped to the Processor
 * GIC directly, rather they are represented in Group D of the primary interrupt
 * controller.
 */

/******************************************************************************
 * Secondary interrupt Controller, Group A, which holds the TX (M2S) error
 * interrupt bits
 ******************************************************************************/

/**
 * MSIx response
 * MSIX Bus generator response error, the Bus response received with error indication
 */
#define AL_INT_2ND_GROUP_A_M2S_MSIX_RESP		AL_BIT(27)
/**
 * MSIx timeout	MSIX Bus generator timeout error.
 * The generator didn't receive bus response for the MSIx write transaction.
 */
#define AL_INT_2ND_GROUP_A_M2S_MSIX_TO			AL_BIT(26)
/** Prefetch header buffer parity error */
#define AL_INT_2ND_GROUP_A_M2S_PREFETCH_HDR_PARITY	AL_BIT(25)
/** Prefetch descriptor buffer parity error */
#define AL_INT_2ND_GROUP_A_M2S_PREFETCH_DESC_PARITY	AL_BIT(24)
/** Data buffer parity error */
#define AL_INT_2ND_GROUP_A_M2S_DATA_PARITY		AL_BIT(23)
/** Data header buffer parity error */
#define AL_INT_2ND_GROUP_A_M2S_HDR_PARITY		AL_BIT(22)
/** Completion coalescing buffer parity error */
#define AL_INT_2ND_GROUP_A_M2S_COMPL_COAL_PARITY	AL_BIT(21)
/** UNACK packets buffer parity error */
#define AL_INT_2ND_GROUP_A_M2S_UNACK_PKT_PARITY		AL_BIT(20)
/** ACK packets buffer parity error */
#define AL_INT_2ND_GROUP_A_M2S_ACK_PKT_PARITY		AL_BIT(19)
/** AXI data buffer parity error */
#define AL_INT_2ND_GROUP_A_M2S_AX_DATA_PARITY		AL_BIT(18)
/**
 * Prefetch Ring ID error
 * A wrong RingId was received while prefetching submission descriptor. This
 * could indicate a software bug or hardware failure, unless the UDMA is
 * working in a mode to ignore RingId (the al_udma_iofic_config() API can be
 * used to configure the UDMA to ignore the Ring ID check)
 */
#define AL_INT_2ND_GROUP_A_M2S_PREFETCH_RING_ID		AL_BIT(17)
/**
 * Prefetch last
 * Error in last bit indication of the descriptor
 * Descriptor with Last bit asserted is read from the queue to the prefetch
 * FIFO when the prefetch engine is not in a middle of packet processing (a
 * descriptor with First bit asserted should be read first to indicate start of
 * packet)
 */
#define AL_INT_2ND_GROUP_A_M2S_PREFETCH_LAST		AL_BIT(16)
/**
 * Prefetch first
 * Error in first bit indication of the descriptor
 * Descriptor with First bit asserted is read from the queue to the prefetch
 * FIFO while the prefetch engine is in a middle of packet processing ( a
 * descriptor with Last bit asserted should be read to indicate end of packet
 * before starting a new one)
 */
#define AL_INT_2ND_GROUP_A_M2S_PREFETCH_FIRST		AL_BIT(15)
/**
 * Prefetch max descriptors
 * Number of descriptors per packet exceeds the configurable maximum
 * descriptors per packet. This could indicate a software bug or a hardware
 * failure.  (The al_udma_m2s_max_descs_set() API is used to configure the
 * maximum descriptors per packet)
 */
#define AL_INT_2ND_GROUP_A_M2S_PREFETCH_MAX_DESC	AL_BIT(14)
/**
 * Packet length
 * Packet length exceeds the configurable maximum packet size.  The
 * al_udma_m2s_packet_size_cfg_set() API is used to configure the maximum
 * packet size)
 */
#define AL_INT_2ND_GROUP_A_M2S_PKT_LEN			AL_BIT(13)
/**
 * Prefetch AXI timeout
 * Bus request to I/O Fabric timeout error
 */
#define AL_INT_2ND_GROUP_A_M2S_PREFETCH_AXI_TO		AL_BIT(12)
/**
 * Prefetch AXI response
 * Bus response from I/O Fabric error
 */
#define AL_INT_2ND_GROUP_A_M2S_PREFETCH_AXI_RESP	AL_BIT(11)
/**
 * Prefetch AXI parity
 * Bus parity error on descriptor being prefetched
 */
#define AL_INT_2ND_GROUP_A_M2S_PREFETCH_AXI_PARITY	AL_BIT(10)
/**
 * Data AXI timeout
 * Bus request to I/O Fabric timeout error
 */
#define AL_INT_2ND_GROUP_A_M2S_DATA_AXI_TO		AL_BIT(9)
/**
 * Data AXI response
 * Bus response from I/O Fabric error
 */
#define AL_INT_2ND_GROUP_A_M2S_DATA_AXI_RESP		AL_BIT(8)
/**
 * Data AXI parity
 * Bus parity error on data being read
 */
#define AL_INT_2ND_GROUP_A_M2S_DATA_AXI_PARITY		AL_BIT(7)
/**
 * Completion AXI timeout
 * Bus request to I/O Fabric timeout error
 */
#define AL_INT_2ND_GROUP_A_M2S_CONPL_AXI_TO		AL_BIT(6)
/**
 * Completion AXI response
 * Bus response from I/O Fabric error
 */
#define AL_INT_2ND_GROUP_A_M2S_COMPL_AXI_RESP		AL_BIT(5)
/**
 * Completion AXI parity
 * Bus generator internal SRAM parity error
 */
#define AL_INT_2ND_GROUP_A_M2S_COMP_AXI_PARITY		AL_BIT(4)
/**
 * Stream timeout
 * Application stream interface timeout indicating a failure at the Application
 * layer (RAID, Ethernet etc)
 */
#define AL_INT_2ND_GROUP_A_M2S_STRM_TO			AL_BIT(3)
/**
 * Stream response
 * Application stream interface response error indicating a failure at the
 * Application layer (RAID, Ethernet etc)
 */
#define AL_INT_2ND_GROUP_A_M2S_STRM_RESP		AL_BIT(2)
/**
 * Stream parity
 * Application stream interface parity error indicating a failure at the
 * Application layer (RAID, Ethernet etc)
 */
#define AL_INT_2ND_GROUP_A_M2S_STRM_PARITY		 AL_BIT(1)
/**
 * Stream completion mismatch
 * Application stream interface, packet serial mismatch error indicating a
 * failure at the Application layer (RAID, Ethernet etc)
 */
#define AL_INT_2ND_GROUP_A_M2S_STRM_COMPL_MISMATCH	AL_BIT(0)

/*******************************************************************************
 * Secondary interrupt Controller, Group B, which holds the RX (S2M) error
 * interrupt bits
 ******************************************************************************/

/** Prefetch descriptor buffer parity error */
#define AL_INT_2ND_GROUP_B_S2M_PREFETCH_DESC_PARITY	AL_BIT(30)
/** Completion coalescing buffer parity error */
#define AL_INT_2ND_GROUP_B_S2M_COMPL_COAL_PARITY	AL_BIT(29)
/** PRE-UNACK packets buffer parity error */
#define AL_INT_2ND_GROUP_B_S2M_PRE_UNACK_PKT_PARITY	AL_BIT(28)
/** UNACK packets buffer parity error */
#define AL_INT_2ND_GROUP_B_S2M_UNACK_PKT_PARITY		AL_BIT(27)
/** Data buffer parity error */
#define AL_INT_2ND_GROUP_B_S2M_DATA_PARITY		AL_BIT(26)
/** Data header buffer parity error */
#define AL_INT_2ND_GROUP_B_S2M_DATA_HDR_PARITY		AL_BIT(25)
/**
 * Packet length
 * Application stream interface, Data counter length mismatch with metadata
 * packet length indicating a failure at the Application layer (RAID, Ethernet
 * etc)
 */
#define AL_INT_2ND_GROUP_B_S2M_PKT_LEN			AL_BIT(24)
/**
 * Stream last
 * Application stream interface, error in Last bit indication, this error is
 * asserted when a 'last' indication is asserted on the stream interface
 * (between the application and the UDMA) when the interface is not in the
 * middle of packet, meaning that there was no 'first' indication before. This
 * indicates a failure at the application layer.
 */
#define AL_INT_2ND_GROUP_B_S2M_STRM_LAST		AL_BIT(23)
/**
 * Stream first
 * Application stream interface error in first bit indication, this error is
 * asserted when a 'first' indication is asserted on the stream interface
 * (between the application and the UDMA) when the interface is in the middle
 * of packet, meaning that there was a 'first' indication before and the UDMA
 * is waiting for a 'last' indication to end the packet. This indicates a
 * failure at the application layer.
 */
#define AL_INT_2ND_GROUP_B_S2M_STRM_FIRST		AL_BIT(22)
/**
 * Stream data
 * Application stream interface, error indication during data transaction
 */
#define AL_INT_2ND_GROUP_B_S2M_STRM_DATA		AL_BIT(21)
/**
 * Stream Data parity
 * Application stream interface, parity error during data transaction
 */
#define AL_INT_2ND_GROUP_B_S2M_STRM_DATA_PARITY		AL_BIT(20)
/**
 * Stream Header error
 * Application stream interface, error indication during header transaction
 */
#define AL_INT_2ND_GROUP_B_S2M_STRM_HDR			AL_BIT(19)
/**
 * Stream Header parity
 * Application stream interface, parity error during header transaction
 */
#define AL_INT_2ND_GROUP_B_S2M_STRM_HDR_PARITY		AL_BIT(18)
/**
 * Completion UNACK
 * Completion write, UNACK timeout due to completion FIFO back pressure
 */
#define AL_INT_2ND_GROUP_B_S2M_COMPL_UNACK		AL_BIT(17)
/**
 * Completion stream
 * Completion write, UNACK timeout due to stream ACK FIFO back pressure
 */
#define AL_INT_2ND_GROUP_B_S2M_COMPL_STRM		AL_BIT(16)
/**
 * Completion AXI timeout
 * Bus request to I/O Fabric timeout error
 */
#define AL_INT_2ND_GROUP_B_S2M_COMPL_AXI_TO		AL_BIT(15)
/**
 * Completion AXI response
 * Bus response from I/O Fabric error
 */
#define AL_INT_2ND_GROUP_B_S2M_COMPL_AXI_RESP		AL_BIT(14)
/**
 * Completion AXI parity
 * Completion Bus generator internal SRAM parity error
 */
#define AL_INT_2ND_GROUP_B_S2M_COMPL_AXI_PARITY		AL_BIT(13)
/**
 * Prefetch saturate
 * Prefetch engine, packet length counter saturated (32 bit) , this is caused
 * by an error at the application layer which sends packet data without
 * 'last'/'first' indication.
 */
#define AL_INT_2ND_GROUP_B_S2M_PREFETCH_SAT		AL_BIT(12)
/**
 * Prefetch ring ID
 * Prefetch engine, Ring ID is not matching the expected RingID. This could
 * indicate a software bug or hardware failure, unless the UDMA is working in a
 * mode to ignore RingId  (the al_udma_iofic_config() API can be used to
 * configure the UDMA to ignore the Ring ID check)
 */
#define AL_INT_2ND_GROUP_B_S2M_PREFETCH_RING_ID		AL_BIT(11)
/**
 * Prefetch AXI timeout
 * Bus request to I/O Fabric timeout error
 */
#define AL_INT_2ND_GROUP_B_S2M_PREFETCH_AXI_TO		AL_BIT(10)
/**
 * Prefetch AXI response
 * Bus response from I/O Fabric error
 */
#define AL_INT_2ND_GROUP_B_S2M_PREFETCH_AXI_RESP	AL_BIT(9)
/**
 * Prefetch AXI parity
 * Bus parity error on descriptor being prefetched
 */
#define AL_INT_2ND_GROUP_B_S2M_PREFETCH_AXI_PARITY	AL_BIT(8)
/**
 * No descriptors hint
 * Data write, Hint to the SW that there are not enough descriptors in the
 * queue for the current received packet. This is considered a hint and not an
 * error, as it could be a normal situation in certain application. The S2M
 * UDMA behavior when it runs out of Rx Descriptor is controlled by driver
 * which can use this hint to add more descriptors to the Rx queue.
 */
#define AL_INT_2ND_GROUP_B_S2M_NO_DESC_HINT		AL_BIT(7)
/**
 * No descriptors timeout
 * Data write, Timeout indication when there are not enough descriptors for the
 * current packet and the timeout expires. The S2M UDMA behavior when it runs
 * out of Rx Descriptor is controlled by driver which can use this hint to add
 * more descriptors to the Rx queue. The al_udma_s2m_no_desc_cfg_set() is used
 * to configure theUDMA S2M timeout and behavior when there are no Rx
 * descriptors for the received packet.
 */
#define AL_INT_2ND_GROUP_B_S2M_NO_DESC_TO		AL_BIT(6)
/**
 * Promotion indication
 * Data write, the data write engine checks the queue number of the two packets
 * at the head of the data FIFO, the data write engine notify the prefetch
 * engine to promote these queue numbers in the prefetch scheduler to make sure
 * that these queue will have RX descriptors for these packets. This error
 * indicates that the prefetch promotion didn't work for the second packet in
 * the FIFO. This is an indication used for system debug and not an error.
 */
#define AL_INT_2ND_GROUP_B_S2M_PROM_IND			AL_BIT(5)
/**
 * Header split ignored
 * Data write, The application requested header split but the buffer descriptor
 * doesn't include a second buffer for the header
 */
#define AL_INT_2ND_GROUP_B_S2M_HDR_SPLT_IGNORED		AL_BIT(4)
/**
 * Header split length
 * Data write, The application requested header split and the length of the
 * second buffer allocated for the header is not enough for the requested
 * header length. The remaining of the header is written to buffer 1 (data
 * buffer).
 */
#define AL_INT_2ND_GROUP_B_S2M_HDR_SPLT_LEN		AL_BIT(3)
/**
 * Data AXI timeout
 * Bus request to I/O Fabric timeout error
 */
#define AL_INT_2ND_GROUP_B_S2M_DATA_AXI_TO		AL_BIT(2)
/**
 * Data AXI response
 * Bus response from I/O Fabric error
 */
#define AL_INT_2ND_GROUP_B_S2M_DATA_AXI_RESP		AL_BIT(1)
/**
 * Data AXI parity
 * Bus parity error on data being read
 */
#define AL_INT_2ND_GROUP_B_S2M_DATA_AXI_PARITY		AL_BIT(0)

/*******************************************************************************
 * Configurations
 ******************************************************************************/

/**
 * Configure the UDMA interrupt controller registers, interrupts will are kept
 * masked.
 * This is a static setting that should be called while initialized the
 * interrupt controller within a given UDMA, and should not be modified during
 * runtime unless the UDMA is completely disabled. The first argument sets the
 * interrupt and MSIX modes. The m2s/s2m errors/abort are a set of bit-wise
 * masks to define the behaviour of the UDMA once an error happens: The _abort
 * will put the UDMA in abort state once an error happens The _error bitmask
 * will indicate and error in the secondary cause register but will not abort.
 * The bit-mask that the _errors_disable and _aborts_disable are described in
 * 'AL_INT_2ND_GROUP_A_*' and 'AL_INT_2ND_GROUP_B_*'
 *
 * @param regs pointer to unit registers
 * @param mode interrupt scheme mode (legacy, MSI-X..)
 * @param m2s_errors_disable
 * 	  This is a bit-wise mask, to indicate which one of the error causes in
 * 	  secondary interrupt group_A should generate an interrupt. When a bit is
 * 	  set, the error cause is ignored.
 * 	  Recommended value: 0 (enable all errors).
 * @param m2s_aborts_disable
 * 	  This is a bit-wise mask, to indicate which one of the error causes in
 * 	  secondary interrupt group_A should automatically put the UDMA in
 * 	  abort state. When a bit is set, the error cause does cause an abort.
 * 	  Recommended value: 0 (enable all aborts).
 * @param s2m_errors_disable
 * 	  This is a bit-wise mask, to indicate which one of the error causes in
 * 	  secondary interrupt group_A should generate an interrupt. When a bit is
 * 	  set, the error cause is ignored.
 * 	  Recommended value: 0xE0 (disable hint errors).
 * @param s2m_aborts_disable
 * 	  This is a bit-wise mask, to indicate which one of the error causes in
 * 	  secondary interrupt group_A should automatically put the UDMA in
 * 	  abort state. When a bit is set, the error cause does cause an abort.
 * 	  Recommended value: 0xE0 (disable hint aborts).
 *
 * @return 0 on success. -EINVAL otherwise.
 */
int al_udma_iofic_config(struct unit_regs __iomem *regs,
			enum al_iofic_mode mode,
			uint32_t	m2s_errors_disable,
			uint32_t	m2s_aborts_disable,
			uint32_t	s2m_errors_disable,
			uint32_t	s2m_aborts_disable);
/**
 * return the offset of the unmask register for a given group.
 * this function can be used when the upper layer wants to directly
 * access the unmask regiter and bypass the al_udma_iofic_unmask() API.
 *
 * @param regs pointer to udma registers
 * @param level the interrupt controller level (primary / secondary)
 * @param group the interrupt group ('AL_INT_GROUP_*')
 * @return the offset of the unmask register.
 */
uint32_t __iomem * al_udma_iofic_unmask_offset_get(
	struct unit_regs __iomem	*regs,
	enum al_udma_iofic_level	level,
	int				group);

/**
 * Get the interrupt controller base address for either the primary or secondary
 * interrupt controller
 *
 * @param regs pointer to udma unit registers
 * @param level the interrupt controller level (primary / secondary)
 *
 * @returns	The interrupt controller base address
 *
 */
static INLINE void __iomem *al_udma_iofic_reg_base_get(
	struct unit_regs __iomem	*regs,
	enum al_udma_iofic_level	level)
{
	void __iomem *iofic_regs = (level == AL_UDMA_IOFIC_LEVEL_PRIMARY) ?
		(void __iomem *)&regs->gen.interrupt_regs.main_iofic :
		(void __iomem *)&regs->gen.interrupt_regs.secondary_iofic_ctrl;

	return iofic_regs;
}

/**
 * Check the interrupt controller level/group validity
 *
 * @param level the interrupt controller level (primary / secondary)
 * @param group the interrupt group ('AL_INT_GROUP_*')
 *
 * @returns	0 - invalid, 1 - valid
 *
 */
static INLINE int al_udma_iofic_level_and_group_valid(
	enum al_udma_iofic_level	level,
	int				group)
{
	if (((level == AL_UDMA_IOFIC_LEVEL_PRIMARY) && (group >= 0) && (group < 4)) ||
		((level == AL_UDMA_IOFIC_LEVEL_SECONDARY) && (group >= 0) && (group < 2)))
		return 1;

	return 0;
}
/**
 * unmask specific interrupts for a given group
 * this functions uses the interrupt mask clear register to guarantee atomicity
 * it's safe to call it while the mask is changed by the HW (auto mask) or another cpu.
 *
 * @param regs pointer to udma unit registers
 * @param level the interrupt controller level (primary / secondary)
 * @param group the interrupt group ('AL_INT_GROUP_*')
 * @param mask bitwise of interrupts to unmask, set bits will be unmasked.
 */
static INLINE void al_udma_iofic_unmask(
	struct unit_regs __iomem	*regs,
	enum al_udma_iofic_level	level,
	int				group,
	uint32_t			mask)
{
	al_assert(al_udma_iofic_level_and_group_valid(level, group));
	al_iofic_unmask(al_udma_iofic_reg_base_get(regs, level), group, mask);
}

/**
 * mask specific interrupts for a given group
 * this functions modifies interrupt mask register, the callee must make sure
 * the mask is not changed by another cpu.
 *
 * @param regs pointer to udma unit registers
 * @param level the interrupt controller level (primary / secondary)
 * @param group the interrupt group ('AL_INT_GROUP_*')
 * @param mask bitwise of interrupts to mask, set bits will be masked.
 */
static INLINE void al_udma_iofic_mask(
	struct unit_regs __iomem	*regs,
	enum al_udma_iofic_level	level,
	int				group,
	uint32_t			mask)
{
	al_assert(al_udma_iofic_level_and_group_valid(level, group));
	al_iofic_mask(al_udma_iofic_reg_base_get(regs, level), group, mask);
}

/**
 * read interrupt cause register for a given group
 * this will clear the set bits if the Clear on Read mode enabled.
 * @param regs pointer to udma unit registers
 * @param level the interrupt controller level (primary / secondary)
 * @param group the interrupt group ('AL_INT_GROUP_*')
 */
static INLINE uint32_t al_udma_iofic_read_cause(
	struct unit_regs __iomem	*regs,
	enum al_udma_iofic_level	level,
	int				group)
{
	al_assert(al_udma_iofic_level_and_group_valid(level, group));
	return al_iofic_read_cause(al_udma_iofic_reg_base_get(regs, level), group);
}

/**
 * clear bits in the interrupt cause register for a given group
 *
 * @param regs pointer to udma unit registers
 * @param level the interrupt controller level (primary / secondary)
 * @param group the interrupt group ('AL_INT_GROUP_*')
 * @param mask bitwise of bits to be cleared, set bits will be cleared.
 */
static INLINE void al_udma_iofic_clear_cause(
	struct unit_regs __iomem	*regs,
	enum al_udma_iofic_level	level,
	int				group,
	uint32_t		mask)
{
	al_assert(al_udma_iofic_level_and_group_valid(level, group));
	al_iofic_clear_cause(al_udma_iofic_reg_base_get(regs, level), group, mask);
}

#endif
/** @} end of UDMA group */
