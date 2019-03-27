/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @defgroup sli SLI-4 Base APIs
 */

/**
 * @file
 * All common (i.e. transport-independent) SLI-4 functions are implemented
 * in this file.
 */

#include "sli4.h"

#if defined(OCS_INCLUDE_DEBUG)
#include "ocs_utils.h"
#endif

#define SLI4_BMBX_DELAY_US 1000 /* 1 ms */
#define SLI4_INIT_PORT_DELAY_US 10000 /* 10 ms */

static int32_t sli_fw_init(sli4_t *);
static int32_t sli_fw_term(sli4_t *);
static int32_t sli_sliport_control(sli4_t *sli4, uint32_t endian);
static int32_t sli_cmd_fw_deinitialize(sli4_t *, void *, size_t);
static int32_t sli_cmd_fw_initialize(sli4_t *, void *, size_t);
static int32_t sli_queue_doorbell(sli4_t *, sli4_queue_t *);
static uint8_t sli_queue_entry_is_valid(sli4_queue_t *, uint8_t *, uint8_t);

const uint8_t sli4_fw_initialize[] = {
	0xff, 0x12, 0x34, 0xff,
	0xff, 0x56, 0x78, 0xff,
};

const uint8_t sli4_fw_deinitialize[] = {
	0xff, 0xaa, 0xbb, 0xff,
	0xff, 0xcc, 0xdd, 0xff,
};

typedef struct {
	uint32_t rev_id;
	uint32_t family;	/* generation */
	sli4_asic_type_e type;
	sli4_asic_rev_e rev;
} sli4_asic_entry_t;

sli4_asic_entry_t sli4_asic_table[] = {
	{	0x00,	1,	SLI4_ASIC_TYPE_BE3,	SLI4_ASIC_REV_A0},
	{	0x01,	1,	SLI4_ASIC_TYPE_BE3,	SLI4_ASIC_REV_A1},
	{	0x02,	1,	SLI4_ASIC_TYPE_BE3,	SLI4_ASIC_REV_A2},
	{	0x00,	4,	SLI4_ASIC_TYPE_SKYHAWK,	SLI4_ASIC_REV_A0},
	{	0x00,	2,	SLI4_ASIC_TYPE_SKYHAWK,	SLI4_ASIC_REV_A0},
	{	0x10,	1,	SLI4_ASIC_TYPE_BE3,	SLI4_ASIC_REV_B0},
	{	0x10,	0x04,	SLI4_ASIC_TYPE_SKYHAWK,	SLI4_ASIC_REV_B0},
	{	0x11,	0x04,	SLI4_ASIC_TYPE_SKYHAWK,	SLI4_ASIC_REV_B1},
	{	0x0,	0x0a,	SLI4_ASIC_TYPE_LANCER,	SLI4_ASIC_REV_A0},
	{	0x10,	0x0b,	SLI4_ASIC_TYPE_LANCER,	SLI4_ASIC_REV_B0},
	{	0x30,	0x0b,	SLI4_ASIC_TYPE_LANCER,	SLI4_ASIC_REV_D0},
	{	0x3,	0x0b,	SLI4_ASIC_TYPE_LANCERG6,SLI4_ASIC_REV_A3},
	{	0x0,	0x0c,	SLI4_ASIC_TYPE_LANCERG6,SLI4_ASIC_REV_A0},
	{	0x1,	0x0c,	SLI4_ASIC_TYPE_LANCERG6,SLI4_ASIC_REV_A1},
	{	0x3,	0x0c,	SLI4_ASIC_TYPE_LANCERG6,SLI4_ASIC_REV_A3},

	{	0x00,	0x05,	SLI4_ASIC_TYPE_CORSAIR,	SLI4_ASIC_REV_A0},
};

/*
 * @brief Convert queue type enum (SLI_QTYPE_*) into a string.
 */
const char *SLI_QNAME[] = {
	"Event Queue",
	"Completion Queue",
	"Mailbox Queue",
	"Work Queue",
	"Receive Queue",
	"Undefined"
};

/**
 * @brief Define the mapping of registers to their BAR and offset.
 *
 * @par Description
 * Although SLI-4 specification defines a common set of registers, their locations
 * (both BAR and offset) depend on the interface type. This array maps a register
 * enum to an array of BAR/offset pairs indexed by the interface type. For
 * example, to access the bootstrap mailbox register on an interface type 0
 * device, code can refer to the offset using regmap[SLI4_REG_BMBX][0].offset.
 *
 * @b Note: A value of UINT32_MAX for either the register set (rset) or offset (off)
 * indicates an invalid mapping.
 */
const sli4_reg_t regmap[SLI4_REG_MAX][SLI4_MAX_IF_TYPES] = {
	/* SLI4_REG_BMBX */
	{
		{ 2, SLI4_BMBX_REG }, { 0, SLI4_BMBX_REG }, { 0, SLI4_BMBX_REG }, { 0, SLI4_BMBX_REG },
	},
	/* SLI4_REG_EQCQ_DOORBELL */
	{
		{ 2, SLI4_EQCQ_DOORBELL_REG }, { 0, SLI4_EQCQ_DOORBELL_REG },
		{ 0, SLI4_EQCQ_DOORBELL_REG }, { 0, SLI4_EQCQ_DOORBELL_REG },
	},
	/* SLI4_REG_FCOE_RQ_DOORBELL */
	{
		{ 2, SLI4_RQ_DOORBELL_REG }, { 0, SLI4_RQ_DOORBELL_REG },
		{ 0, SLI4_RQ_DOORBELL_REG }, { UINT32_MAX, UINT32_MAX },
	},
	/* SLI4_REG_IO_WQ_DOORBELL */
	{
		{ 2, SLI4_IO_WQ_DOORBELL_REG }, { 0, SLI4_IO_WQ_DOORBELL_REG }, { 0, SLI4_IO_WQ_DOORBELL_REG }, { UINT32_MAX, UINT32_MAX },
	},
	/* SLI4_REG_MQ_DOORBELL */
	{
		{ 2, SLI4_MQ_DOORBELL_REG }, { 0, SLI4_MQ_DOORBELL_REG },
		{ 0, SLI4_MQ_DOORBELL_REG }, { 0, SLI4_MQ_DOORBELL_REG },
	},
	/* SLI4_REG_PHYSDEV_CONTROL */
	{
		{ UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX }, { 0, SLI4_PHSDEV_CONTROL_REG_23 }, { 0, SLI4_PHSDEV_CONTROL_REG_23 },
	},
	/* SLI4_REG_SLIPORT_CONTROL */
	{
		{ UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX }, { 0, SLI4_SLIPORT_CONTROL_REG }, { UINT32_MAX, UINT32_MAX },
	},
	/* SLI4_REG_SLIPORT_ERROR1 */
	{
		{ UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX }, { 0, SLI4_SLIPORT_ERROR1 }, { UINT32_MAX, UINT32_MAX },
	},
	/* SLI4_REG_SLIPORT_ERROR2 */
	{
		{ UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX }, { 0, SLI4_SLIPORT_ERROR2 }, { UINT32_MAX, UINT32_MAX },
	},
	/* SLI4_REG_SLIPORT_SEMAPHORE */
	{
		{ 1, SLI4_PORT_SEMAPHORE_REG_0 },  { 0, SLI4_PORT_SEMAPHORE_REG_1 },
		{ 0, SLI4_PORT_SEMAPHORE_REG_23 }, { 0, SLI4_PORT_SEMAPHORE_REG_23 },
	},
	/* SLI4_REG_SLIPORT_STATUS */
	{
		{ UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX }, { 0, SLI4_PORT_STATUS_REG_23 }, { 0, SLI4_PORT_STATUS_REG_23 },
	},
	/* SLI4_REG_UERR_MASK_HI */
	{
		{ 0, SLI4_UERR_MASK_HIGH_REG }, { UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX },
	},
	/* SLI4_REG_UERR_MASK_LO */
	{
		{ 0, SLI4_UERR_MASK_LOW_REG }, { UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX },
	},
	/* SLI4_REG_UERR_STATUS_HI */
	{
		{ 0, SLI4_UERR_STATUS_HIGH_REG }, { UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX },
	},
	/* SLI4_REG_UERR_STATUS_LO */
	{
		{ 0, SLI4_UERR_STATUS_LOW_REG }, { UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX },
	},
	/* SLI4_REG_SW_UE_CSR1 */
	{
		{ 1, SLI4_SW_UE_CSR1}, { UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX },
	},
	/* SLI4_REG_SW_UE_CSR2 */
	{
		{ 1, SLI4_SW_UE_CSR2}, { UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX }, { UINT32_MAX, UINT32_MAX },
	},
};

/**
 * @brief Read the given SLI register.
 *
 * @param sli Pointer to the SLI context.
 * @param reg Register name enum.
 *
 * @return Returns the register value.
 */
uint32_t
sli_reg_read(sli4_t *sli, sli4_regname_e reg)
{
	const sli4_reg_t	*r = &(regmap[reg][sli->if_type]);

	if ((UINT32_MAX == r->rset) || (UINT32_MAX == r->off)) {
		ocs_log_err(sli->os, "regname %d not defined for if_type %d\n", reg, sli->if_type);
		return UINT32_MAX;
	}

	return ocs_reg_read32(sli->os, r->rset, r->off);
}

/**
 * @brief Write the value to the given SLI register.
 *
 * @param sli Pointer to the SLI context.
 * @param reg Register name enum.
 * @param val Value to write.
 *
 * @return None.
 */
void
sli_reg_write(sli4_t *sli, sli4_regname_e reg, uint32_t val)
{
	const sli4_reg_t	*r = &(regmap[reg][sli->if_type]);

	if ((UINT32_MAX == r->rset) || (UINT32_MAX == r->off)) {
		ocs_log_err(sli->os, "regname %d not defined for if_type %d\n", reg, sli->if_type);
		return;
	}

	ocs_reg_write32(sli->os, r->rset, r->off, val);
}

/**
 * @brief Check if the SLI_INTF register is valid.
 *
 * @param val 32-bit SLI_INTF register value.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
static uint8_t
sli_intf_valid_check(uint32_t val)
{
	return ((val >> SLI4_INTF_VALID_SHIFT) & SLI4_INTF_VALID_MASK) != SLI4_INTF_VALID;
}

/**
 * @brief Retrieve the SLI revision level.
 *
 * @param val 32-bit SLI_INTF register value.
 *
 * @return Returns the SLI revision level.
 */
static uint8_t
sli_intf_sli_revision(uint32_t val)
{
	return ((val >> SLI4_INTF_SLI_REVISION_SHIFT) & SLI4_INTF_SLI_REVISION_MASK);
}

static uint8_t
sli_intf_sli_family(uint32_t val)
{
	return ((val >> SLI4_INTF_SLI_FAMILY_SHIFT) & SLI4_INTF_SLI_FAMILY_MASK);
}

/**
 * @brief Retrieve the SLI interface type.
 *
 * @param val 32-bit SLI_INTF register value.
 *
 * @return Returns the SLI interface type.
 */
static uint8_t
sli_intf_if_type(uint32_t val)
{
	return ((val >> SLI4_INTF_IF_TYPE_SHIFT) & SLI4_INTF_IF_TYPE_MASK);
}

/**
 * @brief Retrieve PCI revision ID.
 *
 * @param val 32-bit PCI CLASS_REVISION register value.
 *
 * @return Returns the PCI revision ID.
 */
static uint8_t
sli_pci_rev_id(uint32_t val)
{
	return ((val >> SLI4_PCI_REV_ID_SHIFT) & SLI4_PCI_REV_ID_MASK);
}

/**
 * @brief retrieve SLI ASIC generation
 *
 * @param val 32-bit SLI_ASIC_ID register value
 *
 * @return SLI ASIC generation
 */
static uint8_t
sli_asic_gen(uint32_t val)
{
	return ((val >> SLI4_ASIC_GEN_SHIFT) & SLI4_ASIC_GEN_MASK);
}

/**
 * @brief Wait for the bootstrap mailbox to report "ready".
 *
 * @param sli4 SLI context pointer.
 * @param msec Number of milliseconds to wait.
 *
 * @return Returns 0 if BMBX is ready, or non-zero otherwise (i.e. time out occurred).
 */
static int32_t
sli_bmbx_wait(sli4_t *sli4, uint32_t msec)
{
	uint32_t	val = 0;

	do {
		ocs_udelay(SLI4_BMBX_DELAY_US);
		val = sli_reg_read(sli4, SLI4_REG_BMBX);
		msec--;
	} while(msec && !(val & SLI4_BMBX_RDY));

	return(!(val & SLI4_BMBX_RDY));
}

/**
 * @brief Write bootstrap mailbox.
 *
 * @param sli4 SLI context pointer.
 *
 * @return Returns 0 if command succeeded, or non-zero otherwise.
 */
static int32_t
sli_bmbx_write(sli4_t *sli4)
{
	uint32_t	val = 0;

	/* write buffer location to bootstrap mailbox register */
	ocs_dma_sync(&sli4->bmbx, OCS_DMASYNC_PREWRITE);
	val = SLI4_BMBX_WRITE_HI(sli4->bmbx.phys);
	sli_reg_write(sli4, SLI4_REG_BMBX, val);

	if (sli_bmbx_wait(sli4, SLI4_BMBX_DELAY_US)) {
		ocs_log_crit(sli4->os, "BMBX WRITE_HI failed\n");
		return -1;
	}
	val = SLI4_BMBX_WRITE_LO(sli4->bmbx.phys);
	sli_reg_write(sli4, SLI4_REG_BMBX, val);

	/* wait for SLI Port to set ready bit */
	return sli_bmbx_wait(sli4, SLI4_BMBX_TIMEOUT_MSEC/*XXX*/);
}

#if defined(OCS_INCLUDE_DEBUG)
/**
 * @ingroup sli
 * @brief Dump BMBX mailbox command.
 *
 * @par Description
 * Convenience function for dumping BMBX mailbox commands. Takes
 * into account which mailbox command is given since SLI_CONFIG
 * commands are special.
 *
 * @b Note: This function takes advantage of
 * the one-command-at-a-time nature of the BMBX to be able to
 * display non-embedded SLI_CONFIG commands. This will not work
 * for mailbox commands on the MQ. Luckily, all current non-emb
 * mailbox commands go through the BMBX.
 *
 * @param sli4 SLI context pointer.
 * @param mbx Pointer to mailbox command to dump.
 * @param prefix Prefix for dump label.
 *
 * @return None.
 */
static void
sli_dump_bmbx_command(sli4_t *sli4, void *mbx, const char *prefix)
{
	uint32_t size = 0;
	char label[64];
	uint32_t i;
	/* Mailbox diagnostic logging */
	sli4_mbox_command_header_t *hdr = (sli4_mbox_command_header_t *)mbx;

	if (!ocs_debug_is_enabled(OCS_DEBUG_ENABLE_MQ_DUMP)) {
		return;
	}

	if (hdr->command == SLI4_MBOX_COMMAND_SLI_CONFIG) {
		sli4_cmd_sli_config_t *sli_config = (sli4_cmd_sli_config_t *)hdr;
		sli4_req_hdr_t	*sli_config_hdr;
		if (sli_config->emb) {
			ocs_snprintf(label, sizeof(label), "%s (emb)", prefix);

			/*  if embedded, dump entire command */
			sli_config_hdr = (sli4_req_hdr_t *)sli_config->payload.embed;
			size = sizeof(*sli_config) - sizeof(sli_config->payload) +
				sli_config_hdr->request_length + (4*sizeof(uint32_t));
			ocs_dump32(OCS_DEBUG_ENABLE_MQ_DUMP, sli4->os, label,
				   (uint8_t *)sli4->bmbx.virt, size);
		} else {
			sli4_sli_config_pmd_t *pmd;
			ocs_snprintf(label, sizeof(label), "%s (non-emb hdr)", prefix);

			/* if non-embedded, break up into two parts: SLI_CONFIG hdr
			   and the payload(s) */
			size = sizeof(*sli_config) - sizeof(sli_config->payload) + (12 * sli_config->pmd_count);
			ocs_dump32(OCS_DEBUG_ENABLE_MQ_DUMP, sli4->os, label,
				   (uint8_t *)sli4->bmbx.virt, size);

			/* as sanity check, make sure first PMD matches what was saved */
			pmd = &sli_config->payload.mem;
			if ((pmd->address_high == ocs_addr32_hi(sli4->bmbx_non_emb_pmd->phys)) &&
			    (pmd->address_low == ocs_addr32_lo(sli4->bmbx_non_emb_pmd->phys))) {
				for (i = 0; i < sli_config->pmd_count; i++, pmd++) {
					sli_config_hdr = sli4->bmbx_non_emb_pmd->virt;
					ocs_snprintf(label, sizeof(label), "%s (non-emb pay[%d])",
						     prefix, i);
					ocs_dump32(OCS_DEBUG_ENABLE_MQ_DUMP, sli4->os, label,
						   (uint8_t *)sli4->bmbx_non_emb_pmd->virt,
						   sli_config_hdr->request_length + (4*sizeof(uint32_t)));
				}
			} else {
				ocs_log_debug(sli4->os, "pmd addr does not match pmd:%x %x (%x %x)\n",
					pmd->address_high, pmd->address_low,
					ocs_addr32_hi(sli4->bmbx_non_emb_pmd->phys),
					ocs_addr32_lo(sli4->bmbx_non_emb_pmd->phys));
			}

		}
	} else {
		/* not an SLI_CONFIG command, just display first 64 bytes, like we do
		   for MQEs */
		size = 64;
		ocs_dump32(OCS_DEBUG_ENABLE_MQ_DUMP, sli4->os, prefix,
			   (uint8_t *)mbx, size);
	}
}
#endif

/**
 * @ingroup sli
 * @brief Submit a command to the bootstrap mailbox and check the status.
 *
 * @param sli4 SLI context pointer.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_bmbx_command(sli4_t *sli4)
{
	void *cqe = (uint8_t *)sli4->bmbx.virt + SLI4_BMBX_SIZE;

#if defined(OCS_INCLUDE_DEBUG)
	sli_dump_bmbx_command(sli4, sli4->bmbx.virt, "bmbx cmd");
#endif

	if (sli_fw_error_status(sli4) > 0) {
		ocs_log_crit(sli4->os, "Chip is in an error state - Mailbox "
			"command rejected status=%#x error1=%#x error2=%#x\n",
			sli_reg_read(sli4, SLI4_REG_SLIPORT_STATUS),
			sli_reg_read(sli4, SLI4_REG_SLIPORT_ERROR1),
			sli_reg_read(sli4, SLI4_REG_SLIPORT_ERROR2));
		return -1;
	}

	if (sli_bmbx_write(sli4)) {
		ocs_log_crit(sli4->os, "bootstrap mailbox write fail phys=%p reg=%#x\n",
			(void*)sli4->bmbx.phys,
			sli_reg_read(sli4, SLI4_REG_BMBX));
		return -1;
	}

	/* check completion queue entry status */
	ocs_dma_sync(&sli4->bmbx, OCS_DMASYNC_POSTREAD);
	if (((sli4_mcqe_t *)cqe)->val) {
#if defined(OCS_INCLUDE_DEBUG)
		sli_dump_bmbx_command(sli4, sli4->bmbx.virt, "bmbx cmpl");
        ocs_dump32(OCS_DEBUG_ENABLE_CQ_DUMP, sli4->os, "bmbx cqe", cqe, sizeof(sli4_mcqe_t));
#endif
		return sli_cqe_mq(cqe);
	} else {
		ocs_log_err(sli4->os, "invalid or wrong type\n");
		return -1;
	}
}

/****************************************************************************
 * Messages
 */

/**
 * @ingroup sli
 * @brief Write a CONFIG_LINK command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_config_link(sli4_t *sli4, void *buf, size_t size)
{
	sli4_cmd_config_link_t	*config_link = buf;

	ocs_memset(buf, 0, size);

	config_link->hdr.command = SLI4_MBOX_COMMAND_CONFIG_LINK;

	/* Port interprets zero in a field as "use default value" */

	return sizeof(sli4_cmd_config_link_t);
}

/**
 * @ingroup sli
 * @brief Write a DOWN_LINK command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_down_link(sli4_t *sli4, void *buf, size_t size)
{
	sli4_mbox_command_header_t	*hdr = buf;

	ocs_memset(buf, 0, size);

	hdr->command = SLI4_MBOX_COMMAND_DOWN_LINK;

	/* Port interprets zero in a field as "use default value" */

	return sizeof(sli4_mbox_command_header_t);
}

/**
 * @ingroup sli
 * @brief Write a DUMP Type 4 command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param wki The well known item ID.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_dump_type4(sli4_t *sli4, void *buf, size_t size, uint16_t wki)
{
	sli4_cmd_dump4_t	*cmd = buf;

	ocs_memset(buf, 0, size);

	cmd->hdr.command = SLI4_MBOX_COMMAND_DUMP;
	cmd->type = 4;
	cmd->wki_selection = wki;
	return sizeof(sli4_cmd_dump4_t);
}

/**
 * @ingroup sli
 * @brief Write a COMMON_READ_TRANSCEIVER_DATA command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param page_num The page of SFP data to retrieve (0xa0 or 0xa2).
 * @param dma DMA structure from which the data will be copied.
 *
 * @note This creates a Version 0 message.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_read_transceiver_data(sli4_t *sli4, void *buf, size_t size, uint32_t page_num,
				     ocs_dma_t *dma)
{
	sli4_req_common_read_transceiver_data_t *req = NULL;
	uint32_t	sli_config_off = 0;
	uint32_t	payload_size;

	if (dma == NULL) {
		/* Payload length must accommodate both request and response */
		payload_size = max(sizeof(sli4_req_common_read_transceiver_data_t),
				   sizeof(sli4_res_common_read_transceiver_data_t));
	} else {
		payload_size = dma->size;
	}

	if (sli4->port_type == SLI4_PORT_TYPE_FC) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size, payload_size, dma);
	}

	if (dma == NULL) {
		req = (sli4_req_common_read_transceiver_data_t *)((uint8_t *)buf + sli_config_off);
	} else {
		req = (sli4_req_common_read_transceiver_data_t *)dma->virt;
		ocs_memset(req, 0, dma->size);
	}

	req->hdr.opcode = SLI4_OPC_COMMON_READ_TRANSCEIVER_DATA;
	req->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
	req->hdr.request_length = payload_size - sizeof(sli4_req_hdr_t);

	req->page_number = page_num;
	req->port = sli4->physical_port;

	return(sli_config_off + sizeof(sli4_req_common_read_transceiver_data_t));
}

/**
 * @ingroup sli
 * @brief Write a READ_LINK_STAT command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param req_ext_counters If TRUE, then the extended counters will be requested.
 * @param clear_overflow_flags If TRUE, then overflow flags will be cleared.
 * @param clear_all_counters If TRUE, the counters will be cleared.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_read_link_stats(sli4_t *sli4, void *buf, size_t size,
			uint8_t req_ext_counters,
			uint8_t clear_overflow_flags,
			uint8_t clear_all_counters)
{
	sli4_cmd_read_link_stats_t	*cmd = buf;

	ocs_memset(buf, 0, size);

	cmd->hdr.command = SLI4_MBOX_COMMAND_READ_LNK_STAT;
	cmd->rec = req_ext_counters;
	cmd->clrc = clear_all_counters;
	cmd->clof = clear_overflow_flags;
	return sizeof(sli4_cmd_read_link_stats_t);
}

/**
 * @ingroup sli
 * @brief Write a READ_STATUS command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param clear_counters If TRUE, the counters will be cleared.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_read_status(sli4_t *sli4, void *buf, size_t size,
			uint8_t clear_counters)
{
	sli4_cmd_read_status_t	*cmd = buf;

	ocs_memset(buf, 0, size);

	cmd->hdr.command = SLI4_MBOX_COMMAND_READ_STATUS;
	cmd->cc = clear_counters;
	return sizeof(sli4_cmd_read_status_t);
}

/**
 * @brief Write a FW_DEINITIALIZE command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 *
 * @return Returns the number of bytes written.
 */
static int32_t
sli_cmd_fw_deinitialize(sli4_t *sli4, void *buf, size_t size)
{

	ocs_memset(buf, 0, size);
	ocs_memcpy(buf, sli4_fw_deinitialize, sizeof(sli4_fw_deinitialize));

	return sizeof(sli4_fw_deinitialize);
}

/**
 * @brief Write a FW_INITIALIZE command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 *
 * @return Returns the number of bytes written.
 */
static int32_t
sli_cmd_fw_initialize(sli4_t *sli4, void *buf, size_t size)
{

	ocs_memset(buf, 0, size);
	ocs_memcpy(buf, sli4_fw_initialize, sizeof(sli4_fw_initialize));

	return sizeof(sli4_fw_initialize);
}

/**
 * @ingroup sli
 * @brief Write an INIT_LINK command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param speed Link speed.
 * @param reset_alpa For native FC, this is the selective reset AL_PA
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_init_link(sli4_t *sli4, void *buf, size_t size, uint32_t speed, uint8_t reset_alpa)
{
	sli4_cmd_init_link_t	*init_link = buf;

	ocs_memset(buf, 0, size);

	init_link->hdr.command = SLI4_MBOX_COMMAND_INIT_LINK;

	/* Most fields only have meaning for FC links */
	if (sli4->config.topology != SLI4_READ_CFG_TOPO_FCOE) {
		init_link->selective_reset_al_pa = reset_alpa;
		init_link->link_flags.loopback = FALSE;

		init_link->link_speed_selection_code = speed;
		switch (speed) {
		case FC_LINK_SPEED_1G:
		case FC_LINK_SPEED_2G:
		case FC_LINK_SPEED_4G:
		case FC_LINK_SPEED_8G:
		case FC_LINK_SPEED_16G:
		case FC_LINK_SPEED_32G:
			init_link->link_flags.fixed_speed = TRUE;
			break;
		case FC_LINK_SPEED_10G:
			ocs_log_test(sli4->os, "unsupported FC speed %d\n", speed);
			return 0;
		}

		switch (sli4->config.topology) {
		case SLI4_READ_CFG_TOPO_FC:
			/* Attempt P2P but failover to FC-AL */
			init_link->link_flags.enable_topology_failover = TRUE;

			if (sli_get_asic_type(sli4) == SLI4_ASIC_TYPE_LANCER)
				init_link->link_flags.topology = SLI4_INIT_LINK_F_FCAL_FAIL_OVER;
			else
				init_link->link_flags.topology = SLI4_INIT_LINK_F_P2P_FAIL_OVER;

			break;
		case SLI4_READ_CFG_TOPO_FC_AL:
			init_link->link_flags.topology = SLI4_INIT_LINK_F_FCAL_ONLY;
			if ((init_link->link_speed_selection_code == FC_LINK_SPEED_16G) ||
			    (init_link->link_speed_selection_code == FC_LINK_SPEED_32G)) {
				ocs_log_test(sli4->os, "unsupported FC-AL speed %d\n", speed);
				return 0;
			}
			break;
		case SLI4_READ_CFG_TOPO_FC_DA:
			init_link->link_flags.topology = FC_TOPOLOGY_P2P;
			break;
		default:
			ocs_log_test(sli4->os, "unsupported topology %#x\n", sli4->config.topology);
			return 0;
		}

		init_link->link_flags.unfair = FALSE;
		init_link->link_flags.skip_lirp_lilp = FALSE;
		init_link->link_flags.gen_loop_validity_check = FALSE;
		init_link->link_flags.skip_lisa = FALSE;
		init_link->link_flags.select_hightest_al_pa = FALSE;
	}

	return sizeof(sli4_cmd_init_link_t);
}

/**
 * @ingroup sli
 * @brief Write an INIT_VFI command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param vfi VFI
 * @param fcfi FCFI
 * @param vpi VPI (Set to -1 if unused.)
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_init_vfi(sli4_t *sli4, void *buf, size_t size, uint16_t vfi,
		uint16_t fcfi, uint16_t vpi)
{
	sli4_cmd_init_vfi_t	*init_vfi = buf;

	ocs_memset(buf, 0, size);

	init_vfi->hdr.command = SLI4_MBOX_COMMAND_INIT_VFI;

	init_vfi->vfi = vfi;
	init_vfi->fcfi = fcfi;

	/*
	 * If the VPI is valid, initialize it at the same time as
	 * the VFI
	 */
	if (0xffff != vpi) {
		init_vfi->vp  = TRUE;
		init_vfi->vpi = vpi;
	}

	return sizeof(sli4_cmd_init_vfi_t);
}

/**
 * @ingroup sli
 * @brief Write an INIT_VPI command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param vpi VPI allocated.
 * @param vfi VFI associated with this VPI.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_init_vpi(sli4_t *sli4, void *buf, size_t size, uint16_t vpi, uint16_t vfi)
{
	sli4_cmd_init_vpi_t	*init_vpi = buf;

	ocs_memset(buf, 0, size);

	init_vpi->hdr.command = SLI4_MBOX_COMMAND_INIT_VPI;
	init_vpi->vpi = vpi;
	init_vpi->vfi = vfi;

	return sizeof(sli4_cmd_init_vpi_t);
}

/**
 * @ingroup sli
 * @brief Write a POST_XRI command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param xri_base Starting XRI value for range of XRI given to SLI Port.
 * @param xri_count Number of XRIs provided to the SLI Port.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_post_xri(sli4_t *sli4, void *buf, size_t size,  uint16_t xri_base, uint16_t xri_count)
{
	sli4_cmd_post_xri_t	*post_xri = buf;

	ocs_memset(buf, 0, size);

	post_xri->hdr.command = SLI4_MBOX_COMMAND_POST_XRI;
	post_xri->xri_base = xri_base;
	post_xri->xri_count = xri_count;

	if (sli4->config.auto_xfer_rdy == 0) {
		post_xri->enx = TRUE;
		post_xri->val = TRUE;
	}

	return sizeof(sli4_cmd_post_xri_t);
}

/**
 * @ingroup sli
 * @brief Write a RELEASE_XRI command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param num_xri The number of XRIs to be released.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_release_xri(sli4_t *sli4, void *buf, size_t size,  uint8_t num_xri)
{
	sli4_cmd_release_xri_t	*release_xri = buf;

	ocs_memset(buf, 0, size);

	release_xri->hdr.command = SLI4_MBOX_COMMAND_RELEASE_XRI;
	release_xri->xri_count = num_xri;

	return sizeof(sli4_cmd_release_xri_t);
}

/**
 * @brief Write a READ_CONFIG command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes
 *
 * @return Returns the number of bytes written.
 */
static int32_t
sli_cmd_read_config(sli4_t *sli4, void *buf, size_t size)
{
	sli4_cmd_read_config_t	*read_config = buf;

	ocs_memset(buf, 0, size);

	read_config->hdr.command = SLI4_MBOX_COMMAND_READ_CONFIG;

	return sizeof(sli4_cmd_read_config_t);
}

/**
 * @brief Write a READ_NVPARMS command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_read_nvparms(sli4_t *sli4, void *buf, size_t size)
{
	sli4_cmd_read_nvparms_t	*read_nvparms = buf;

	ocs_memset(buf, 0, size);

	read_nvparms->hdr.command = SLI4_MBOX_COMMAND_READ_NVPARMS;

	return sizeof(sli4_cmd_read_nvparms_t);
}

/**
 * @brief Write a WRITE_NVPARMS command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param wwpn WWPN to write - pointer to array of 8 uint8_t.
 * @param wwnn WWNN to write - pointer to array of 8 uint8_t.
 * @param hard_alpa Hard ALPA to write.
 * @param preferred_d_id  Preferred D_ID to write.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_write_nvparms(sli4_t *sli4, void *buf, size_t size, uint8_t *wwpn, uint8_t *wwnn, uint8_t hard_alpa,
		uint32_t preferred_d_id)
{
	sli4_cmd_write_nvparms_t	*write_nvparms = buf;

	ocs_memset(buf, 0, size);

	write_nvparms->hdr.command = SLI4_MBOX_COMMAND_WRITE_NVPARMS;
	ocs_memcpy(write_nvparms->wwpn, wwpn, 8);
	ocs_memcpy(write_nvparms->wwnn, wwnn, 8);
	write_nvparms->hard_alpa = hard_alpa;
	write_nvparms->preferred_d_id = preferred_d_id;

	return sizeof(sli4_cmd_write_nvparms_t);
}

/**
 * @brief Write a READ_REV command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param vpd Pointer to the buffer.
 *
 * @return Returns the number of bytes written.
 */
static int32_t
sli_cmd_read_rev(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *vpd)
{
	sli4_cmd_read_rev_t	*read_rev = buf;

	ocs_memset(buf, 0, size);

	read_rev->hdr.command = SLI4_MBOX_COMMAND_READ_REV;

	if (vpd && vpd->size) {
		read_rev->vpd = TRUE;

		read_rev->available_length = vpd->size;

		read_rev->physical_address_low  = ocs_addr32_lo(vpd->phys);
		read_rev->physical_address_high = ocs_addr32_hi(vpd->phys);
	}

	return sizeof(sli4_cmd_read_rev_t);
}

/**
 * @ingroup sli
 * @brief Write a READ_SPARM64 command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param dma DMA buffer for the service parameters.
 * @param vpi VPI used to determine the WWN.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_read_sparm64(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *dma,
		uint16_t vpi)
{
	sli4_cmd_read_sparm64_t	*read_sparm64 = buf;

	ocs_memset(buf, 0, size);

	if (SLI4_READ_SPARM64_VPI_SPECIAL == vpi) {
		ocs_log_test(sli4->os, "special VPI not supported!!!\n");
		return -1;
	}

	if (!dma || !dma->phys) {
		ocs_log_test(sli4->os, "bad DMA buffer\n");
		return -1;
	}

	read_sparm64->hdr.command = SLI4_MBOX_COMMAND_READ_SPARM64;

	read_sparm64->bde_64.bde_type = SLI4_BDE_TYPE_BDE_64;
	read_sparm64->bde_64.buffer_length = dma->size;
	read_sparm64->bde_64.u.data.buffer_address_low  = ocs_addr32_lo(dma->phys);
	read_sparm64->bde_64.u.data.buffer_address_high = ocs_addr32_hi(dma->phys);

	read_sparm64->vpi = vpi;

	return sizeof(sli4_cmd_read_sparm64_t);
}

/**
 * @ingroup sli
 * @brief Write a READ_TOPOLOGY command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param dma DMA buffer for loop map (optional).
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_read_topology(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *dma)
{
	sli4_cmd_read_topology_t *read_topo = buf;

	ocs_memset(buf, 0, size);

	read_topo->hdr.command = SLI4_MBOX_COMMAND_READ_TOPOLOGY;

	if (dma && dma->size) {
		if (dma->size < SLI4_MIN_LOOP_MAP_BYTES) {
			ocs_log_test(sli4->os, "loop map buffer too small %jd\n",
					dma->size);
			return 0;
		}

		ocs_memset(dma->virt, 0, dma->size);

		read_topo->bde_loop_map.bde_type = SLI4_BDE_TYPE_BDE_64;
		read_topo->bde_loop_map.buffer_length = dma->size;
		read_topo->bde_loop_map.u.data.buffer_address_low  = ocs_addr32_lo(dma->phys);
		read_topo->bde_loop_map.u.data.buffer_address_high = ocs_addr32_hi(dma->phys);
	}

	return sizeof(sli4_cmd_read_topology_t);
}

/**
 * @ingroup sli
 * @brief Write a REG_FCFI command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param index FCF index returned by READ_FCF_TABLE.
 * @param rq_cfg RQ_ID/R_CTL/TYPE routing information
 * @param vlan_id VLAN ID tag.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_reg_fcfi(sli4_t *sli4, void *buf, size_t size, uint16_t index, sli4_cmd_rq_cfg_t rq_cfg[SLI4_CMD_REG_FCFI_NUM_RQ_CFG], uint16_t vlan_id)
{
	sli4_cmd_reg_fcfi_t	*reg_fcfi = buf;
	uint32_t		i;

	ocs_memset(buf, 0, size);

	reg_fcfi->hdr.command = SLI4_MBOX_COMMAND_REG_FCFI;

	reg_fcfi->fcf_index = index;

	for (i = 0; i < SLI4_CMD_REG_FCFI_NUM_RQ_CFG; i++) {
		switch(i) {
		case 0:	reg_fcfi->rq_id_0 = rq_cfg[0].rq_id; break;
		case 1:	reg_fcfi->rq_id_1 = rq_cfg[1].rq_id; break;
		case 2:	reg_fcfi->rq_id_2 = rq_cfg[2].rq_id; break;
		case 3:	reg_fcfi->rq_id_3 = rq_cfg[3].rq_id; break;
		}
		reg_fcfi->rq_cfg[i].r_ctl_mask = rq_cfg[i].r_ctl_mask;
		reg_fcfi->rq_cfg[i].r_ctl_match = rq_cfg[i].r_ctl_match;
		reg_fcfi->rq_cfg[i].type_mask = rq_cfg[i].type_mask;
		reg_fcfi->rq_cfg[i].type_match = rq_cfg[i].type_match;
	}

	if (vlan_id) {
		reg_fcfi->vv = TRUE;
		reg_fcfi->vlan_tag = vlan_id;
	}

	return sizeof(sli4_cmd_reg_fcfi_t);
}

/**
 * @brief Write REG_FCFI_MRQ to provided command buffer
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param fcf_index FCF index returned by READ_FCF_TABLE.
 * @param vlan_id VLAN ID tag.
 * @param rr_quant Round robin quanta if RQ selection policy is 2
 * @param rq_selection_policy RQ selection policy
 * @param num_rqs Array of count of RQs per filter
 * @param rq_ids Array of RQ ids per filter
 * @param rq_cfg RQ_ID/R_CTL/TYPE routing information
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t
sli_cmd_reg_fcfi_mrq(sli4_t *sli4, void *buf, size_t size, uint8_t mode,
		     uint16_t fcf_index, uint16_t vlan_id, uint8_t rq_selection_policy,
		     uint8_t mrq_bit_mask, uint16_t num_mrqs,
		     sli4_cmd_rq_cfg_t rq_cfg[SLI4_CMD_REG_FCFI_NUM_RQ_CFG])
{
	sli4_cmd_reg_fcfi_mrq_t	*reg_fcfi_mrq = buf;
	uint32_t i;

	ocs_memset(buf, 0, size);

	reg_fcfi_mrq->hdr.command = SLI4_MBOX_COMMAND_REG_FCFI_MRQ;
	if (mode == SLI4_CMD_REG_FCFI_SET_FCFI_MODE) {
		reg_fcfi_mrq->fcf_index = fcf_index;
		if (vlan_id) {
			reg_fcfi_mrq->vv = TRUE;
			reg_fcfi_mrq->vlan_tag = vlan_id;
		}
		goto done;
	}

	reg_fcfi_mrq->mode = mode;
	for (i = 0; i < SLI4_CMD_REG_FCFI_NUM_RQ_CFG; i++) {
		reg_fcfi_mrq->rq_cfg[i].r_ctl_mask = rq_cfg[i].r_ctl_mask;
		reg_fcfi_mrq->rq_cfg[i].r_ctl_match = rq_cfg[i].r_ctl_match;
		reg_fcfi_mrq->rq_cfg[i].type_mask = rq_cfg[i].type_mask;
		reg_fcfi_mrq->rq_cfg[i].type_match = rq_cfg[i].type_match;

		switch(i) {
		case 3:	reg_fcfi_mrq->rq_id_3 = rq_cfg[i].rq_id; break;
		case 2:	reg_fcfi_mrq->rq_id_2 = rq_cfg[i].rq_id; break;
		case 1:	reg_fcfi_mrq->rq_id_1 = rq_cfg[i].rq_id; break;
		case 0:	reg_fcfi_mrq->rq_id_0 = rq_cfg[i].rq_id; break;
		}
	}

	reg_fcfi_mrq->rq_selection_policy = rq_selection_policy;
	reg_fcfi_mrq->mrq_filter_bitmask = mrq_bit_mask;
	reg_fcfi_mrq->num_mrq_pairs = num_mrqs;
done:
	return sizeof(sli4_cmd_reg_fcfi_mrq_t);
}

/**
 * @ingroup sli
 * @brief Write a REG_RPI command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param nport_id Remote F/N_Port_ID.
 * @param rpi Previously-allocated Remote Port Indicator.
 * @param vpi Previously-allocated Virtual Port Indicator.
 * @param dma DMA buffer that contains the remote port's service parameters.
 * @param update Boolean indicating an update to an existing RPI (TRUE)
 * or a new registration (FALSE).
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_reg_rpi(sli4_t *sli4, void *buf, size_t size, uint32_t nport_id, uint16_t rpi, 
			uint16_t vpi, ocs_dma_t *dma, uint8_t update,  uint8_t enable_t10_pi)
{
	sli4_cmd_reg_rpi_t *reg_rpi = buf;

	ocs_memset(buf, 0, size);

	reg_rpi->hdr.command = SLI4_MBOX_COMMAND_REG_RPI;

	reg_rpi->rpi = rpi;
	reg_rpi->remote_n_port_id = nport_id;
	reg_rpi->upd = update;
	reg_rpi->etow = enable_t10_pi;

	reg_rpi->bde_64.bde_type = SLI4_BDE_TYPE_BDE_64;
	reg_rpi->bde_64.buffer_length = SLI4_REG_RPI_BUF_LEN;
	reg_rpi->bde_64.u.data.buffer_address_low  = ocs_addr32_lo(dma->phys);
	reg_rpi->bde_64.u.data.buffer_address_high = ocs_addr32_hi(dma->phys);

	reg_rpi->vpi = vpi;

	return sizeof(sli4_cmd_reg_rpi_t);
}

/**
 * @ingroup sli
 * @brief Write a REG_VFI command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param domain Pointer to the domain object.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_reg_vfi(sli4_t *sli4, void *buf, size_t size, ocs_domain_t *domain)
{
	sli4_cmd_reg_vfi_t	*reg_vfi = buf;

	if (!sli4 || !buf || !domain) {
		return 0;
	}

	ocs_memset(buf, 0, size);

	reg_vfi->hdr.command = SLI4_MBOX_COMMAND_REG_VFI;

	reg_vfi->vfi = domain->indicator;

	reg_vfi->fcfi = domain->fcf_indicator;

	/* TODO contents of domain->dma only valid if topo == FABRIC */
	reg_vfi->sparm.bde_type = SLI4_BDE_TYPE_BDE_64;
	reg_vfi->sparm.buffer_length = 0x70;
	reg_vfi->sparm.u.data.buffer_address_low  = ocs_addr32_lo(domain->dma.phys);
	reg_vfi->sparm.u.data.buffer_address_high = ocs_addr32_hi(domain->dma.phys);

	reg_vfi->e_d_tov = sli4->config.e_d_tov;
	reg_vfi->r_a_tov = sli4->config.r_a_tov;

	reg_vfi->vp = TRUE;
	reg_vfi->vpi = domain->sport->indicator;
	ocs_memcpy(reg_vfi->wwpn, &domain->sport->sli_wwpn, sizeof(reg_vfi->wwpn));
	reg_vfi->local_n_port_id = domain->sport->fc_id;

	return sizeof(sli4_cmd_reg_vfi_t);
}

/**
 * @ingroup sli
 * @brief Write a REG_VPI command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param sport Point to SLI Port object.
 * @param update Boolean indicating whether to update the existing VPI (true)
 * or create a new VPI (false).
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_reg_vpi(sli4_t *sli4, void *buf, size_t size, ocs_sli_port_t *sport, uint8_t update)
{
	sli4_cmd_reg_vpi_t	*reg_vpi = buf;

	if (!sli4 || !buf || !sport) {
		return 0;
	}

	ocs_memset(buf, 0, size);

	reg_vpi->hdr.command = SLI4_MBOX_COMMAND_REG_VPI;

	reg_vpi->local_n_port_id = sport->fc_id;
	reg_vpi->upd = update != 0;
	ocs_memcpy(reg_vpi->wwpn, &sport->sli_wwpn, sizeof(reg_vpi->wwpn));
	reg_vpi->vpi = sport->indicator;
	reg_vpi->vfi = sport->domain->indicator;

	return sizeof(sli4_cmd_reg_vpi_t);
}

/**
 * @brief Write a REQUEST_FEATURES command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param mask Features to request.
 * @param query Use feature query mode (does not change FW).
 *
 * @return Returns the number of bytes written.
 */
static int32_t
sli_cmd_request_features(sli4_t *sli4, void *buf, size_t size, sli4_features_t mask, uint8_t query)
{
	sli4_cmd_request_features_t *features = buf;

	ocs_memset(buf, 0, size);

	features->hdr.command = SLI4_MBOX_COMMAND_REQUEST_FEATURES;

	if (query) {
		features->qry = TRUE;
	}
	features->command.dword = mask.dword;

	return sizeof(sli4_cmd_request_features_t);
}

/**
 * @ingroup sli
 * @brief Write a SLI_CONFIG command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param length Length in bytes of attached command.
 * @param dma DMA buffer for non-embedded commands.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_sli_config(sli4_t *sli4, void *buf, size_t size, uint32_t length, ocs_dma_t *dma)
{
	sli4_cmd_sli_config_t	*sli_config = NULL;

	if ((length > sizeof(sli_config->payload.embed)) && (dma == NULL)) {
		ocs_log_test(sli4->os, "length(%d) > payload(%ld)\n",
				length, sizeof(sli_config->payload.embed));
		return -1;
	}

	sli_config = buf;

	ocs_memset(buf, 0, size);

	sli_config->hdr.command = SLI4_MBOX_COMMAND_SLI_CONFIG;
	if (NULL == dma) {
		sli_config->emb = TRUE;
		sli_config->payload_length = length;
	} else {
		sli_config->emb = FALSE;

		sli_config->pmd_count = 1;

		sli_config->payload.mem.address_low = ocs_addr32_lo(dma->phys);
		sli_config->payload.mem.address_high = ocs_addr32_hi(dma->phys);
		sli_config->payload.mem.length = dma->size;
		sli_config->payload_length = dma->size;
#if defined(OCS_INCLUDE_DEBUG)
		/* save pointer to DMA for BMBX dumping purposes */
		sli4->bmbx_non_emb_pmd = dma;
#endif

	}

	return offsetof(sli4_cmd_sli_config_t, payload.embed);
}

/**
 * @brief Initialize SLI Port control register.
 *
 * @param sli4 SLI context pointer.
 * @param endian Endian value to write.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */

static int32_t
sli_sliport_control(sli4_t *sli4, uint32_t endian)
{
	uint32_t iter;
	int32_t rc;

	rc = -1;

	/* Initialize port, endian */
	sli_reg_write(sli4, SLI4_REG_SLIPORT_CONTROL, endian | SLI4_SLIPORT_CONTROL_IP);

	for (iter = 0; iter < 3000; iter ++) {
		ocs_udelay(SLI4_INIT_PORT_DELAY_US);
		if (sli_fw_ready(sli4) == 1) {
			rc = 0;
			break;
		}
	}

	if (rc != 0) {
		ocs_log_crit(sli4->os, "port failed to become ready after initialization\n");
	}

	return rc;
}

/**
 * @ingroup sli
 * @brief Write a UNREG_FCFI command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param indicator Indicator value.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_unreg_fcfi(sli4_t *sli4, void *buf, size_t size, uint16_t indicator)
{
	sli4_cmd_unreg_fcfi_t	*unreg_fcfi = buf;

	if (!sli4 || !buf) {
		return 0;
	}

	ocs_memset(buf, 0, size);

	unreg_fcfi->hdr.command = SLI4_MBOX_COMMAND_UNREG_FCFI;

	unreg_fcfi->fcfi = indicator;

	return sizeof(sli4_cmd_unreg_fcfi_t);
}

/**
 * @ingroup sli
 * @brief Write an UNREG_RPI command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param indicator Indicator value.
 * @param which Type of unregister, such as node, port, domain, or FCF.
 * @param fc_id FC address.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_unreg_rpi(sli4_t *sli4, void *buf, size_t size, uint16_t indicator, sli4_resource_e which,
		uint32_t fc_id)
{
	sli4_cmd_unreg_rpi_t	*unreg_rpi = buf;
	uint8_t		index_indicator = 0;

	if (!sli4 || !buf) {
		return 0;
	}

	ocs_memset(buf, 0, size);

	unreg_rpi->hdr.command = SLI4_MBOX_COMMAND_UNREG_RPI;

	switch (which) {
	case SLI_RSRC_FCOE_RPI:
		index_indicator = SLI4_UNREG_RPI_II_RPI;
		if (fc_id != UINT32_MAX) {
			unreg_rpi->dp = TRUE;
			unreg_rpi->destination_n_port_id = fc_id & 0x00ffffff;
		}
		break;
	case SLI_RSRC_FCOE_VPI:
		index_indicator = SLI4_UNREG_RPI_II_VPI;
		break;
	case SLI_RSRC_FCOE_VFI:
		index_indicator = SLI4_UNREG_RPI_II_VFI;
		break;
	case SLI_RSRC_FCOE_FCFI:
		index_indicator = SLI4_UNREG_RPI_II_FCFI;
		break;
	default:
		ocs_log_test(sli4->os, "unknown type %#x\n", which);
		return 0;
	}

	unreg_rpi->ii = index_indicator;
	unreg_rpi->index = indicator;

	return sizeof(sli4_cmd_unreg_rpi_t);
}

/**
 * @ingroup sli
 * @brief Write an UNREG_VFI command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param domain Pointer to the domain object
 * @param which Type of unregister, such as domain, FCFI, or everything.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_unreg_vfi(sli4_t *sli4, void *buf, size_t size, ocs_domain_t *domain, uint32_t which)
{
	sli4_cmd_unreg_vfi_t	*unreg_vfi = buf;

	if (!sli4 || !buf || !domain) {
		return 0;
	}

	ocs_memset(buf, 0, size);

	unreg_vfi->hdr.command = SLI4_MBOX_COMMAND_UNREG_VFI;
	switch (which) {
	case SLI4_UNREG_TYPE_DOMAIN:
		unreg_vfi->index = domain->indicator;
		break;
	case SLI4_UNREG_TYPE_FCF:
		unreg_vfi->index = domain->fcf_indicator;
		break;
	case SLI4_UNREG_TYPE_ALL:
		unreg_vfi->index = UINT16_MAX;
		break;
	default:
		return 0;
	}

	if (SLI4_UNREG_TYPE_DOMAIN != which) {
		unreg_vfi->ii = SLI4_UNREG_VFI_II_FCFI;
	}

	return sizeof(sli4_cmd_unreg_vfi_t);
}

/**
 * @ingroup sli
 * @brief Write an UNREG_VPI command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param indicator Indicator value.
 * @param which Type of unregister: port, domain, FCFI, everything
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_unreg_vpi(sli4_t *sli4, void *buf, size_t size, uint16_t indicator, uint32_t which)
{
	sli4_cmd_unreg_vpi_t	*unreg_vpi = buf;

	if (!sli4 || !buf) {
		return 0;
	}

	ocs_memset(buf, 0, size);

	unreg_vpi->hdr.command = SLI4_MBOX_COMMAND_UNREG_VPI;
	unreg_vpi->index = indicator;
	switch (which) {
	case SLI4_UNREG_TYPE_PORT:
		unreg_vpi->ii = SLI4_UNREG_VPI_II_VPI;
		break;
	case SLI4_UNREG_TYPE_DOMAIN:
		unreg_vpi->ii = SLI4_UNREG_VPI_II_VFI;
		break;
	case SLI4_UNREG_TYPE_FCF:
		unreg_vpi->ii = SLI4_UNREG_VPI_II_FCFI;
		break;
	case SLI4_UNREG_TYPE_ALL:
		unreg_vpi->index = UINT16_MAX;	/* override indicator */
		unreg_vpi->ii = SLI4_UNREG_VPI_II_FCFI;
		break;
	default:
		return 0;
	}

	return sizeof(sli4_cmd_unreg_vpi_t);
}


/**
 * @ingroup sli
 * @brief Write an CONFIG_AUTO_XFER_RDY command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param max_burst_len if the write FCP_DL is less than this size,
 * then the SLI port will generate the auto XFER_RDY.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_config_auto_xfer_rdy(sli4_t *sli4, void *buf, size_t size, uint32_t max_burst_len)
{
	sli4_cmd_config_auto_xfer_rdy_t	*req = buf;

	if (!sli4 || !buf) {
		return 0;
	}

	ocs_memset(buf, 0, size);

	req->hdr.command = SLI4_MBOX_COMMAND_CONFIG_AUTO_XFER_RDY;
	req->max_burst_len = max_burst_len;

	return sizeof(sli4_cmd_config_auto_xfer_rdy_t);
}

/**
 * @ingroup sli
 * @brief Write an CONFIG_AUTO_XFER_RDY_HP command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to the destination buffer.
 * @param size Buffer size, in bytes.
 * @param max_burst_len if the write FCP_DL is less than this size,
 * @param esoc enable start offset computation,
 * @param block_size block size,
 * then the SLI port will generate the auto XFER_RDY.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_config_auto_xfer_rdy_hp(sli4_t *sli4, void *buf, size_t size, uint32_t max_burst_len,
                                                uint32_t esoc, uint32_t block_size )
{
        sli4_cmd_config_auto_xfer_rdy_hp_t      *req = buf;

        if (!sli4 || !buf) {
                return 0;
        }

        ocs_memset(buf, 0, size);

        req->hdr.command = SLI4_MBOX_COMMAND_CONFIG_AUTO_XFER_RDY_HP;
        req->max_burst_len = max_burst_len;
        req->esoc = esoc;
        req->block_size = block_size;
        return sizeof(sli4_cmd_config_auto_xfer_rdy_hp_t);
}

/**
 * @brief Write a COMMON_FUNCTION_RESET command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 *
 * @return Returns the number of bytes written.
 */
static int32_t
sli_cmd_common_function_reset(sli4_t *sli4, void *buf, size_t size)
{
	sli4_req_common_function_reset_t *reset = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		uint32_t payload_size;

		/* Payload length must accommodate both request and response */
		payload_size = max(sizeof(sli4_req_common_function_reset_t),
				sizeof(sli4_res_common_function_reset_t));

		sli_config_off = sli_cmd_sli_config(sli4, buf, size, payload_size,
				NULL);
	}
	reset = (sli4_req_common_function_reset_t *)((uint8_t *)buf + sli_config_off);

	reset->hdr.opcode = SLI4_OPC_COMMON_FUNCTION_RESET;
	reset->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;

	return(sli_config_off + sizeof(sli4_req_common_function_reset_t));
}

/**
 * @brief Write a COMMON_CREATE_CQ command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param qmem DMA memory for the queue.
 * @param eq_id Associated EQ_ID
 * @param ignored This parameter carries the ULP which is only used for WQ and RQs
 *
 * @note This creates a Version 0 message.
 *
 * @return Returns the number of bytes written.
 */
static int32_t
sli_cmd_common_create_cq(sli4_t *sli4, void *buf, size_t size,
		ocs_dma_t *qmem, uint16_t eq_id, uint16_t ignored)
{
	sli4_req_common_create_cq_v0_t	*cqv0 = NULL;
	sli4_req_common_create_cq_v2_t	*cqv2 = NULL;
	uint32_t	sli_config_off = 0;
	uint32_t	p;
	uintptr_t	addr;
	uint32_t	if_type = sli4->if_type;
	uint32_t	page_bytes = 0;
	uint32_t	num_pages = 0;
	uint32_t 	cmd_size = 0;
	uint32_t	page_size = 0;
	uint32_t	n_cqe = 0;

	/* First calculate number of pages and the mailbox cmd length */
	switch (if_type)
	{
	case SLI4_IF_TYPE_BE3_SKH_PF:
		page_bytes = SLI_PAGE_SIZE;
		num_pages = sli_page_count(qmem->size, page_bytes);
		cmd_size = sizeof(sli4_req_common_create_cq_v0_t) + (8 * num_pages);
		break;
	case SLI4_IF_TYPE_LANCER_FC_ETH:
		n_cqe = qmem->size / SLI4_CQE_BYTES;
		switch (n_cqe) {
		case 256:
		case 512:
		case 1024:
		case 2048:
			page_size = 1;
			break;
		case 4096:
			page_size = 2;
			break;
		default:
			return 0;
		}
		page_bytes = page_size * SLI_PAGE_SIZE;
		num_pages = sli_page_count(qmem->size, page_bytes);
		cmd_size = sizeof(sli4_req_common_create_cq_v2_t) + (8 * num_pages);
		break;
	default:
		ocs_log_test(sli4->os, "unsupported IF_TYPE %d\n", if_type);
		return -1;
	}


	/* now that we have the mailbox command size, we can set SLI_CONFIG fields */
	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		uint32_t payload_size;

		/* Payload length must accommodate both request and response */
		payload_size = max((size_t)cmd_size, sizeof(sli4_res_common_create_queue_t));

		sli_config_off = sli_cmd_sli_config(sli4, buf, size, payload_size,
				NULL);
	}

	switch (if_type)
	{
	case SLI4_IF_TYPE_BE3_SKH_PF:
		cqv0 = (sli4_req_common_create_cq_v0_t *)((uint8_t *)buf + sli_config_off);
		cqv0->hdr.opcode = SLI4_OPC_COMMON_CREATE_CQ;
		cqv0->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
		cqv0->hdr.version = 0;
		cqv0->hdr.request_length = cmd_size - sizeof(sli4_req_hdr_t);

		/* valid values for number of pages: 1, 2, 4 (sec 4.4.3) */
		cqv0->num_pages = num_pages;
		switch (cqv0->num_pages) {
		case 1:
			cqv0->cqecnt = SLI4_CQ_CNT_256;
			break;
		case 2:
			cqv0->cqecnt = SLI4_CQ_CNT_512;
			break;
		case 4:
			cqv0->cqecnt = SLI4_CQ_CNT_1024;
			break;
		default:
			ocs_log_test(sli4->os, "num_pages %d not valid\n", cqv0->num_pages);
			return -1;
		}
		cqv0->evt = TRUE;
		cqv0->valid = TRUE;
		/* TODO cq->nodelay = ???; */
		/* TODO cq->clswm = ???; */
		cqv0->arm = FALSE;
		cqv0->eq_id = eq_id;

		for (p = 0, addr = qmem->phys;
				p < cqv0->num_pages;
				p++, addr += page_bytes) {
			cqv0->page_physical_address[p].low = ocs_addr32_lo(addr);
			cqv0->page_physical_address[p].high = ocs_addr32_hi(addr);
		}

		break;
	case SLI4_IF_TYPE_LANCER_FC_ETH:
	{
		cqv2 = (sli4_req_common_create_cq_v2_t *)((uint8_t *)buf + sli_config_off);
		cqv2->hdr.opcode = SLI4_OPC_COMMON_CREATE_CQ;
		cqv2->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
		cqv2->hdr.version = 2;
		cqv2->hdr.request_length = cmd_size - sizeof(sli4_req_hdr_t);

		cqv2->page_size = page_size;

		/* valid values for number of pages: 1, 2, 4, 8 (sec 4.4.3) */
		cqv2->num_pages = num_pages;
		if (!cqv2->num_pages || (cqv2->num_pages > SLI4_COMMON_CREATE_CQ_V2_MAX_PAGES)) {
			return 0;
		}

		switch (cqv2->num_pages) {
		case 1:
			cqv2->cqecnt = SLI4_CQ_CNT_256;
			break;
		case 2:
			cqv2->cqecnt = SLI4_CQ_CNT_512;
			break;
		case 4:
			cqv2->cqecnt = SLI4_CQ_CNT_1024;
			break;
		case 8:
			cqv2->cqecnt = SLI4_CQ_CNT_LARGE;
			cqv2->cqe_count = n_cqe;
			break;
		default:
			ocs_log_test(sli4->os, "num_pages %d not valid\n", cqv2->num_pages);
			return -1;
		}

		cqv2->evt = TRUE;
		cqv2->valid = TRUE;
		/* TODO cq->nodelay = ???; */
		/* TODO cq->clswm = ???; */
		cqv2->arm = FALSE;
		cqv2->eq_id = eq_id;

		for (p = 0, addr = qmem->phys;
				p < cqv2->num_pages;
				p++, addr += page_bytes) {
			cqv2->page_physical_address[p].low = ocs_addr32_lo(addr);
			cqv2->page_physical_address[p].high = ocs_addr32_hi(addr);
		}
	}
		break;
	}	

	return (sli_config_off + cmd_size);
}

/**
 * @brief Write a COMMON_DESTROY_CQ command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param cq_id CQ ID
 *
 * @note This creates a Version 0 message.
 *
 * @return Returns the number of bytes written.
 */
static int32_t
sli_cmd_common_destroy_cq(sli4_t *sli4, void *buf, size_t size, uint16_t cq_id)
{
	sli4_req_common_destroy_cq_t	*cq = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				/* Payload length must accommodate both request and response */
				max(sizeof(sli4_req_common_destroy_cq_t),
					sizeof(sli4_res_hdr_t)),
				NULL);
	}
	cq = (sli4_req_common_destroy_cq_t *)((uint8_t *)buf + sli_config_off);

	cq->hdr.opcode = SLI4_OPC_COMMON_DESTROY_CQ;
	cq->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
	cq->hdr.request_length = sizeof(sli4_req_common_destroy_cq_t) -
					sizeof(sli4_req_hdr_t);
	cq->cq_id = cq_id;

	return(sli_config_off + sizeof(sli4_req_common_destroy_cq_t));
}

/**
 * @brief Write a COMMON_MODIFY_EQ_DELAY command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param q Queue object array.
 * @param num_q Queue object array count.
 * @param shift Phase shift for staggering interrupts.
 * @param delay_mult Delay multiplier for limiting interrupt frequency.
 *
 * @return Returns the number of bytes written.
 */
static int32_t
sli_cmd_common_modify_eq_delay(sli4_t *sli4, void *buf, size_t size, sli4_queue_t *q, int num_q, uint32_t shift,
				uint32_t delay_mult)
{
	sli4_req_common_modify_eq_delay_t *modify_delay = NULL;
	uint32_t	sli_config_off = 0;
	int i;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				/* Payload length must accommodate both request and response */
				max(sizeof(sli4_req_common_modify_eq_delay_t), sizeof(sli4_res_hdr_t)),
				NULL);
	}

	modify_delay = (sli4_req_common_modify_eq_delay_t *)((uint8_t *)buf + sli_config_off);

	modify_delay->hdr.opcode = SLI4_OPC_COMMON_MODIFY_EQ_DELAY;
	modify_delay->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
	modify_delay->hdr.request_length = sizeof(sli4_req_common_modify_eq_delay_t) -
					sizeof(sli4_req_hdr_t);

	modify_delay->num_eq = num_q;

	for (i = 0; i<num_q; i++) {
		modify_delay->eq_delay_record[i].eq_id = q[i].id;
		modify_delay->eq_delay_record[i].phase = shift;
		modify_delay->eq_delay_record[i].delay_multiplier = delay_mult;
	}

	return(sli_config_off + sizeof(sli4_req_common_modify_eq_delay_t));
}

/**
 * @brief Write a COMMON_CREATE_EQ command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param qmem DMA memory for the queue.
 * @param ignored1 Ignored (used for consistency among queue creation functions).
 * @param ignored2 Ignored (used for consistency among queue creation functions).
 *
 * @note Other queue creation routines use the last parameter to pass in
 * the associated Q_ID and ULP. EQ doesn't have an associated queue or ULP,
 * so these parameters are ignored
 *
 * @note This creates a Version 0 message
 *
 * @return Returns the number of bytes written.
 */
static int32_t
sli_cmd_common_create_eq(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *qmem,
		uint16_t ignored1, uint16_t ignored2)
{
	sli4_req_common_create_eq_t	*eq = NULL;
	uint32_t	sli_config_off = 0;
	uint32_t	p;
	uintptr_t	addr;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		uint32_t payload_size;

		/* Payload length must accommodate both request and response */
		payload_size = max(sizeof(sli4_req_common_create_eq_t),
				sizeof(sli4_res_common_create_queue_t));

		sli_config_off = sli_cmd_sli_config(sli4, buf, size, payload_size,
				NULL);
	}
	eq = (sli4_req_common_create_eq_t *)((uint8_t *)buf + sli_config_off);

	eq->hdr.opcode = SLI4_OPC_COMMON_CREATE_EQ;
	eq->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
	eq->hdr.request_length = sizeof(sli4_req_common_create_eq_t) -
					sizeof(sli4_req_hdr_t);
	/* valid values for number of pages: 1, 2, 4 (sec 4.4.3) */
	eq->num_pages = qmem->size / SLI_PAGE_SIZE;
	switch (eq->num_pages) {
	case 1:
		eq->eqesz = SLI4_EQE_SIZE_4;
		eq->count = SLI4_EQ_CNT_1024;
		break;
	case 2:
		eq->eqesz = SLI4_EQE_SIZE_4;
		eq->count = SLI4_EQ_CNT_2048;
		break;
	case 4:
		eq->eqesz = SLI4_EQE_SIZE_4;
		eq->count = SLI4_EQ_CNT_4096;
		break;
	default:
		ocs_log_test(sli4->os, "num_pages %d not valid\n", eq->num_pages);
		return -1;
	}
	eq->valid = TRUE;
	eq->arm = FALSE;
	eq->delay_multiplier = 32;

	for (p = 0, addr = qmem->phys;
			p < eq->num_pages;
			p++, addr += SLI_PAGE_SIZE) {
		eq->page_address[p].low = ocs_addr32_lo(addr);
		eq->page_address[p].high = ocs_addr32_hi(addr);
	}

	return(sli_config_off + sizeof(sli4_req_common_create_eq_t));
}


/**
 * @brief Write a COMMON_DESTROY_EQ command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param eq_id Queue ID to destroy.
 *
 * @note Other queue creation routines use the last parameter to pass in
 * the associated Q_ID. EQ doesn't have an associated queue so this
 * parameter is ignored.
 *
 * @note This creates a Version 0 message.
 *
 * @return Returns the number of bytes written.
 */
static int32_t
sli_cmd_common_destroy_eq(sli4_t *sli4, void *buf, size_t size, uint16_t eq_id)
{
	sli4_req_common_destroy_eq_t	*eq = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				/* Payload length must accommodate both request and response */
				max(sizeof(sli4_req_common_destroy_eq_t),
					sizeof(sli4_res_hdr_t)),
				NULL);
	}
	eq = (sli4_req_common_destroy_eq_t *)((uint8_t *)buf + sli_config_off);

	eq->hdr.opcode = SLI4_OPC_COMMON_DESTROY_EQ;
	eq->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
	eq->hdr.request_length = sizeof(sli4_req_common_destroy_eq_t) -
					sizeof(sli4_req_hdr_t);

	eq->eq_id = eq_id;

	return(sli_config_off + sizeof(sli4_req_common_destroy_eq_t));
}

/**
 * @brief Write a LOWLEVEL_SET_WATCHDOG command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param timeout watchdog timer timeout in seconds
 *
 * @return void
 */
void
sli4_cmd_lowlevel_set_watchdog(sli4_t *sli4, void *buf, size_t size, uint16_t timeout)
{

	sli4_req_lowlevel_set_watchdog_t *req = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				/* Payload length must accommodate both request and response */
				max(sizeof(sli4_req_lowlevel_set_watchdog_t),
					sizeof(sli4_res_lowlevel_set_watchdog_t)),
				NULL);
	}
	req = (sli4_req_lowlevel_set_watchdog_t *)((uint8_t *)buf + sli_config_off);

	req->hdr.opcode = SLI4_OPC_LOWLEVEL_SET_WATCHDOG;
	req->hdr.subsystem = SLI4_SUBSYSTEM_LOWLEVEL;
	req->hdr.request_length = sizeof(sli4_req_lowlevel_set_watchdog_t) - sizeof(sli4_req_hdr_t);
	req->watchdog_timeout = timeout;
	
	return;
}

static int32_t
sli_cmd_common_get_cntl_attributes(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *dma)
{
	sli4_req_hdr_t *hdr = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				sizeof(sli4_req_hdr_t),
				dma);
	}

	if (dma == NULL) {
		return 0;
	}

	ocs_memset(dma->virt, 0, dma->size);

	hdr = dma->virt;

	hdr->opcode = SLI4_OPC_COMMON_GET_CNTL_ATTRIBUTES;
	hdr->subsystem = SLI4_SUBSYSTEM_COMMON;
	hdr->request_length = dma->size;

	return(sli_config_off + sizeof(sli4_req_hdr_t));
}

/**
 * @brief Write a COMMON_GET_CNTL_ADDL_ATTRIBUTES command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param dma DMA structure from which the data will be copied.
 *
 * @note This creates a Version 0 message.
 *
 * @return Returns the number of bytes written.
 */
static int32_t
sli_cmd_common_get_cntl_addl_attributes(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *dma)
{
	sli4_req_hdr_t *hdr = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size, sizeof(sli4_req_hdr_t), dma);
	}

	if (dma == NULL) {
		return 0;
	}

	ocs_memset(dma->virt, 0, dma->size);

	hdr = dma->virt;

	hdr->opcode = SLI4_OPC_COMMON_GET_CNTL_ADDL_ATTRIBUTES;
	hdr->subsystem = SLI4_SUBSYSTEM_COMMON;
	hdr->request_length = dma->size;

	return(sli_config_off + sizeof(sli4_req_hdr_t));
}

/**
 * @brief Write a COMMON_CREATE_MQ_EXT command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param qmem DMA memory for the queue.
 * @param cq_id Associated CQ_ID.
 * @param ignored This parameter carries the ULP which is only used for WQ and RQs
 *
 * @note This creates a Version 0 message.
 *
 * @return Returns the number of bytes written.
 */
static int32_t
sli_cmd_common_create_mq_ext(sli4_t *sli4, void *buf, size_t size,
			     ocs_dma_t *qmem, uint16_t cq_id, uint16_t ignored)
{
	sli4_req_common_create_mq_ext_t	*mq = NULL;
	uint32_t	sli_config_off = 0;
	uint32_t	p;
	uintptr_t	addr;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		uint32_t payload_size;

		/* Payload length must accommodate both request and response */
		payload_size = max(sizeof(sli4_req_common_create_mq_ext_t),
				sizeof(sli4_res_common_create_queue_t));

		sli_config_off = sli_cmd_sli_config(sli4, buf, size, payload_size,
				NULL);
	}
	mq = (sli4_req_common_create_mq_ext_t *)((uint8_t *)buf + sli_config_off);

	mq->hdr.opcode = SLI4_OPC_COMMON_CREATE_MQ_EXT;
	mq->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
	mq->hdr.request_length = sizeof(sli4_req_common_create_mq_ext_t) -
					sizeof(sli4_req_hdr_t);
	/* valid values for number of pages: 1, 2, 4, 8 (sec 4.4.12) */
	mq->num_pages = qmem->size / SLI_PAGE_SIZE;
	switch (mq->num_pages) {
	case 1:
		mq->ring_size = SLI4_MQE_SIZE_16;
		break;
	case 2:
		mq->ring_size = SLI4_MQE_SIZE_32;
		break;
	case 4:
		mq->ring_size = SLI4_MQE_SIZE_64;
		break;
	case 8:
		mq->ring_size = SLI4_MQE_SIZE_128;
		break;
	default:
		ocs_log_test(sli4->os, "num_pages %d not valid\n", mq->num_pages);
		return -1;
	}

	/* TODO break this down by sli4->config.topology */
	mq->async_event_bitmap = SLI4_ASYNC_EVT_FC_FCOE;

	if (sli4->config.mq_create_version) {
		mq->cq_id_v1 = cq_id;
		mq->hdr.version = 1;
	}
	else {
		mq->cq_id_v0 = cq_id;
	}
	mq->val = TRUE;

	for (p = 0, addr = qmem->phys;
			p < mq->num_pages;
			p++, addr += SLI_PAGE_SIZE) {
		mq->page_physical_address[p].low = ocs_addr32_lo(addr);
		mq->page_physical_address[p].high = ocs_addr32_hi(addr);
	}

	return(sli_config_off + sizeof(sli4_req_common_create_mq_ext_t));
}

/**
 * @brief Write a COMMON_DESTROY_MQ command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param mq_id MQ ID
 *
 * @note This creates a Version 0 message.
 *
 * @return Returns the number of bytes written.
 */
static int32_t
sli_cmd_common_destroy_mq(sli4_t *sli4, void *buf, size_t size, uint16_t mq_id)
{
	sli4_req_common_destroy_mq_t	*mq = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				/* Payload length must accommodate both request and response */
				max(sizeof(sli4_req_common_destroy_mq_t),
					sizeof(sli4_res_hdr_t)),
				NULL);
	}
	mq = (sli4_req_common_destroy_mq_t *)((uint8_t *)buf + sli_config_off);

	mq->hdr.opcode = SLI4_OPC_COMMON_DESTROY_MQ;
	mq->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
	mq->hdr.request_length = sizeof(sli4_req_common_destroy_mq_t) -
					sizeof(sli4_req_hdr_t);

	mq->mq_id = mq_id;

	return(sli_config_off + sizeof(sli4_req_common_destroy_mq_t));
}

/**
 * @ingroup sli
 * @brief Write a COMMON_NOP command
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param context NOP context value (passed to response, except on FC/FCoE).
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_nop(sli4_t *sli4, void *buf, size_t size, uint64_t context)
{
	sli4_req_common_nop_t *nop = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				/* Payload length must accommodate both request and response */
				max(sizeof(sli4_req_common_nop_t), sizeof(sli4_res_common_nop_t)),
				NULL);
	}

	nop = (sli4_req_common_nop_t *)((uint8_t *)buf + sli_config_off);

	nop->hdr.opcode = SLI4_OPC_COMMON_NOP;
	nop->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
	nop->hdr.request_length = 8;

	ocs_memcpy(&nop->context, &context, sizeof(context));

	return(sli_config_off + sizeof(sli4_req_common_nop_t));
}

/**
 * @ingroup sli
 * @brief Write a COMMON_GET_RESOURCE_EXTENT_INFO command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param rtype Resource type (for example, XRI, VFI, VPI, and RPI).
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_get_resource_extent_info(sli4_t *sli4, void *buf, size_t size, uint16_t rtype)
{
	sli4_req_common_get_resource_extent_info_t *extent = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				sizeof(sli4_req_common_get_resource_extent_info_t),
				NULL);
	}

	extent = (sli4_req_common_get_resource_extent_info_t *)((uint8_t *)buf + sli_config_off);

	extent->hdr.opcode = SLI4_OPC_COMMON_GET_RESOURCE_EXTENT_INFO;
	extent->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
	extent->hdr.request_length = 4;

	extent->resource_type = rtype;

	return(sli_config_off + sizeof(sli4_req_common_get_resource_extent_info_t));
}

/**
 * @ingroup sli
 * @brief Write a COMMON_GET_SLI4_PARAMETERS command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_get_sli4_parameters(sli4_t *sli4, void *buf, size_t size)
{
	sli4_req_hdr_t	*hdr = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				sizeof(sli4_res_common_get_sli4_parameters_t),
				NULL);
	}

	hdr = (sli4_req_hdr_t *)((uint8_t *)buf + sli_config_off);

	hdr->opcode = SLI4_OPC_COMMON_GET_SLI4_PARAMETERS;
	hdr->subsystem = SLI4_SUBSYSTEM_COMMON;
	hdr->request_length = 0x50;

	return(sli_config_off + sizeof(sli4_req_hdr_t));
}

/**
 * @brief Write a COMMON_QUERY_FW_CONFIG command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to destination buffer.
 * @param size Buffer size in bytes.
 *
 * @return Returns the number of bytes written
 */
static int32_t
sli_cmd_common_query_fw_config(sli4_t *sli4, void *buf, size_t size)
{
	sli4_req_common_query_fw_config_t   *fw_config;
	uint32_t	sli_config_off = 0;
	uint32_t payload_size;

	/* Payload length must accommodate both request and response */
	payload_size = max(sizeof(sli4_req_common_query_fw_config_t),
			   sizeof(sli4_res_common_query_fw_config_t));

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				payload_size,
				NULL);
	}

	fw_config = (sli4_req_common_query_fw_config_t*)((uint8_t*)buf + sli_config_off);
	fw_config->hdr.opcode	      = SLI4_OPC_COMMON_QUERY_FW_CONFIG;
	fw_config->hdr.subsystem      = SLI4_SUBSYSTEM_COMMON;
	fw_config->hdr.request_length = payload_size - sizeof(sli4_req_hdr_t);
	return sli_config_off + sizeof(sli4_req_common_query_fw_config_t);
}

/**
 * @brief Write a COMMON_GET_PORT_NAME command to the provided buffer.
 *
 * @param sli4 SLI context pointer.
 * @param buf Virtual pointer to destination buffer.
 * @param size Buffer size in bytes.
 *
 * @note Function supports both version 0 and 1 forms of this command via
 * the IF_TYPE.
 *
 * @return Returns the number of bytes written.
 */
static int32_t
sli_cmd_common_get_port_name(sli4_t *sli4, void *buf, size_t size)
{
	sli4_req_common_get_port_name_t	*port_name;
	uint32_t	sli_config_off = 0;
	uint32_t	payload_size;
	uint8_t		version = 0;
	uint8_t		pt = 0;

	/* Select command version according to IF_TYPE */
	switch (sli4->if_type) {
	case SLI4_IF_TYPE_BE3_SKH_PF:
	case SLI4_IF_TYPE_BE3_SKH_VF:
		version = 0;
		break;
	case SLI4_IF_TYPE_LANCER_FC_ETH:
	case SLI4_IF_TYPE_LANCER_RDMA:
		version = 1;
		break;
	default:
		ocs_log_test(sli4->os, "unsupported IF_TYPE %d\n", sli4->if_type);
		return 0;
	}

	/* Payload length must accommodate both request and response */
	payload_size = max(sizeof(sli4_req_common_get_port_name_t),
			   sizeof(sli4_res_common_get_port_name_t));

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				payload_size,
				NULL);

		pt = 1;
	}

	port_name = (sli4_req_common_get_port_name_t *)((uint8_t *)buf + sli_config_off);

	port_name->hdr.opcode		= SLI4_OPC_COMMON_GET_PORT_NAME;
	port_name->hdr.subsystem	= SLI4_SUBSYSTEM_COMMON;
	port_name->hdr.request_length	= sizeof(sli4_req_hdr_t) + (version * sizeof(uint32_t));
	port_name->hdr.version		= version;

	/* Set the port type value (ethernet=0, FC=1) for V1 commands */
	if (version == 1) {
		port_name->pt = pt;
	}

	return sli_config_off + port_name->hdr.request_length;
}


/**
 * @ingroup sli
 * @brief Write a COMMON_WRITE_OBJECT command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param noc True if the object should be written but not committed to flash.
 * @param eof True if this is the last write for this object.
 * @param desired_write_length Number of bytes of data to write to the object.
 * @param offset Offset, in bytes, from the start of the object.
 * @param object_name Name of the object to write.
 * @param dma DMA structure from which the data will be copied.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_write_object(sli4_t *sli4, void *buf, size_t size,
		uint16_t noc, uint16_t eof, uint32_t desired_write_length,
		uint32_t offset,
		char *object_name,
		ocs_dma_t *dma)
{
	sli4_req_common_write_object_t *wr_obj = NULL;
	uint32_t	sli_config_off = 0;
	sli4_bde_t *host_buffer;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				sizeof (sli4_req_common_write_object_t) + sizeof (sli4_bde_t),
				NULL);
	}

	wr_obj = (sli4_req_common_write_object_t *)((uint8_t *)buf + sli_config_off);

	wr_obj->hdr.opcode = SLI4_OPC_COMMON_WRITE_OBJECT;
	wr_obj->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
	wr_obj->hdr.request_length = sizeof(*wr_obj) - 4*sizeof(uint32_t) + sizeof(sli4_bde_t);
	wr_obj->hdr.timeout = 0;
	wr_obj->hdr.version = 0;

	wr_obj->noc = noc;
	wr_obj->eof = eof;
	wr_obj->desired_write_length = desired_write_length;
	wr_obj->write_offset = offset;
	ocs_strncpy(wr_obj->object_name, object_name, sizeof(wr_obj->object_name));
	wr_obj->host_buffer_descriptor_count = 1;

	host_buffer = (sli4_bde_t *)wr_obj->host_buffer_descriptor;

	/* Setup to transfer xfer_size bytes to device */
	host_buffer->bde_type = SLI4_BDE_TYPE_BDE_64;
	host_buffer->buffer_length = desired_write_length;
	host_buffer->u.data.buffer_address_low = ocs_addr32_lo(dma->phys);
	host_buffer->u.data.buffer_address_high = ocs_addr32_hi(dma->phys);


	return(sli_config_off + sizeof(sli4_req_common_write_object_t) + sizeof (sli4_bde_t));
}


/**
 * @ingroup sli
 * @brief Write a COMMON_DELETE_OBJECT command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param object_name Name of the object to write.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_delete_object(sli4_t *sli4, void *buf, size_t size,
		char *object_name)
{
	sli4_req_common_delete_object_t *del_obj = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				sizeof (sli4_req_common_delete_object_t),
				NULL);
	}

	del_obj = (sli4_req_common_delete_object_t *)((uint8_t *)buf + sli_config_off);

	del_obj->hdr.opcode = SLI4_OPC_COMMON_DELETE_OBJECT;
	del_obj->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
	del_obj->hdr.request_length = sizeof(*del_obj);
	del_obj->hdr.timeout = 0;
	del_obj->hdr.version = 0;

	ocs_strncpy(del_obj->object_name, object_name, sizeof(del_obj->object_name));
	return(sli_config_off + sizeof(sli4_req_common_delete_object_t));
}

/**
 * @ingroup sli
 * @brief Write a COMMON_READ_OBJECT command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param desired_read_length Number of bytes of data to read from the object.
 * @param offset Offset, in bytes, from the start of the object.
 * @param object_name Name of the object to read.
 * @param dma DMA structure from which the data will be copied.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_read_object(sli4_t *sli4, void *buf, size_t size,
		uint32_t desired_read_length,
		uint32_t offset,
		char *object_name,
		ocs_dma_t *dma)
{
	sli4_req_common_read_object_t *rd_obj = NULL;
	uint32_t	sli_config_off = 0;
	sli4_bde_t *host_buffer;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				sizeof (sli4_req_common_read_object_t) + sizeof (sli4_bde_t),
				NULL);
	}

	rd_obj = (sli4_req_common_read_object_t *)((uint8_t *)buf + sli_config_off);

	rd_obj->hdr.opcode = SLI4_OPC_COMMON_READ_OBJECT;
	rd_obj->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
	rd_obj->hdr.request_length = sizeof(*rd_obj) - 4*sizeof(uint32_t) + sizeof(sli4_bde_t);
	rd_obj->hdr.timeout = 0;
	rd_obj->hdr.version = 0;

	rd_obj->desired_read_length = desired_read_length;
	rd_obj->read_offset = offset;
	ocs_strncpy(rd_obj->object_name, object_name, sizeof(rd_obj->object_name));
	rd_obj->host_buffer_descriptor_count = 1;

	host_buffer = (sli4_bde_t *)rd_obj->host_buffer_descriptor;

	/* Setup to transfer xfer_size bytes to device */
	host_buffer->bde_type = SLI4_BDE_TYPE_BDE_64;
	host_buffer->buffer_length = desired_read_length;
	if (dma != NULL) {
		host_buffer->u.data.buffer_address_low = ocs_addr32_lo(dma->phys);
		host_buffer->u.data.buffer_address_high = ocs_addr32_hi(dma->phys);
	} else {
		host_buffer->u.data.buffer_address_low = 0;
		host_buffer->u.data.buffer_address_high = 0;
	}


	return(sli_config_off + sizeof(sli4_req_common_read_object_t) + sizeof (sli4_bde_t));
}

/**
 * @ingroup sli
 * @brief Write a DMTF_EXEC_CLP_CMD command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param cmd DMA structure that describes the buffer for the command.
 * @param resp DMA structure that describes the buffer for the response.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_dmtf_exec_clp_cmd(sli4_t *sli4, void *buf, size_t size,
		ocs_dma_t *cmd,
		ocs_dma_t *resp)
{
	sli4_req_dmtf_exec_clp_cmd_t *clp_cmd = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				sizeof (sli4_req_dmtf_exec_clp_cmd_t),
				NULL);
	}

	clp_cmd = (sli4_req_dmtf_exec_clp_cmd_t*)((uint8_t *)buf + sli_config_off);

	clp_cmd->hdr.opcode = SLI4_OPC_DMTF_EXEC_CLP_CMD;
	clp_cmd->hdr.subsystem = SLI4_SUBSYSTEM_DMTF;
	clp_cmd->hdr.request_length = sizeof(sli4_req_dmtf_exec_clp_cmd_t) -
					sizeof(sli4_req_hdr_t);
	clp_cmd->hdr.timeout = 0;
	clp_cmd->hdr.version = 0;
	clp_cmd->cmd_buf_length = cmd->size;
	clp_cmd->cmd_buf_addr_low = ocs_addr32_lo(cmd->phys);
	clp_cmd->cmd_buf_addr_high = ocs_addr32_hi(cmd->phys);
	clp_cmd->resp_buf_length = resp->size;
	clp_cmd->resp_buf_addr_low = ocs_addr32_lo(resp->phys);
	clp_cmd->resp_buf_addr_high = ocs_addr32_hi(resp->phys);

	return(sli_config_off + sizeof(sli4_req_dmtf_exec_clp_cmd_t));
}

/**
 * @ingroup sli
 * @brief Write a COMMON_SET_DUMP_LOCATION command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param query Zero to set dump location, non-zero to query dump size
 * @param is_buffer_list Set to one if the buffer is a set of buffer descriptors or
 *                       set to 0 if the buffer is a contiguous dump area.
 * @param buffer DMA structure to which the dump will be copied.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_set_dump_location(sli4_t *sli4, void *buf, size_t size,
				 uint8_t query, uint8_t is_buffer_list,
				 ocs_dma_t *buffer, uint8_t fdb)
{
	sli4_req_common_set_dump_location_t *set_dump_loc = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				sizeof (sli4_req_common_set_dump_location_t),
				NULL);
	}

	set_dump_loc = (sli4_req_common_set_dump_location_t *)((uint8_t *)buf + sli_config_off);

	set_dump_loc->hdr.opcode = SLI4_OPC_COMMON_SET_DUMP_LOCATION;
	set_dump_loc->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
	set_dump_loc->hdr.request_length = sizeof(sli4_req_common_set_dump_location_t) - sizeof(sli4_req_hdr_t);
	set_dump_loc->hdr.timeout = 0;
	set_dump_loc->hdr.version = 0;

	set_dump_loc->blp = is_buffer_list;
	set_dump_loc->qry = query;
	set_dump_loc->fdb = fdb;

	if (buffer) {
		set_dump_loc->buf_addr_low = ocs_addr32_lo(buffer->phys);
		set_dump_loc->buf_addr_high = ocs_addr32_hi(buffer->phys);
		set_dump_loc->buffer_length = buffer->len;
	} else {
		set_dump_loc->buf_addr_low = 0;
		set_dump_loc->buf_addr_high = 0;
		set_dump_loc->buffer_length = 0;
	}

	return(sli_config_off + sizeof(sli4_req_common_set_dump_location_t));
}


/**
 * @ingroup sli
 * @brief Write a COMMON_SET_FEATURES command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param feature Feature to set.
 * @param param_len Length of the parameter (must be a multiple of 4 bytes).
 * @param parameter Pointer to the parameter value.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_set_features(sli4_t *sli4, void *buf, size_t size,
			    uint32_t feature,
			    uint32_t param_len,
			    void* parameter)
{
	sli4_req_common_set_features_t *cmd = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				sizeof (sli4_req_common_set_features_t),
				NULL);
	}

	cmd = (sli4_req_common_set_features_t *)((uint8_t *)buf + sli_config_off);

	cmd->hdr.opcode = SLI4_OPC_COMMON_SET_FEATURES;
	cmd->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
	cmd->hdr.request_length = sizeof(sli4_req_common_set_features_t) - sizeof(sli4_req_hdr_t);
	cmd->hdr.timeout = 0;
	cmd->hdr.version = 0;

	cmd->feature = feature;
	cmd->param_len = param_len;
	ocs_memcpy(cmd->params, parameter, param_len);

	return(sli_config_off + sizeof(sli4_req_common_set_features_t));
}

/**
 * @ingroup sli
 * @brief Write a COMMON_COMMON_GET_PROFILE_CONFIG command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size in bytes.
 * @param dma DMA capable memory used to retrieve profile.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_get_profile_config(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *dma)
{
        sli4_req_common_get_profile_config_t *req = NULL;
	uint32_t	sli_config_off = 0;
	uint32_t	payload_size;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size,
				sizeof (sli4_req_common_get_profile_config_t),
				dma);
	}

	if (dma != NULL) {
		req = dma->virt;
		ocs_memset(req, 0, dma->size);
		payload_size = dma->size;
	} else {
		req = (sli4_req_common_get_profile_config_t *)((uint8_t *)buf + sli_config_off);
		payload_size = sizeof(sli4_req_common_get_profile_config_t);
	}

        req->hdr.opcode = SLI4_OPC_COMMON_GET_PROFILE_CONFIG;
        req->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
        req->hdr.request_length = payload_size - sizeof(sli4_req_hdr_t);
        req->hdr.version = 1;

        return(sli_config_off + sizeof(sli4_req_common_get_profile_config_t));
}

/**
 * @ingroup sli
 * @brief Write a COMMON_COMMON_SET_PROFILE_CONFIG command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param dma DMA capable memory containing profile.
 * @param profile_id Profile ID to configure.
 * @param descriptor_count Number of descriptors in DMA buffer.
 * @param isap Implicit Set Active Profile value to use.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_set_profile_config(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *dma,
		uint8_t profile_id, uint32_t descriptor_count, uint8_t isap)
{
        sli4_req_common_set_profile_config_t *req = NULL;
        uint32_t cmd_off = 0;
        uint32_t payload_size;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		cmd_off = sli_cmd_sli_config(sli4, buf, size,
						    sizeof (sli4_req_common_set_profile_config_t),
						    dma);
	}

	if (dma != NULL) {
		req = dma->virt;
		ocs_memset(req, 0, dma->size);
		payload_size = dma->size;
	} else {
		req = (sli4_req_common_set_profile_config_t *)((uint8_t *)buf + cmd_off);
		payload_size = sizeof(sli4_req_common_set_profile_config_t);
	}

        req->hdr.opcode = SLI4_OPC_COMMON_SET_PROFILE_CONFIG;
        req->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
        req->hdr.request_length = payload_size - sizeof(sli4_req_hdr_t);
        req->hdr.version = 1;
        req->profile_id = profile_id;
        req->desc_count = descriptor_count;
        req->isap = isap;

        return(cmd_off + sizeof(sli4_req_common_set_profile_config_t));
}

/**
 * @ingroup sli
 * @brief Write a COMMON_COMMON_GET_PROFILE_LIST command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size in bytes.
 * @param start_profile_index First profile index to return.
 * @param dma Buffer into which the list will be written.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_get_profile_list(sli4_t *sli4, void *buf, size_t size,
                                   uint32_t start_profile_index, ocs_dma_t *dma)
{
        sli4_req_common_get_profile_list_t *req = NULL;
        uint32_t cmd_off = 0;
        uint32_t payload_size;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		cmd_off = sli_cmd_sli_config(sli4, buf, size,
					     sizeof (sli4_req_common_get_profile_list_t),
					     dma);
	}

	if (dma != NULL) {
		req = dma->virt;
		ocs_memset(req, 0, dma->size);
		payload_size = dma->size;
	} else {
		req = (sli4_req_common_get_profile_list_t *)((uint8_t *)buf + cmd_off);
		payload_size = sizeof(sli4_req_common_get_profile_list_t);
	}

        req->hdr.opcode = SLI4_OPC_COMMON_GET_PROFILE_LIST;
        req->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
        req->hdr.request_length = payload_size - sizeof(sli4_req_hdr_t);
        req->hdr.version = 0;

        req->start_profile_index = start_profile_index;

        return(cmd_off + sizeof(sli4_req_common_get_profile_list_t));
}

/**
 * @ingroup sli
 * @brief Write a COMMON_COMMON_GET_ACTIVE_PROFILE command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size in bytes.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_get_active_profile(sli4_t *sli4, void *buf, size_t size)
{
        sli4_req_common_get_active_profile_t *req = NULL;
        uint32_t cmd_off = 0;
        uint32_t payload_size;

        /* Payload length must accommodate both request and response */
        payload_size = max(sizeof(sli4_req_common_get_active_profile_t),
                           sizeof(sli4_res_common_get_active_profile_t));

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		cmd_off = sli_cmd_sli_config(sli4, buf, size,
				payload_size,
				NULL);
	}

        req = (sli4_req_common_get_active_profile_t *)
                ((uint8_t*)buf + cmd_off);

        req->hdr.opcode = SLI4_OPC_COMMON_GET_ACTIVE_PROFILE;
        req->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
        req->hdr.request_length = payload_size - sizeof(sli4_req_hdr_t);
        req->hdr.version = 0;

        return(cmd_off + sizeof(sli4_req_common_get_active_profile_t));
}

/**
 * @ingroup sli
 * @brief Write a COMMON_COMMON_SET_ACTIVE_PROFILE command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size in bytes.
 * @param fd If non-zero, set profile to factory default.
 * @param active_profile_id ID of new active profile.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_set_active_profile(sli4_t *sli4, void *buf, size_t size,
                                  uint32_t fd, uint32_t active_profile_id)
{
        sli4_req_common_set_active_profile_t *req = NULL;
        uint32_t cmd_off = 0;
        uint32_t payload_size;

        /* Payload length must accommodate both request and response */
        payload_size = max(sizeof(sli4_req_common_set_active_profile_t),
                           sizeof(sli4_res_common_set_active_profile_t));

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		cmd_off = sli_cmd_sli_config(sli4, buf, size,
				payload_size,
				NULL);
	}

        req = (sli4_req_common_set_active_profile_t *)
                ((uint8_t*)buf + cmd_off);

        req->hdr.opcode = SLI4_OPC_COMMON_SET_ACTIVE_PROFILE;
        req->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
        req->hdr.request_length = payload_size - sizeof(sli4_req_hdr_t);
        req->hdr.version = 0;
        req->fd = fd;
        req->active_profile_id = active_profile_id;

        return(cmd_off + sizeof(sli4_req_common_set_active_profile_t));
}

/**
 * @ingroup sli
 * @brief Write a COMMON_GET_RECONFIG_LINK_INFO command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size in bytes.
 * @param dma Buffer to store the supported link configuration modes from the physical device.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_get_reconfig_link_info(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *dma)
{
        sli4_req_common_get_reconfig_link_info_t *req = NULL;
        uint32_t cmd_off = 0;
        uint32_t payload_size;

        /* Payload length must accommodate both request and response */
        payload_size = max(sizeof(sli4_req_common_get_reconfig_link_info_t),
                           sizeof(sli4_res_common_get_reconfig_link_info_t));

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		cmd_off = sli_cmd_sli_config(sli4, buf, size,
				payload_size,
				dma);
	}

	if (dma != NULL) {
		req = dma->virt;
		ocs_memset(req, 0, dma->size);
		payload_size = dma->size;
	} else {
		req = (sli4_req_common_get_reconfig_link_info_t *)((uint8_t *)buf + cmd_off);
		payload_size = sizeof(sli4_req_common_get_reconfig_link_info_t);
	}

        req->hdr.opcode = SLI4_OPC_COMMON_GET_RECONFIG_LINK_INFO;
        req->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
        req->hdr.request_length = payload_size - sizeof(sli4_req_hdr_t);
        req->hdr.version = 0;

        return(cmd_off + sizeof(sli4_req_common_get_reconfig_link_info_t));
}

/**
 * @ingroup sli
 * @brief Write a COMMON_SET_RECONFIG_LINK_ID command.
 *
 * @param sli4 SLI context.
 * @param buf destination buffer for the command.
 * @param size buffer size in bytes.
 * @param fd If non-zero, set link config to factory default.
 * @param active_link_config_id ID of new active profile.
 * @param dma Buffer to assign the link configuration mode that is to become active from the physical device.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_common_set_reconfig_link_id(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *dma,
                                  uint32_t fd, uint32_t active_link_config_id)
{
        sli4_req_common_set_reconfig_link_id_t *req = NULL;
        uint32_t cmd_off = 0;
        uint32_t payload_size;

        /* Payload length must accommodate both request and response */
        payload_size = max(sizeof(sli4_req_common_set_reconfig_link_id_t),
                           sizeof(sli4_res_common_set_reconfig_link_id_t));

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		cmd_off = sli_cmd_sli_config(sli4, buf, size,
				payload_size,
				NULL);
	}

		if (dma != NULL) {
		req = dma->virt;
		ocs_memset(req, 0, dma->size);
		payload_size = dma->size;
	} else {
		req = (sli4_req_common_set_reconfig_link_id_t *)((uint8_t *)buf + cmd_off);
		payload_size = sizeof(sli4_req_common_set_reconfig_link_id_t);
	}

        req->hdr.opcode = SLI4_OPC_COMMON_SET_RECONFIG_LINK_ID;
        req->hdr.subsystem = SLI4_SUBSYSTEM_COMMON;
        req->hdr.request_length = payload_size - sizeof(sli4_req_hdr_t);
        req->hdr.version = 0;
        req->fd = fd;
        req->next_link_config_id = active_link_config_id;

        return(cmd_off + sizeof(sli4_req_common_set_reconfig_link_id_t));
}


/**
 * @ingroup sli
 * @brief Check the mailbox/queue completion entry.
 *
 * @param buf Pointer to the MCQE.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_cqe_mq(void *buf)
{
	sli4_mcqe_t	*mcqe = buf;

	/*
	 * Firmware can split mbx completions into two MCQEs: first with only
	 * the "consumed" bit set and a second with the "complete" bit set.
	 * Thus, ignore MCQE unless "complete" is set.
	 */
	if (!mcqe->cmp) {
		return -2;
	}

	if (mcqe->completion_status) {
		ocs_log_debug(NULL, "bad status (cmpl=%#x ext=%#x con=%d cmp=%d ae=%d val=%d)\n",
				mcqe->completion_status,
				mcqe->extended_status,
				mcqe->con,
				mcqe->cmp,
				mcqe->ae,
				mcqe->val);
	}

	return mcqe->completion_status;
}

/**
 * @ingroup sli
 * @brief Check the asynchronous event completion entry.
 *
 * @param sli4 SLI context.
 * @param buf Pointer to the ACQE.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_cqe_async(sli4_t *sli4, void *buf)
{
	sli4_acqe_t	*acqe = buf;
	int32_t		rc = -1;

	if (!sli4 || !buf) {
		ocs_log_err(NULL, "bad parameter sli4=%p buf=%p\n", sli4, buf);
		return -1;
	}

	switch (acqe->event_code) {
	case SLI4_ACQE_EVENT_CODE_LINK_STATE:
		rc = sli_fc_process_link_state(sli4, buf);
		break;
	case SLI4_ACQE_EVENT_CODE_FCOE_FIP:
		rc = sli_fc_process_fcoe(sli4, buf);
		break;
	case SLI4_ACQE_EVENT_CODE_GRP_5:
		/*TODO*/ocs_log_debug(sli4->os, "ACQE GRP5\n");
		break;
	case SLI4_ACQE_EVENT_CODE_SLI_PORT_EVENT:
        ocs_log_debug(sli4->os,"ACQE SLI Port, type=0x%x, data1,2=0x%08x,0x%08x\n",
		acqe->event_type, acqe->event_data[0], acqe->event_data[1]);
#if defined(OCS_INCLUDE_DEBUG)
		ocs_dump32(OCS_DEBUG_ALWAYS, sli4->os, "acq", acqe, sizeof(*acqe));
#endif
		break;
	case SLI4_ACQE_EVENT_CODE_FC_LINK_EVENT:
		rc = sli_fc_process_link_attention(sli4, buf);
		break;
	default:
		/*TODO*/ocs_log_test(sli4->os, "ACQE unknown=%#x\n", acqe->event_code);
	}

	return rc;
}

/**
 * @brief Check the SLI_CONFIG response.
 *
 * @par Description
 * Function checks the SLI_CONFIG response and the payload status.
 *
 * @param buf Pointer to SLI_CONFIG response.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
static int32_t
sli_res_sli_config(void *buf)
{
	sli4_cmd_sli_config_t	*sli_config = buf;

	if (!buf || (SLI4_MBOX_COMMAND_SLI_CONFIG != sli_config->hdr.command)) {
		ocs_log_err(NULL, "bad parameter buf=%p cmd=%#x\n", buf,
				buf ? sli_config->hdr.command : -1);
		return -1;
	}

	if (sli_config->hdr.status) {
		return sli_config->hdr.status;
	}

	if (sli_config->emb) {
		return sli_config->payload.embed[4];
	} else {
		ocs_log_test(NULL, "external buffers not supported\n");
		return -1;
	}
}

/**
 * @brief Issue a COMMON_FUNCTION_RESET command.
 *
 * @param sli4 SLI context.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
static int32_t
sli_common_function_reset(sli4_t *sli4)
{

	if (sli_cmd_common_function_reset(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE)) {
		if (sli_bmbx_command(sli4)) {
			ocs_log_crit(sli4->os, "bootstrap mailbox write fail (COM_FUNC_RESET)\n");
			return -1;
		}
		if (sli_res_sli_config(sli4->bmbx.virt)) {
			ocs_log_err(sli4->os, "bad status COM_FUNC_RESET\n");
			return -1;
		}
	} else {
		ocs_log_err(sli4->os, "bad COM_FUNC_RESET write\n");
		return -1;
	}

	return 0;
}


/**
 * @brief check to see if the FW is ready.
 *
 * @par Description
 * Based on <i>SLI-4 Architecture Specification, Revision 4.x0-13 (2012).</i>.
 *
 * @param sli4 SLI context.
 * @param timeout_ms Time, in milliseconds, to wait for the port to be ready
 *  before failing.
 *
 * @return Returns TRUE for ready, or FALSE otherwise.
 */
static int32_t
sli_wait_for_fw_ready(sli4_t *sli4, uint32_t timeout_ms)
{
	uint32_t	iter = timeout_ms / (SLI4_INIT_PORT_DELAY_US / 1000);
	uint32_t	ready = FALSE;

	do {
		iter--;
		ocs_udelay(SLI4_INIT_PORT_DELAY_US);
		if (sli_fw_ready(sli4) == 1) {
			ready = TRUE;
		}
	} while (!ready && (iter > 0));

	return ready;
}

/**
 * @brief Initialize the firmware.
 *
 * @par Description
 * Based on <i>SLI-4 Architecture Specification, Revision 4.x0-13 (2012).</i>.
 *
 * @param sli4 SLI context.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
static int32_t
sli_fw_init(sli4_t *sli4)
{
	uint32_t ready;
	uint32_t endian;

	/*
	 * Is firmware ready for operation?
	 */
	ready = sli_wait_for_fw_ready(sli4, SLI4_FW_READY_TIMEOUT_MSEC);
	if (!ready) {
		ocs_log_crit(sli4->os, "FW status is NOT ready\n");
		return -1;
	}

	/*
	 * Reset port to a known state
	 */
	switch (sli4->if_type) {
	case SLI4_IF_TYPE_BE3_SKH_PF:
	case SLI4_IF_TYPE_BE3_SKH_VF:
		/* No SLIPORT_CONTROL register so use command sequence instead */
		if (sli_bmbx_wait(sli4, SLI4_BMBX_DELAY_US)) {
			ocs_log_crit(sli4->os, "bootstrap mailbox not ready\n");
			return -1;
		}

		if (sli_cmd_fw_initialize(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE)) {
			if (sli_bmbx_command(sli4)) {
				ocs_log_crit(sli4->os, "bootstrap mailbox write fail (FW_INIT)\n");
				return -1;
			}
		} else {
			ocs_log_crit(sli4->os, "bad FW_INIT write\n");
			return -1;
		}

		if (sli_common_function_reset(sli4)) {
			ocs_log_err(sli4->os, "bad COM_FUNC_RESET write\n");
			return -1;
		}
		break;
	case SLI4_IF_TYPE_LANCER_FC_ETH:
#if BYTE_ORDER == LITTLE_ENDIAN
		endian = SLI4_SLIPORT_CONTROL_LITTLE_ENDIAN;
#else
		endian = SLI4_SLIPORT_CONTROL_BIG_ENDIAN;
#endif

		if (sli_sliport_control(sli4, endian))
			return -1;
		break;
	default:
		ocs_log_test(sli4->os, "if_type %d not supported\n", sli4->if_type);
		return -1;
	}

	return 0;
}

/**
 * @brief Terminate the firmware.
 *
 * @param sli4 SLI context.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
static int32_t
sli_fw_term(sli4_t *sli4)
{
	uint32_t endian;

	if (sli4->if_type == SLI4_IF_TYPE_BE3_SKH_PF ||
	    sli4->if_type == SLI4_IF_TYPE_BE3_SKH_VF) {
		/* No SLIPORT_CONTROL register so use command sequence instead */
		if (sli_bmbx_wait(sli4, SLI4_BMBX_DELAY_US)) {
			ocs_log_crit(sli4->os, "bootstrap mailbox not ready\n");
			return -1;
		}

		if (sli_common_function_reset(sli4)) {
			ocs_log_err(sli4->os, "bad COM_FUNC_RESET write\n");
			return -1;
		}

		if (sli_cmd_fw_deinitialize(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE)) {
			if (sli_bmbx_command(sli4)) {
				ocs_log_crit(sli4->os, "bootstrap mailbox write fail (FW_DEINIT)\n");
				return -1;
			}
		} else {
			ocs_log_test(sli4->os, "bad FW_DEINIT write\n");
			return -1;
		}
	} else {
#if BYTE_ORDER == LITTLE_ENDIAN
		endian = SLI4_SLIPORT_CONTROL_LITTLE_ENDIAN;
#else
		endian = SLI4_SLIPORT_CONTROL_BIG_ENDIAN;
#endif
		/* type 2 etc. use SLIPORT_CONTROL to initialize port */
		sli_sliport_control(sli4, endian);
	}
	return 0;
}

/**
 * @brief Write the doorbell register associated with the queue object.
 *
 * @param sli4 SLI context.
 * @param q Queue object.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
static int32_t
sli_queue_doorbell(sli4_t *sli4, sli4_queue_t *q)
{
	uint32_t	val = 0;

	switch (q->type) {
	case SLI_QTYPE_EQ:
		val = sli_eq_doorbell(q->n_posted, q->id, FALSE);
		ocs_reg_write32(sli4->os, q->doorbell_rset, q->doorbell_offset, val);
		break;
	case SLI_QTYPE_CQ:
		val = sli_cq_doorbell(q->n_posted, q->id, FALSE);
		ocs_reg_write32(sli4->os, q->doorbell_rset, q->doorbell_offset, val);
		break;
	case SLI_QTYPE_MQ:
		val = SLI4_MQ_DOORBELL(q->n_posted, q->id);
		ocs_reg_write32(sli4->os, q->doorbell_rset, q->doorbell_offset, val);
		break;
	case SLI_QTYPE_RQ:
	{
		uint32_t	n_posted = q->n_posted;
		/*
		 * FC/FCoE has different rules for Receive Queues. The host
		 * should only update the doorbell of the RQ-pair containing
		 * the headers since the header / payload RQs are treated
		 * as a matched unit.
		 */
		if (SLI4_PORT_TYPE_FC == sli4->port_type) {
			/*
			 * In RQ-pair, an RQ either contains the FC header
			 * (i.e. is_hdr == TRUE) or the payload.
			 *
			 * Don't ring doorbell for payload RQ
			 */
			if (!q->u.flag.is_hdr) {
				break;
			}
			/*
			 * Some RQ cannot be incremented one entry at a time. Instead,
			 * the driver collects a number of entries and updates the
			 * RQ in batches.
			 */
			if (q->u.flag.rq_batch) {
				if (((q->index + q->n_posted) % SLI4_QUEUE_RQ_BATCH)) {
					break;
				}
				n_posted = SLI4_QUEUE_RQ_BATCH;
			}
		}

		val = SLI4_RQ_DOORBELL(n_posted, q->id);
		ocs_reg_write32(sli4->os, q->doorbell_rset, q->doorbell_offset, val);
		break;
	}
	case SLI_QTYPE_WQ:
		val = SLI4_WQ_DOORBELL(q->n_posted, q->index, q->id);
		ocs_reg_write32(sli4->os, q->doorbell_rset, q->doorbell_offset, val);
		break;
	default:
		ocs_log_test(sli4->os, "bad queue type %d\n", q->type);
		return -1;
	}

	return 0;
}

static int32_t
sli_request_features(sli4_t *sli4, sli4_features_t *features, uint8_t query)
{

	if (sli_cmd_request_features(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE,
				*features, query)) {
		sli4_cmd_request_features_t *req_features = sli4->bmbx.virt;

		if (sli_bmbx_command(sli4)) {
			ocs_log_crit(sli4->os, "bootstrap mailbox write fail (REQUEST_FEATURES)\n");
			return -1;
		}
		if (req_features->hdr.status) {
			ocs_log_err(sli4->os, "REQUEST_FEATURES bad status %#x\n",
					req_features->hdr.status);
			return -1;
		}
		features->dword = req_features->response.dword;
	} else {
		ocs_log_err(sli4->os, "bad REQUEST_FEATURES write\n");
		return -1;
	}

	return 0;
}

/**
 * @brief Calculate max queue entries.
 *
 * @param sli4 SLI context.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
void
sli_calc_max_qentries(sli4_t *sli4)
{
	sli4_qtype_e q;
	uint32_t alloc_size, qentries, qentry_size;

	for (q = SLI_QTYPE_EQ; q < SLI_QTYPE_MAX; q++) {
		sli4->config.max_qentries[q] = sli_convert_mask_to_count(sli4->config.count_method[q],
									 sli4->config.count_mask[q]);
	}

	/* single, continguous DMA allocations will be called for each queue
	 * of size (max_qentries * queue entry size); since these can be large,
	 * check against the OS max DMA allocation size
	 */
	for (q = SLI_QTYPE_EQ; q < SLI_QTYPE_MAX; q++) {
		qentries = sli4->config.max_qentries[q];
		qentry_size = sli_get_queue_entry_size(sli4, q);
		alloc_size = qentries * qentry_size;
		if (alloc_size > ocs_max_dma_alloc(sli4->os, SLI_PAGE_SIZE)) {
			while (alloc_size > ocs_max_dma_alloc(sli4->os, SLI_PAGE_SIZE)) {
				/* cut the qentries in hwf until alloc_size <= max DMA alloc size */
				qentries >>= 1;
				alloc_size = qentries * qentry_size;
			}
			ocs_log_debug(sli4->os, "[%s]: max_qentries from %d to %d (max dma %d)\n",
				SLI_QNAME[q], sli4->config.max_qentries[q],
				qentries, ocs_max_dma_alloc(sli4->os, SLI_PAGE_SIZE));
			sli4->config.max_qentries[q] = qentries;
		}
	}
}

/**
 * @brief Issue a FW_CONFIG mailbox command and store the results.
 *
 * @param sli4 SLI context.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
static int32_t
sli_query_fw_config(sli4_t *sli4)
{
	/*
	 * Read the device configuration
	 *
	 * Note: Only ulp0 fields contain values
	 */
	if (sli_cmd_common_query_fw_config(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE)) {
		sli4_res_common_query_fw_config_t   *fw_config =
			(sli4_res_common_query_fw_config_t *)
			(((uint8_t *)sli4->bmbx.virt) + offsetof(sli4_cmd_sli_config_t, payload.embed));

		if (sli_bmbx_command(sli4)) {
			ocs_log_crit(sli4->os, "bootstrap mailbox write fail (QUERY_FW_CONFIG)\n");
			return -1;
		}
		if (fw_config->hdr.status) {
			ocs_log_err(sli4->os, "COMMON_QUERY_FW_CONFIG bad status %#x\n",
				fw_config->hdr.status);
			return -1;
		}

		sli4->physical_port = fw_config->physical_port;
		sli4->config.dual_ulp_capable = ((fw_config->function_mode & SLI4_FUNCTION_MODE_DUA_MODE) == 0 ? 0 : 1);
		sli4->config.is_ulp_fc[0] = ((fw_config->ulp0_mode &
					      (SLI4_ULP_MODE_FCOE_INI |
					       SLI4_ULP_MODE_FCOE_TGT)) == 0 ? 0 : 1);
		sli4->config.is_ulp_fc[1] = ((fw_config->ulp1_mode &
					      (SLI4_ULP_MODE_FCOE_INI |
					       SLI4_ULP_MODE_FCOE_TGT)) == 0 ? 0 : 1);

		if (sli4->config.dual_ulp_capable) {
			/*
			 * Lancer will not support this, so we use the values
			 * from the READ_CONFIG.
			 */
			if (sli4->config.is_ulp_fc[0] &&
			    sli4->config.is_ulp_fc[1]) {
				sli4->config.max_qcount[SLI_QTYPE_WQ] = fw_config->ulp0_toe_wq_total + fw_config->ulp1_toe_wq_total;
				sli4->config.max_qcount[SLI_QTYPE_RQ] = fw_config->ulp0_toe_defrq_total + fw_config->ulp1_toe_defrq_total;
			} else if (sli4->config.is_ulp_fc[0]) {
				sli4->config.max_qcount[SLI_QTYPE_WQ] = fw_config->ulp0_toe_wq_total;
				sli4->config.max_qcount[SLI_QTYPE_RQ] = fw_config->ulp0_toe_defrq_total;
			} else {
				sli4->config.max_qcount[SLI_QTYPE_WQ] = fw_config->ulp1_toe_wq_total;
				sli4->config.max_qcount[SLI_QTYPE_RQ] = fw_config->ulp1_toe_defrq_total;
			}
		}
	} else {
		ocs_log_err(sli4->os, "bad QUERY_FW_CONFIG write\n");
		return -1;
	}
	return 0;
}


static int32_t
sli_get_config(sli4_t *sli4)
{
	ocs_dma_t	get_cntl_addl_data;

	/*
	 * Read the device configuration
	 */
	if (sli_cmd_read_config(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE)) {
		sli4_res_read_config_t	*read_config = sli4->bmbx.virt;
		uint32_t	i;
		uint32_t	total;

		if (sli_bmbx_command(sli4)) {
			ocs_log_crit(sli4->os, "bootstrap mailbox write fail (READ_CONFIG)\n");
			return -1;
		}
		if (read_config->hdr.status) {
			ocs_log_err(sli4->os, "READ_CONFIG bad status %#x\n",
					read_config->hdr.status);
			return -1;
		}

		sli4->config.has_extents = read_config->ext;
		if (FALSE == sli4->config.has_extents) {
			uint32_t	i = 0;
			uint32_t	*base = sli4->config.extent[0].base;

			if (!base) {
				if (NULL == (base = ocs_malloc(sli4->os, SLI_RSRC_MAX * sizeof(uint32_t),
								OCS_M_ZERO | OCS_M_NOWAIT))) {
					ocs_log_err(sli4->os, "memory allocation failed for sli4_resource_t\n");
					return -1;
				}
			}

			for (i = 0; i < SLI_RSRC_MAX; i++) {
				sli4->config.extent[i].number = 1;
				sli4->config.extent[i].n_alloc = 0;
				sli4->config.extent[i].base = &base[i];
			}

			sli4->config.extent[SLI_RSRC_FCOE_VFI].base[0] = read_config->vfi_base;
			sli4->config.extent[SLI_RSRC_FCOE_VFI].size = read_config->vfi_count;

			sli4->config.extent[SLI_RSRC_FCOE_VPI].base[0] = read_config->vpi_base;
			sli4->config.extent[SLI_RSRC_FCOE_VPI].size = read_config->vpi_count;

			sli4->config.extent[SLI_RSRC_FCOE_RPI].base[0] = read_config->rpi_base;
			sli4->config.extent[SLI_RSRC_FCOE_RPI].size = read_config->rpi_count;

			sli4->config.extent[SLI_RSRC_FCOE_XRI].base[0] = read_config->xri_base;
			sli4->config.extent[SLI_RSRC_FCOE_XRI].size = read_config->xri_count;

			sli4->config.extent[SLI_RSRC_FCOE_FCFI].base[0] = 0;
			sli4->config.extent[SLI_RSRC_FCOE_FCFI].size = read_config->fcfi_count;
		} else {
			/* TODO extents*/
			;
		}

		for (i = 0; i < SLI_RSRC_MAX; i++) {
			total = sli4->config.extent[i].number * sli4->config.extent[i].size;
			sli4->config.extent[i].use_map = ocs_bitmap_alloc(total);
			if (NULL == sli4->config.extent[i].use_map) {
				ocs_log_err(sli4->os, "bitmap memory allocation failed "
						"resource %d\n", i);
				return -1;
			}
			sli4->config.extent[i].map_size = total;
		}

		sli4->config.topology = read_config->topology;
		switch (sli4->config.topology) {
		case SLI4_READ_CFG_TOPO_FCOE:
			ocs_log_debug(sli4->os, "FCoE\n");
			break;
		case SLI4_READ_CFG_TOPO_FC:
			ocs_log_debug(sli4->os, "FC (unknown)\n");
			break;
		case SLI4_READ_CFG_TOPO_FC_DA:
			ocs_log_debug(sli4->os, "FC (direct attach)\n");
			break;
		case SLI4_READ_CFG_TOPO_FC_AL:
			ocs_log_debug(sli4->os, "FC (arbitrated loop)\n");
			break;
		default:
			ocs_log_test(sli4->os, "bad topology %#x\n", sli4->config.topology);
		}

		sli4->config.e_d_tov = read_config->e_d_tov;
		sli4->config.r_a_tov = read_config->r_a_tov;

		sli4->config.link_module_type = read_config->lmt;

		sli4->config.max_qcount[SLI_QTYPE_EQ] = read_config->eq_count;
		sli4->config.max_qcount[SLI_QTYPE_CQ] = read_config->cq_count;
		sli4->config.max_qcount[SLI_QTYPE_WQ] = read_config->wq_count;
		sli4->config.max_qcount[SLI_QTYPE_RQ] = read_config->rq_count;

		/*
		 * READ_CONFIG doesn't give the max number of MQ. Applications
		 * will typically want 1, but we may need another at some future
		 * date. Dummy up a "max" MQ count here.
		 */
		sli4->config.max_qcount[SLI_QTYPE_MQ] = SLI_USER_MQ_COUNT;
	} else {
		ocs_log_err(sli4->os, "bad READ_CONFIG write\n");
		return -1;
	}

	if (sli_cmd_common_get_sli4_parameters(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE)) {
		sli4_res_common_get_sli4_parameters_t	*parms = (sli4_res_common_get_sli4_parameters_t *)
			(((uint8_t *)sli4->bmbx.virt) + offsetof(sli4_cmd_sli_config_t, payload.embed));

		if (sli_bmbx_command(sli4)) {
			ocs_log_crit(sli4->os, "bootstrap mailbox write fail (COMMON_GET_SLI4_PARAMETERS)\n");
			return -1;
		} else if (parms->hdr.status) {
			ocs_log_err(sli4->os, "COMMON_GET_SLI4_PARAMETERS bad status %#x att'l %#x\n",
					parms->hdr.status, parms->hdr.additional_status);
			return -1;
		}

		sli4->config.auto_reg = parms->areg;
		sli4->config.auto_xfer_rdy = parms->agxf;
		sli4->config.hdr_template_req = parms->hdrr;
		sli4->config.t10_dif_inline_capable = parms->timm;
		sli4->config.t10_dif_separate_capable = parms->tsmm;

		sli4->config.mq_create_version = parms->mqv;
		sli4->config.cq_create_version = parms->cqv;
		sli4->config.rq_min_buf_size = parms->min_rq_buffer_size;
		sli4->config.rq_max_buf_size = parms->max_rq_buffer_size;

		sli4->config.qpage_count[SLI_QTYPE_EQ] = parms->eq_page_cnt;
		sli4->config.qpage_count[SLI_QTYPE_CQ] = parms->cq_page_cnt;
		sli4->config.qpage_count[SLI_QTYPE_MQ] = parms->mq_page_cnt;
		sli4->config.qpage_count[SLI_QTYPE_WQ] = parms->wq_page_cnt;
		sli4->config.qpage_count[SLI_QTYPE_RQ] = parms->rq_page_cnt;

		/* save count methods and masks for each queue type */
		sli4->config.count_mask[SLI_QTYPE_EQ] = parms->eqe_count_mask;
		sli4->config.count_method[SLI_QTYPE_EQ] = parms->eqe_count_method;
		sli4->config.count_mask[SLI_QTYPE_CQ] = parms->cqe_count_mask;
		sli4->config.count_method[SLI_QTYPE_CQ] = parms->cqe_count_method;
		sli4->config.count_mask[SLI_QTYPE_MQ] = parms->mqe_count_mask;
		sli4->config.count_method[SLI_QTYPE_MQ] = parms->mqe_count_method;
		sli4->config.count_mask[SLI_QTYPE_WQ] = parms->wqe_count_mask;
		sli4->config.count_method[SLI_QTYPE_WQ] = parms->wqe_count_method;
		sli4->config.count_mask[SLI_QTYPE_RQ] = parms->rqe_count_mask;
		sli4->config.count_method[SLI_QTYPE_RQ] = parms->rqe_count_method;

		/* now calculate max queue entries */
		sli_calc_max_qentries(sli4);

		sli4->config.max_sgl_pages = parms->sgl_page_cnt;	/* max # of pages */
		sli4->config.sgl_page_sizes = parms->sgl_page_sizes;	/* bit map of available sizes */
		/* ignore HLM here. Use value from REQUEST_FEATURES */
		
		sli4->config.sge_supported_length = parms->sge_supported_length;
		if (sli4->config.sge_supported_length > OCS_MAX_SGE_SIZE)
			sli4->config.sge_supported_length = OCS_MAX_SGE_SIZE;

		sli4->config.sgl_pre_registration_required = parms->sglr;
		/* default to using pre-registered SGL's */
		sli4->config.sgl_pre_registered = TRUE;

		sli4->config.perf_hint = parms->phon;
		sli4->config.perf_wq_id_association = parms->phwq;

		sli4->config.rq_batch = parms->rq_db_window;

		/* save the fields for skyhawk SGL chaining */
		sli4->config.sgl_chaining_params.chaining_capable =
			(parms->sglc == 1);
		sli4->config.sgl_chaining_params.frag_num_field_offset =
			parms->frag_num_field_offset;
		sli4->config.sgl_chaining_params.frag_num_field_mask =
			(1ull << parms->frag_num_field_size) - 1;
		sli4->config.sgl_chaining_params.sgl_index_field_offset =
			parms->sgl_index_field_offset;
		sli4->config.sgl_chaining_params.sgl_index_field_mask =
			(1ull << parms->sgl_index_field_size) - 1;
		sli4->config.sgl_chaining_params.chain_sge_initial_value_lo =
			parms->chain_sge_initial_value_lo;
		sli4->config.sgl_chaining_params.chain_sge_initial_value_hi =
			parms->chain_sge_initial_value_hi;

		/* Use the highest available WQE size. */
		if (parms->wqe_sizes & SLI4_128BYTE_WQE_SUPPORT) {
			sli4->config.wqe_size = SLI4_WQE_EXT_BYTES;
		} else {
			sli4->config.wqe_size = SLI4_WQE_BYTES;
		}
	}

	if (sli_query_fw_config(sli4)) {
		ocs_log_err(sli4->os, "Error sending QUERY_FW_CONFIG\n");
		return -1;
	}

	sli4->config.port_number = 0;

	/*
	 * Issue COMMON_GET_CNTL_ATTRIBUTES to get port_number. Temporarily
	 * uses VPD DMA buffer as the response won't fit in the embedded
	 * buffer.
	 */
	if (sli_cmd_common_get_cntl_attributes(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE, &sli4->vpd.data)) {
		sli4_res_common_get_cntl_attributes_t *attr = sli4->vpd.data.virt;

		if (sli_bmbx_command(sli4)) {
			ocs_log_crit(sli4->os, "bootstrap mailbox write fail (COMMON_GET_CNTL_ATTRIBUTES)\n");
			return -1;
		} else if (attr->hdr.status) {
			ocs_log_err(sli4->os, "COMMON_GET_CNTL_ATTRIBUTES bad status %#x att'l %#x\n",
					attr->hdr.status, attr->hdr.additional_status);
			return -1;
		}

		sli4->config.port_number = attr->port_number;

		ocs_memcpy(sli4->config.bios_version_string, attr->bios_version_string,
				sizeof(sli4->config.bios_version_string));
	} else {
		ocs_log_err(sli4->os, "bad COMMON_GET_CNTL_ATTRIBUTES write\n");
		return -1;
	}

	if (ocs_dma_alloc(sli4->os, &get_cntl_addl_data, sizeof(sli4_res_common_get_cntl_addl_attributes_t),
			  OCS_MIN_DMA_ALIGNMENT)) {
		ocs_log_err(sli4->os, "Failed to allocate memory for GET_CNTL_ADDL_ATTR data\n");
	} else {
		if (sli_cmd_common_get_cntl_addl_attributes(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE,
							    &get_cntl_addl_data)) {
			sli4_res_common_get_cntl_addl_attributes_t *attr = get_cntl_addl_data.virt;

			if (sli_bmbx_command(sli4)) {
				ocs_log_crit(sli4->os,
					     "bootstrap mailbox write fail (COMMON_GET_CNTL_ADDL_ATTRIBUTES)\n");
				ocs_dma_free(sli4->os, &get_cntl_addl_data);
				return -1;
			}
			if (attr->hdr.status) {
				ocs_log_err(sli4->os, "COMMON_GET_CNTL_ADDL_ATTRIBUTES bad status %#x\n",
					    attr->hdr.status);
				ocs_dma_free(sli4->os, &get_cntl_addl_data);
				return -1;
			}

			ocs_memcpy(sli4->config.ipl_name, attr->ipl_file_name, sizeof(sli4->config.ipl_name));

			ocs_log_debug(sli4->os, "IPL:%s \n", (char*)sli4->config.ipl_name);
		} else {
			ocs_log_err(sli4->os, "bad COMMON_GET_CNTL_ADDL_ATTRIBUTES write\n");
			ocs_dma_free(sli4->os, &get_cntl_addl_data);
			return -1;
		}

		ocs_dma_free(sli4->os, &get_cntl_addl_data);
	}

	if (sli_cmd_common_get_port_name(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE)) {
		sli4_res_common_get_port_name_t	*port_name = (sli4_res_common_get_port_name_t *)(((uint8_t *)sli4->bmbx.virt) +
			offsetof(sli4_cmd_sli_config_t, payload.embed));

		if (sli_bmbx_command(sli4)) {
			ocs_log_crit(sli4->os, "bootstrap mailbox write fail (COMMON_GET_PORT_NAME)\n");
			return -1;
		}

		sli4->config.port_name[0] = port_name->port_name[sli4->config.port_number];
	}
	sli4->config.port_name[1] = '\0';

	if (sli_cmd_read_rev(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE, &sli4->vpd.data)) {
		sli4_cmd_read_rev_t	*read_rev = sli4->bmbx.virt;

		if (sli_bmbx_command(sli4)) {
			ocs_log_crit(sli4->os, "bootstrap mailbox write fail (READ_REV)\n");
			return -1;
		}
		if (read_rev->hdr.status) {
			ocs_log_err(sli4->os, "READ_REV bad status %#x\n",
					read_rev->hdr.status);
			return -1;
		}

		sli4->config.fw_rev[0] = read_rev->first_fw_id;
		ocs_memcpy(sli4->config.fw_name[0],read_rev->first_fw_name, sizeof(sli4->config.fw_name[0]));

		sli4->config.fw_rev[1] = read_rev->second_fw_id;
		ocs_memcpy(sli4->config.fw_name[1],read_rev->second_fw_name, sizeof(sli4->config.fw_name[1]));

		sli4->config.hw_rev[0] = read_rev->first_hw_revision;
		sli4->config.hw_rev[1] = read_rev->second_hw_revision;
		sli4->config.hw_rev[2] = read_rev->third_hw_revision;

		ocs_log_debug(sli4->os, "FW1:%s (%08x) / FW2:%s (%08x)\n",
				read_rev->first_fw_name, read_rev->first_fw_id,
				read_rev->second_fw_name, read_rev->second_fw_id);

		ocs_log_debug(sli4->os, "HW1: %08x / HW2: %08x\n", read_rev->first_hw_revision,
				read_rev->second_hw_revision);

		/* Check that all VPD data was returned */
		if (read_rev->returned_vpd_length != read_rev->actual_vpd_length) {
			ocs_log_test(sli4->os, "VPD length: available=%d returned=%d actual=%d\n",
					read_rev->available_length,
					read_rev->returned_vpd_length,
					read_rev->actual_vpd_length);
		}
		sli4->vpd.length = read_rev->returned_vpd_length;
	} else {
		ocs_log_err(sli4->os, "bad READ_REV write\n");
		return -1;
	}

	if (sli_cmd_read_nvparms(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE)) {
		sli4_cmd_read_nvparms_t	*read_nvparms = sli4->bmbx.virt;

		if (sli_bmbx_command(sli4)) {
			ocs_log_crit(sli4->os, "bootstrap mailbox write fail (READ_NVPARMS)\n");
			return -1;
		}
		if (read_nvparms->hdr.status) {
			ocs_log_err(sli4->os, "READ_NVPARMS bad status %#x\n",
					read_nvparms->hdr.status);
			return -1;
		}

		ocs_memcpy(sli4->config.wwpn, read_nvparms->wwpn, sizeof(sli4->config.wwpn));
		ocs_memcpy(sli4->config.wwnn, read_nvparms->wwnn, sizeof(sli4->config.wwnn));

		ocs_log_debug(sli4->os, "WWPN %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
				sli4->config.wwpn[0],
				sli4->config.wwpn[1],
				sli4->config.wwpn[2],
				sli4->config.wwpn[3],
				sli4->config.wwpn[4],
				sli4->config.wwpn[5],
				sli4->config.wwpn[6],
				sli4->config.wwpn[7]);
		ocs_log_debug(sli4->os, "WWNN %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
				sli4->config.wwnn[0],
				sli4->config.wwnn[1],
				sli4->config.wwnn[2],
				sli4->config.wwnn[3],
				sli4->config.wwnn[4],
				sli4->config.wwnn[5],
				sli4->config.wwnn[6],
				sli4->config.wwnn[7]);
	} else {
		ocs_log_err(sli4->os, "bad READ_NVPARMS write\n");
		return -1;
	}

	return 0;
}

/****************************************************************************
 * Public functions
 */

/**
 * @ingroup sli
 * @brief Set up the SLI context.
 *
 * @param sli4 SLI context.
 * @param os Device abstraction.
 * @param port_type Protocol type of port (for example, FC and NIC).
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_setup(sli4_t *sli4, ocs_os_handle_t os, sli4_port_type_e port_type)
{
	uint32_t sli_intf = UINT32_MAX;
	uint32_t pci_class_rev = 0;
	uint32_t rev_id = 0;
	uint32_t family = 0;
	uint32_t i;
	sli4_asic_entry_t *asic;

	ocs_memset(sli4, 0, sizeof(sli4_t));

	sli4->os = os;
	sli4->port_type = port_type;

	/*
	 * Read the SLI_INTF register to discover the register layout
	 * and other capability information
	 */
	sli_intf = ocs_config_read32(os, SLI4_INTF_REG);

	if (sli_intf_valid_check(sli_intf)) {
		ocs_log_err(os, "SLI_INTF is not valid\n");
		return -1;
	}

	/* driver only support SLI-4 */
	sli4->sli_rev = sli_intf_sli_revision(sli_intf);
	if (4 != sli4->sli_rev) {
		ocs_log_err(os, "Unsupported SLI revision (intf=%#x)\n",
				sli_intf);
		return -1;
	}

	sli4->sli_family = sli_intf_sli_family(sli_intf);

	sli4->if_type = sli_intf_if_type(sli_intf);

	if (SLI4_IF_TYPE_LANCER_FC_ETH == sli4->if_type) {
		ocs_log_debug(os, "status=%#x error1=%#x error2=%#x\n",
				sli_reg_read(sli4, SLI4_REG_SLIPORT_STATUS),
				sli_reg_read(sli4, SLI4_REG_SLIPORT_ERROR1),
				sli_reg_read(sli4, SLI4_REG_SLIPORT_ERROR2));
	}

	/*
	 * set the ASIC type and revision
	 */
	pci_class_rev = ocs_config_read32(os, SLI4_PCI_CLASS_REVISION);
	rev_id = sli_pci_rev_id(pci_class_rev);
	family = sli4->sli_family;
	if (family == SLI4_FAMILY_CHECK_ASIC_TYPE) {
		uint32_t asic_id = ocs_config_read32(os, SLI4_ASIC_ID_REG);
		family = sli_asic_gen(asic_id);
	}

	for (i = 0, asic = sli4_asic_table; i < ARRAY_SIZE(sli4_asic_table); i++, asic++) {
		if ((rev_id == asic->rev_id) && (family == asic->family)) {
			sli4->asic_type = asic->type;
			sli4->asic_rev = asic->rev;
			break;
		}
	}
	/* Fail if no matching asic type/rev was found */
	if( (sli4->asic_type == 0) || (sli4->asic_rev == 0)) {
		ocs_log_err(os, "no matching asic family/rev found: %02x/%02x\n", family, rev_id);
		return -1;
	}

	/*
	 * The bootstrap mailbox is equivalent to a MQ with a single 256 byte
	 * entry, a CQ with a single 16 byte entry, and no event queue.
	 * Alignment must be 16 bytes as the low order address bits in the
	 * address register are also control / status.
	 */
	if (ocs_dma_alloc(sli4->os, &sli4->bmbx, SLI4_BMBX_SIZE +
				sizeof(sli4_mcqe_t), 16)) {
		ocs_log_err(os, "bootstrap mailbox allocation failed\n");
		return -1;
	}

	if (sli4->bmbx.phys & SLI4_BMBX_MASK_LO) {
		ocs_log_err(os, "bad alignment for bootstrap mailbox\n");
		return -1;
	}

	ocs_log_debug(os, "bmbx v=%p p=0x%x %08x s=%zd\n", sli4->bmbx.virt,
		ocs_addr32_hi(sli4->bmbx.phys),
		ocs_addr32_lo(sli4->bmbx.phys),
		sli4->bmbx.size);

	/* TODO 4096 is arbitrary. What should this value actually be? */
	if (ocs_dma_alloc(sli4->os, &sli4->vpd.data, 4096/*TODO*/, 4096)) {
		/* Note that failure isn't fatal in this specific case */
		sli4->vpd.data.size = 0;
		ocs_log_test(os, "VPD buffer allocation failed\n");
	}

	if (sli_fw_init(sli4)) {
		ocs_log_err(sli4->os, "FW initialization failed\n");
		return -1;
	}

	/*
	 * Set one of fcpi(initiator), fcpt(target), fcpc(combined) to true
	 * in addition to any other desired features
	 */
	sli4->config.features.flag.iaab = TRUE;
	sli4->config.features.flag.npiv = TRUE;
	sli4->config.features.flag.dif = TRUE;
	sli4->config.features.flag.vf = TRUE;
	sli4->config.features.flag.fcpc = TRUE;
	sli4->config.features.flag.iaar = TRUE;
	sli4->config.features.flag.hlm = TRUE;
	sli4->config.features.flag.perfh = TRUE;
	sli4->config.features.flag.rxseq = TRUE;
	sli4->config.features.flag.rxri = TRUE;
	sli4->config.features.flag.mrqp = TRUE;

	/* use performance hints if available */
	if (sli4->config.perf_hint) {
		sli4->config.features.flag.perfh = TRUE;
	}

	if (sli_request_features(sli4, &sli4->config.features, TRUE)) {
		return -1;
	}

	if (sli_get_config(sli4)) {
		return -1;
	}

	return 0;
}

int32_t
sli_init(sli4_t *sli4)
{

	if (sli4->config.has_extents) {
		/* TODO COMMON_ALLOC_RESOURCE_EXTENTS */;
		ocs_log_test(sli4->os, "XXX need to implement extent allocation\n");
		return -1;
	}

	sli4->config.features.flag.hlm = sli4->config.high_login_mode;
	sli4->config.features.flag.rxseq = FALSE;
	sli4->config.features.flag.rxri  = FALSE;

	if (sli_request_features(sli4, &sli4->config.features, FALSE)) {
		return -1;
	}

	return 0;
}

int32_t
sli_reset(sli4_t *sli4)
{
	uint32_t	i;

	if (sli_fw_init(sli4)) {
		ocs_log_crit(sli4->os, "FW initialization failed\n");
		return -1;
	}

	if (sli4->config.extent[0].base) {
		ocs_free(sli4->os, sli4->config.extent[0].base, SLI_RSRC_MAX * sizeof(uint32_t));
		sli4->config.extent[0].base = NULL;
	}

	for (i = 0; i < SLI_RSRC_MAX; i++) {
		if (sli4->config.extent[i].use_map) {
			ocs_bitmap_free(sli4->config.extent[i].use_map);
			sli4->config.extent[i].use_map = NULL;
		}
		sli4->config.extent[i].base = NULL;
	}

	if (sli_get_config(sli4)) {
		return -1;
	}

	return 0;
}

/**
 * @ingroup sli
 * @brief Issue a Firmware Reset.
 *
 * @par Description
 * Issues a Firmware Reset to the chip.  This reset affects the entire chip,
 * so all PCI function on the same PCI bus and device are affected.
 * @n @n This type of reset can be used to activate newly downloaded firmware.
 * @n @n The driver should be considered to be in an unknown state after this
 * reset and should be reloaded.
 *
 * @param sli4 SLI context.
 *
 * @return Returns 0 on success, or -1 otherwise.
 */

int32_t
sli_fw_reset(sli4_t *sli4)
{
	uint32_t val;
	uint32_t ready;

	/*
	 * Firmware must be ready before issuing the reset.
	 */
	ready = sli_wait_for_fw_ready(sli4, SLI4_FW_READY_TIMEOUT_MSEC);
	if (!ready) {
		ocs_log_crit(sli4->os, "FW status is NOT ready\n");
		return -1;
	}
	switch(sli4->if_type) {
	case SLI4_IF_TYPE_BE3_SKH_PF:
		/* BE3 / Skyhawk use PCICFG_SOFT_RESET_CSR */
		val = ocs_config_read32(sli4->os, SLI4_PCI_SOFT_RESET_CSR);
		val |= SLI4_PCI_SOFT_RESET_MASK;
		ocs_config_write32(sli4->os, SLI4_PCI_SOFT_RESET_CSR, val);
		break;
	case SLI4_IF_TYPE_LANCER_FC_ETH:
		/* Lancer uses PHYDEV_CONTROL */

		val = SLI4_PHYDEV_CONTROL_FRST;
		sli_reg_write(sli4, SLI4_REG_PHYSDEV_CONTROL, val);
		break;
	default:
		ocs_log_test(sli4->os, "Unexpected iftype %d\n", sli4->if_type);
		return -1;
		break;
	}

	/* wait for the FW to become ready after the reset */
	ready = sli_wait_for_fw_ready(sli4, SLI4_FW_READY_TIMEOUT_MSEC);
	if (!ready) {
		ocs_log_crit(sli4->os, "Failed to become ready after firmware reset\n");
		return -1;
	}
	return 0;
}

/**
 * @ingroup sli
 * @brief Tear down a SLI context.
 *
 * @param sli4 SLI context.
 *
 * @return Returns 0 on success, or non-zero otherwise.
 */
int32_t
sli_teardown(sli4_t *sli4)
{
	uint32_t i;

	if (sli4->config.extent[0].base) {
		ocs_free(sli4->os, sli4->config.extent[0].base, SLI_RSRC_MAX * sizeof(uint32_t));
		sli4->config.extent[0].base = NULL;
	}

	for (i = 0; i < SLI_RSRC_MAX; i++) {
		if (sli4->config.has_extents) {
			/* TODO COMMON_DEALLOC_RESOURCE_EXTENTS */;
		}

		sli4->config.extent[i].base = NULL;

		ocs_bitmap_free(sli4->config.extent[i].use_map);
		sli4->config.extent[i].use_map = NULL;
	}

	if (sli_fw_term(sli4)) {
		ocs_log_err(sli4->os, "FW deinitialization failed\n");
	}

	ocs_dma_free(sli4->os, &sli4->vpd.data);
	ocs_dma_free(sli4->os, &sli4->bmbx);

	return 0;
}

/**
 * @ingroup sli
 * @brief Register a callback for the given event.
 *
 * @param sli4 SLI context.
 * @param which Event of interest.
 * @param func Function to call when the event occurs.
 * @param arg Argument passed to the callback function.
 *
 * @return Returns 0 on success, or non-zero otherwise.
 */
int32_t
sli_callback(sli4_t *sli4, sli4_callback_e which, void *func, void *arg)
{

	if (!sli4 || !func || (which >= SLI4_CB_MAX)) {
		ocs_log_err(NULL, "bad parameter sli4=%p which=%#x func=%p\n",
			    sli4, which, func);
		return -1;
	}

	switch (which) {
	case SLI4_CB_LINK:
		sli4->link = func;
		sli4->link_arg = arg;
		break;
	case SLI4_CB_FIP:
		sli4->fip = func;
		sli4->fip_arg = arg;
		break;
	default:
		ocs_log_test(sli4->os, "unknown callback %#x\n", which);
		return -1;
	}

	return 0;
}

/**
 * @ingroup sli
 * @brief Initialize a queue object.
 *
 * @par Description
 * This initializes the sli4_queue_t object members, including the underlying
 * DMA memory.
 *
 * @param sli4 SLI context.
 * @param q Pointer to queue object.
 * @param qtype Type of queue to create.
 * @param size Size of each entry.
 * @param n_entries Number of entries to allocate.
 * @param align Starting memory address alignment.
 *
 * @note Checks if using the existing DMA memory (if any) is possible. If not,
 * it frees the existing memory and re-allocates.
 *
 * @return Returns 0 on success, or non-zero otherwise.
 */
int32_t
__sli_queue_init(sli4_t *sli4, sli4_queue_t *q, uint32_t qtype,
		size_t size, uint32_t n_entries, uint32_t align)
{

	if ((q->dma.virt == NULL) || (size != q->size) || (n_entries != q->length)) {
		if (q->dma.size) {
			ocs_dma_free(sli4->os, &q->dma);
		}

		ocs_memset(q, 0, sizeof(sli4_queue_t));

		if (ocs_dma_alloc(sli4->os, &q->dma, size * n_entries, align)) {
			ocs_log_err(sli4->os, "%s allocation failed\n", SLI_QNAME[qtype]);
			return -1;
		}

		ocs_memset(q->dma.virt, 0, size * n_entries);

		ocs_lock_init(sli4->os, &q->lock, "%s lock[%d:%p]", 
			SLI_QNAME[qtype], ocs_instance(sli4->os), &q->lock);

		q->type = qtype;
		q->size = size;
		q->length = n_entries;

		/* Limit to hwf the queue size per interrupt */
		q->proc_limit = n_entries / 2;

		switch(q->type) {
		case SLI_QTYPE_EQ:
			q->posted_limit = q->length / 2;
			break;
		default:
			if ((sli4->if_type == SLI4_IF_TYPE_BE3_SKH_PF) ||
			    (sli4->if_type == SLI4_IF_TYPE_BE3_SKH_VF)) {
				/* For Skyhawk, ring the doorbell more often */
				q->posted_limit = 8;
			} else {
				q->posted_limit = 64;
			}
			break;
		}
	}

	return 0;
}

/**
 * @ingroup sli
 * @brief Issue the command to create a queue.
 *
 * @param sli4 SLI context.
 * @param q Pointer to queue object.
 *
 * @return Returns 0 on success, or non-zero otherwise.
 */
int32_t
__sli_create_queue(sli4_t *sli4, sli4_queue_t *q)
{
	sli4_res_common_create_queue_t *res_q = NULL;

	if (sli_bmbx_command(sli4)){
		ocs_log_crit(sli4->os, "bootstrap mailbox write fail %s\n",
				SLI_QNAME[q->type]);
		ocs_dma_free(sli4->os, &q->dma);
		return -1;
	}
	if (sli_res_sli_config(sli4->bmbx.virt)) {
		ocs_log_err(sli4->os, "bad status create %s\n", SLI_QNAME[q->type]);
		ocs_dma_free(sli4->os, &q->dma);
		return -1;
	}
	res_q = (void *)((uint8_t *)sli4->bmbx.virt +
			offsetof(sli4_cmd_sli_config_t, payload));

	if (res_q->hdr.status) {
		ocs_log_err(sli4->os, "bad create %s status=%#x addl=%#x\n",
				SLI_QNAME[q->type],
				res_q->hdr.status, res_q->hdr.additional_status);
		ocs_dma_free(sli4->os, &q->dma);
		return -1;
	} else {
		q->id = res_q->q_id;
		q->doorbell_offset = res_q->db_offset;
		q->doorbell_rset = res_q->db_rs;

		switch (q->type) {
		case SLI_QTYPE_EQ:
			/* No doorbell information in response for EQs */
			q->doorbell_offset = regmap[SLI4_REG_EQCQ_DOORBELL][sli4->if_type].off;
			q->doorbell_rset = regmap[SLI4_REG_EQCQ_DOORBELL][sli4->if_type].rset;
			break;
		case SLI_QTYPE_CQ:
			/* No doorbell information in response for CQs */
			q->doorbell_offset = regmap[SLI4_REG_EQCQ_DOORBELL][sli4->if_type].off;
			q->doorbell_rset = regmap[SLI4_REG_EQCQ_DOORBELL][sli4->if_type].rset;
			break;
		case SLI_QTYPE_MQ:
			/* No doorbell information in response for MQs */
			q->doorbell_offset = regmap[SLI4_REG_MQ_DOORBELL][sli4->if_type].off;
			q->doorbell_rset = regmap[SLI4_REG_MQ_DOORBELL][sli4->if_type].rset;
			break;
		case SLI_QTYPE_RQ:
			/* set the doorbell for non-skyhawks */
			if (!sli4->config.dual_ulp_capable) {
				q->doorbell_offset = regmap[SLI4_REG_FCOE_RQ_DOORBELL][sli4->if_type].off;
				q->doorbell_rset = regmap[SLI4_REG_FCOE_RQ_DOORBELL][sli4->if_type].rset;
			}
			break;
		case SLI_QTYPE_WQ:
			/* set the doorbell for non-skyhawks */
			if (!sli4->config.dual_ulp_capable) {
				q->doorbell_offset = regmap[SLI4_REG_IO_WQ_DOORBELL][sli4->if_type].off;
				q->doorbell_rset = regmap[SLI4_REG_IO_WQ_DOORBELL][sli4->if_type].rset;
			}
			break;
		default:
			break;
		}
	}

	return 0;
}

/**
 * @ingroup sli
 * @brief Get queue entry size.
 *
 * Get queue entry size given queue type.
 *
 * @param sli4 SLI context
 * @param qtype Type for which the entry size is returned.
 *
 * @return Returns > 0 on success (queue entry size), or a negative value on failure.
 */
int32_t
sli_get_queue_entry_size(sli4_t *sli4, uint32_t qtype)
{
	uint32_t	size = 0;

	if (!sli4) {
		ocs_log_err(NULL, "bad parameter sli4=%p\n", sli4);
		return -1;
	}

	switch (qtype) {
	case SLI_QTYPE_EQ:
		size = sizeof(uint32_t);
		break;
	case SLI_QTYPE_CQ:
		size = 16;
		break;
	case SLI_QTYPE_MQ:
		size = 256;
		break;
	case SLI_QTYPE_WQ:
		if (SLI4_PORT_TYPE_FC == sli4->port_type) {
			size = sli4->config.wqe_size;
		} else {
			/* TODO */
			ocs_log_test(sli4->os, "unsupported queue entry size\n");
			return -1;
		}
		break;
	case SLI_QTYPE_RQ:
		size = SLI4_FCOE_RQE_SIZE;
		break;
	default:
		ocs_log_test(sli4->os, "unknown queue type %d\n", qtype);
		return -1;
	}
	return size;
}

/**
 * @ingroup sli
 * @brief Modify the delay timer for all the EQs
 *
 * @param sli4 SLI context.
 * @param eq Array of EQs.
 * @param num_eq Count of EQs.
 * @param shift Phase shift for staggering interrupts.
 * @param delay_mult Delay multiplier for limiting interrupt frequency.
 *
 * @return Returns 0 on success, or -1 otherwise.
 */
int32_t
sli_eq_modify_delay(sli4_t *sli4, sli4_queue_t *eq, uint32_t num_eq, uint32_t shift, uint32_t delay_mult)
{

	sli_cmd_common_modify_eq_delay(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE, eq, num_eq, shift, delay_mult);

	if (sli_bmbx_command(sli4)) {
		ocs_log_crit(sli4->os, "bootstrap mailbox write fail (MODIFY EQ DELAY)\n");
		return -1;
	}
	if (sli_res_sli_config(sli4->bmbx.virt)) {
		ocs_log_err(sli4->os, "bad status MODIFY EQ DELAY\n");
		return -1;
	}

	return 0;
}

/**
 * @ingroup sli
 * @brief Allocate a queue.
 *
 * @par Description
 * Allocates DMA memory and configures the requested queue type.
 *
 * @param sli4 SLI context.
 * @param qtype Type of queue to create.
 * @param q Pointer to the queue object.
 * @param n_entries Number of entries to allocate.
 * @param assoc Associated queue (that is, the EQ for a CQ, the CQ for a MQ, and so on).
 * @param ulp The ULP to bind, which is only used for WQ and RQs
 *
 * @return Returns 0 on success, or -1 otherwise.
 */
int32_t
sli_queue_alloc(sli4_t *sli4, uint32_t qtype, sli4_queue_t *q, uint32_t n_entries,
		sli4_queue_t *assoc, uint16_t ulp)
{
	int32_t		size;
	uint32_t	align = 0;
	sli4_create_q_fn_t create = NULL;

	if (!sli4 || !q) {
		ocs_log_err(NULL, "bad parameter sli4=%p q=%p\n", sli4, q);
		return -1;
	}

	/* get queue size */
	size = sli_get_queue_entry_size(sli4, qtype);
	if (size < 0)
		return -1;
	align = SLI_PAGE_SIZE;

	switch (qtype) {
	case SLI_QTYPE_EQ:
		create = sli_cmd_common_create_eq;
		break;
	case SLI_QTYPE_CQ:
		create = sli_cmd_common_create_cq;
		break;
	case SLI_QTYPE_MQ:
		/* Validate the number of entries */
		switch (n_entries) {
		case 16:
		case 32:
		case 64:
		case 128:
			break;
		default:
			ocs_log_test(sli4->os, "illegal n_entries value %d for MQ\n", n_entries);
			return -1;
		}
		assoc->u.flag.is_mq = TRUE;
		create = sli_cmd_common_create_mq_ext;
		break;
	case SLI_QTYPE_WQ:
		if (SLI4_PORT_TYPE_FC == sli4->port_type) {
			if (sli4->if_type == SLI4_IF_TYPE_BE3_SKH_PF) {
				create = sli_cmd_fcoe_wq_create;
			} else {
				create = sli_cmd_fcoe_wq_create_v1;
			}
		} else {
			/* TODO */
			ocs_log_test(sli4->os, "unsupported WQ create\n");
			return -1;
		}
		break;
	default:
		ocs_log_test(sli4->os, "unknown queue type %d\n", qtype);
		return -1;
	}


	if (__sli_queue_init(sli4, q, qtype, size, n_entries, align)) {
		ocs_log_err(sli4->os, "%s allocation failed\n", SLI_QNAME[qtype]);
		return -1;
	}

	if (create(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE, &q->dma, assoc ? assoc->id : 0, ulp)) {

		if (__sli_create_queue(sli4, q)) {
			ocs_log_err(sli4->os, "create %s failed\n", SLI_QNAME[qtype]);
			return -1;
		}
		q->ulp = ulp;
	} else {
		ocs_log_err(sli4->os, "cannot create %s\n", SLI_QNAME[qtype]);
		return -1;
	}

	return 0;
}


/**
 * @ingroup sli
 * @brief Allocate a c queue set.
 *
 * @param sli4 SLI context.
 * @param num_cqs to create
 * @param qs Pointers to the queue objects.
 * @param n_entries Number of entries to allocate per CQ.
 * @param eqs Associated event queues
 *
 * @return Returns 0 on success, or -1 otherwise.
 */
int32_t
sli_cq_alloc_set(sli4_t *sli4, sli4_queue_t *qs[], uint32_t num_cqs,
		 uint32_t n_entries, sli4_queue_t *eqs[])
{
	uint32_t i, offset = 0,  page_bytes = 0, payload_size, cmd_size = 0;
	uint32_t p = 0, page_size = 0, n_cqe = 0, num_pages_cq;
	uintptr_t addr;
	ocs_dma_t dma;
	sli4_req_common_create_cq_set_v0_t  *req = NULL;
	sli4_res_common_create_queue_set_t *res = NULL;

	if (!sli4) {
		ocs_log_err(NULL, "bad parameter sli4=%p\n", sli4);
		return -1;
	}

	memset(&dma, 0, sizeof(dma));

	/* Align the queue DMA memory */
	for (i = 0; i < num_cqs; i++) {
		if (__sli_queue_init(sli4, qs[i], SLI_QTYPE_CQ, SLI4_CQE_BYTES,
			n_entries, SLI_PAGE_SIZE)) {
			ocs_log_err(sli4->os, "Queue init failed.\n");
			goto error;
		}
	}

	n_cqe = qs[0]->dma.size / SLI4_CQE_BYTES;
	switch (n_cqe) {
	case 256:
	case 512:
	case 1024:
	case 2048:
		page_size = 1;
		break;
	case 4096:
		page_size = 2;
		break;
	default:
		return -1;
	}

	page_bytes = page_size * SLI_PAGE_SIZE;
	num_pages_cq = sli_page_count(qs[0]->dma.size, page_bytes);
	cmd_size = sizeof(sli4_req_common_create_cq_set_v0_t) + (8 * num_pages_cq * num_cqs);
	payload_size = max((size_t)cmd_size, sizeof(sli4_res_common_create_queue_set_t));

	if (ocs_dma_alloc(sli4->os, &dma, payload_size, SLI_PAGE_SIZE)) {
		ocs_log_err(sli4->os, "DMA allocation failed\n");
		goto error;
	}
	ocs_memset(dma.virt, 0, payload_size);

	if (sli_cmd_sli_config(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE, 
			payload_size, &dma) == -1) {
		goto error;
	}

	/* Fill the request structure */

	req = (sli4_req_common_create_cq_set_v0_t *)((uint8_t *)dma.virt);
	req->hdr.opcode = SLI4_OPC_COMMON_CREATE_CQ_SET;
	req->hdr.subsystem = SLI4_SUBSYSTEM_FCFCOE;
	req->hdr.version = 0;
	req->hdr.request_length = cmd_size - sizeof(sli4_req_hdr_t);
	req->page_size = page_size;

	req->num_pages = num_pages_cq;
	switch (req->num_pages) {
	case 1:
		req->cqecnt = SLI4_CQ_CNT_256;
		break;
	case 2:
		req->cqecnt = SLI4_CQ_CNT_512;
		break;
	case 4:
		req->cqecnt = SLI4_CQ_CNT_1024;
		break;
	case 8:
		req->cqecnt = SLI4_CQ_CNT_LARGE;
		req->cqe_count = n_cqe;
		break;
	default:
		ocs_log_test(sli4->os, "num_pages %d not valid\n", req->num_pages);
		goto error;
	}

	req->evt = TRUE;
	req->valid = TRUE;
	req->arm = FALSE;
	req->num_cq_req = num_cqs;

	/* Fill page addresses of all the CQs. */
	for (i = 0; i < num_cqs; i++) {
		req->eq_id[i] = eqs[i]->id;
		for (p = 0, addr = qs[i]->dma.phys; p < req->num_pages; p++, addr += page_bytes) {
			req->page_physical_address[offset].low = ocs_addr32_lo(addr);
			req->page_physical_address[offset].high = ocs_addr32_hi(addr);
			offset++;
		}
	}

	if (sli_bmbx_command(sli4)) {
		ocs_log_crit(sli4->os, "bootstrap mailbox write fail CQSet\n");
		goto error;
	}

	res = (void *)((uint8_t *)dma.virt);
	if (res->hdr.status) {
		ocs_log_err(sli4->os, "bad create CQSet status=%#x addl=%#x\n",
			res->hdr.status, res->hdr.additional_status);
		goto error;
	} else {
		/* Check if we got all requested CQs. */
		if (res->num_q_allocated != num_cqs) {
			ocs_log_crit(sli4->os, "Requested count CQs doesnt match.\n");
			goto error;
		}

		/* Fill the resp cq ids. */
		for (i = 0; i < num_cqs; i++) {
			qs[i]->id = res->q_id + i;
			qs[i]->doorbell_offset = regmap[SLI4_REG_EQCQ_DOORBELL][sli4->if_type].off;
			qs[i]->doorbell_rset   = regmap[SLI4_REG_EQCQ_DOORBELL][sli4->if_type].rset;
		}
	}

	ocs_dma_free(sli4->os, &dma);

	return 0;

error:
	for (i = 0; i < num_cqs; i++) {
		if (qs[i]->dma.size) {
			ocs_dma_free(sli4->os, &qs[i]->dma);
		}
	}

	if (dma.size) {
		ocs_dma_free(sli4->os, &dma);
	}

	return -1;
}



/**
 * @ingroup sli
 * @brief Free a queue.
 *
 * @par Description
 * Frees DMA memory and de-registers the requested queue.
 *
 * @param sli4 SLI context.
 * @param q Pointer to the queue object.
 * @param destroy_queues Non-zero if the mailbox commands should be sent to destroy the queues.
 * @param free_memory Non-zero if the DMA memory associated with the queue should be freed.
 *
 * @return Returns 0 on success, or -1 otherwise.
 */
int32_t
sli_queue_free(sli4_t *sli4, sli4_queue_t *q, uint32_t destroy_queues, uint32_t free_memory)
{
	sli4_destroy_q_fn_t destroy = NULL;
	int32_t		rc = -1;

	if (!sli4 || !q) {
		ocs_log_err(NULL, "bad parameter sli4=%p q=%p\n", sli4, q);
		return -1;
	}

	if (destroy_queues) {
		switch (q->type) {
		case SLI_QTYPE_EQ:
			destroy = sli_cmd_common_destroy_eq;
			break;
		case SLI_QTYPE_CQ:
			destroy = sli_cmd_common_destroy_cq;
			break;
		case SLI_QTYPE_MQ:
			destroy = sli_cmd_common_destroy_mq;
			break;
		case SLI_QTYPE_WQ:
			if (SLI4_PORT_TYPE_FC == sli4->port_type) {
				destroy = sli_cmd_fcoe_wq_destroy;
			} else {
				/* TODO */
				ocs_log_test(sli4->os, "unsupported WQ destroy\n");
				return -1;
			}
			break;
		case SLI_QTYPE_RQ:
			if (SLI4_PORT_TYPE_FC == sli4->port_type) {
				destroy = sli_cmd_fcoe_rq_destroy;
			} else {
				/* TODO */
				ocs_log_test(sli4->os, "unsupported RQ destroy\n");
				return -1;
			}
			break;
		default:
			ocs_log_test(sli4->os, "bad queue type %d\n",
					q->type);
			return -1;
		}

		/*
		 * Destroying queues makes BE3 sad (version 0 interface type). Rely
		 * on COMMON_FUNCTION_RESET to free host allocated queue resources
		 * inside the SLI Port.
		 */
		if (SLI4_IF_TYPE_BE3_SKH_PF == sli4->if_type) {
			destroy = NULL;
		}

		/* Destroy the queue if the operation is defined */
		if (destroy && destroy(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE, q->id)) {
			sli4_res_hdr_t	*res = NULL;

			if (sli_bmbx_command(sli4)){
				ocs_log_crit(sli4->os, "bootstrap mailbox write fail destroy %s\n",
						SLI_QNAME[q->type]);
			} else if (sli_res_sli_config(sli4->bmbx.virt)) {
				ocs_log_err(sli4->os, "bad status destroy %s\n", SLI_QNAME[q->type]);
			} else {
				res = (void *)((uint8_t *)sli4->bmbx.virt +
						offsetof(sli4_cmd_sli_config_t, payload));

				if (res->status) {
					ocs_log_err(sli4->os, "bad destroy %s status=%#x addl=%#x\n",
							SLI_QNAME[q->type],
							res->status, res->additional_status);
				} else {
					rc = 0;
				}
			}
		}
	}

	if (free_memory) {
		ocs_lock_free(&q->lock);

		if (ocs_dma_free(sli4->os, &q->dma)) {
			ocs_log_err(sli4->os, "%s queue ID %d free failed\n",
				    SLI_QNAME[q->type], q->id);
			rc = -1;
		}
	}

	return rc;
}

int32_t
sli_queue_reset(sli4_t *sli4, sli4_queue_t *q)
{

	ocs_lock(&q->lock);

	q->index = 0;
	q->n_posted = 0;

	if (SLI_QTYPE_MQ == q->type) {
		q->u.r_idx = 0;
	}

	if (q->dma.virt != NULL) {
		ocs_memset(q->dma.virt, 0, (q->size * (uint64_t)q->length));
	}

	ocs_unlock(&q->lock);

	return 0;
}

/**
 * @ingroup sli
 * @brief Check if the given queue is empty.
 *
 * @par Description
 * If the valid bit of the current entry is unset, the queue is empty.
 *
 * @param sli4 SLI context.
 * @param q Pointer to the queue object.
 *
 * @return Returns TRUE if empty, or FALSE otherwise.
 */
int32_t
sli_queue_is_empty(sli4_t *sli4, sli4_queue_t *q)
{
	int32_t		rc = TRUE;
	uint8_t		*qe = q->dma.virt;

	ocs_lock(&q->lock);

	ocs_dma_sync(&q->dma, OCS_DMASYNC_POSTREAD);

	qe += q->index * q->size;

	rc = !sli_queue_entry_is_valid(q, qe, FALSE);

	ocs_unlock(&q->lock);

	return rc;
}

/**
 * @ingroup sli
 * @brief Arm an EQ.
 *
 * @param sli4 SLI context.
 * @param q Pointer to queue object.
 * @param arm If TRUE, arm the EQ.
 *
 * @return Returns 0 on success, or non-zero otherwise.
 */
int32_t
sli_queue_eq_arm(sli4_t *sli4, sli4_queue_t *q, uint8_t arm)
{
	uint32_t	val = 0;

	ocs_lock(&q->lock);
		val = sli_eq_doorbell(q->n_posted, q->id, arm);
		ocs_reg_write32(sli4->os, q->doorbell_rset, q->doorbell_offset, val);
		q->n_posted = 0;
	ocs_unlock(&q->lock);

	return 0;
}

/**
 * @ingroup sli
 * @brief Arm a queue.
 *
 * @param sli4 SLI context.
 * @param q Pointer to queue object.
 * @param arm If TRUE, arm the queue.
 *
 * @return Returns 0 on success, or non-zero otherwise.
 */
int32_t
sli_queue_arm(sli4_t *sli4, sli4_queue_t *q, uint8_t arm)
{
	uint32_t	val = 0;

	ocs_lock(&q->lock);

	switch (q->type) {
	case SLI_QTYPE_EQ:
		val = sli_eq_doorbell(q->n_posted, q->id, arm);
		ocs_reg_write32(sli4->os, q->doorbell_rset, q->doorbell_offset, val);
		q->n_posted = 0;
		break;
	case SLI_QTYPE_CQ:
		val = sli_cq_doorbell(q->n_posted, q->id, arm);
		ocs_reg_write32(sli4->os, q->doorbell_rset, q->doorbell_offset, val);
		q->n_posted = 0;
		break;
	default:
		ocs_log_test(sli4->os, "should only be used for EQ/CQ, not %s\n",
			     SLI_QNAME[q->type]);
	}

	ocs_unlock(&q->lock);

	return 0;
}

/**
 * @ingroup sli
 * @brief Write an entry to the queue object.
 *
 * Note: Assumes the q->lock will be locked and released by the caller.
 *
 * @param sli4 SLI context.
 * @param q Pointer to the queue object.
 * @param entry Pointer to the entry contents.
 *
 * @return Returns queue index on success, or negative error value otherwise.
 */
int32_t
_sli_queue_write(sli4_t *sli4, sli4_queue_t *q, uint8_t *entry)
{
	int32_t		rc = 0;
	uint8_t		*qe = q->dma.virt;
	uint32_t	qindex;

	qindex = q->index;
	qe += q->index * q->size;

	if (entry) {
		if ((SLI_QTYPE_WQ == q->type) && sli4->config.perf_wq_id_association) {
			sli_set_wq_id_association(entry, q->id);
		}
#if defined(OCS_INCLUDE_DEBUG)
		switch (q->type) {
		case SLI_QTYPE_WQ: {
			ocs_dump32(OCS_DEBUG_ENABLE_WQ_DUMP, sli4->os, "wqe", entry, q->size);
			break;

		}
		case SLI_QTYPE_MQ:
			/* Note: we don't really need to dump the whole 
			 * 256 bytes, just do 64 */
			ocs_dump32(OCS_DEBUG_ENABLE_MQ_DUMP, sli4->os, "mqe outbound", entry, 64);
			break;

		default:
			break;
		}
#endif
		ocs_memcpy(qe, entry, q->size);
		q->n_posted = 1;
	}

	ocs_dma_sync(&q->dma, OCS_DMASYNC_PREWRITE);

	rc = sli_queue_doorbell(sli4, q);

	q->index = (q->index + q->n_posted) & (q->length - 1);
	q->n_posted = 0;

	if (rc < 0) {
		/* failure */
		return rc;
	} else if (rc > 0) {
		/* failure, but we need to return a negative value on failure */
		return -rc;
	} else {
		return qindex;
	}
}

/**
 * @ingroup sli
 * @brief Write an entry to the queue object.
 *
 * Note: Assumes the q->lock will be locked and released by the caller.
 *
 * @param sli4 SLI context.
 * @param q Pointer to the queue object.
 * @param entry Pointer to the entry contents.
 *
 * @return Returns queue index on success, or negative error value otherwise.
 */
int32_t
sli_queue_write(sli4_t *sli4, sli4_queue_t *q, uint8_t *entry)
{
	int32_t rc;

	ocs_lock(&q->lock);
		rc = _sli_queue_write(sli4, q, entry);
	ocs_unlock(&q->lock);

	return rc;
}

/**
 * @brief Check if the current queue entry is valid.
 *
 * @param q Pointer to the queue object.
 * @param qe Pointer to the queue entry.
 * @param clear Boolean to clear valid bit.
 *
 * @return Returns TRUE if the entry is valid, or FALSE otherwise.
 */
static uint8_t
sli_queue_entry_is_valid(sli4_queue_t *q, uint8_t *qe, uint8_t clear)
{
	uint8_t		valid = FALSE;

	switch (q->type) {
	case SLI_QTYPE_EQ:
		valid = ((sli4_eqe_t *)qe)->vld;
		if (valid && clear) {
			((sli4_eqe_t *)qe)->vld = 0;
		}
		break;
	case SLI_QTYPE_CQ:
		/*
		 * For both MCQE and WCQE/RCQE, the valid bit
		 * is bit 31 of dword 3 (0 based)
		 */
		valid = (qe[15] & 0x80) != 0;
		if (valid & clear) {
			qe[15] &= ~0x80;
		}
		break;
	case SLI_QTYPE_MQ:
		valid = q->index != q->u.r_idx;
		break;
	case SLI_QTYPE_RQ:
		valid = TRUE;
		clear = FALSE;
		break;
	default:
		ocs_log_test(NULL, "doesn't handle type=%#x\n", q->type);
	}

	if (clear) {
		ocs_dma_sync(&q->dma, OCS_DMASYNC_PREWRITE);
	}

	return valid;
}

/**
 * @ingroup sli
 * @brief Read an entry from the queue object.
 *
 * @param sli4 SLI context.
 * @param q Pointer to the queue object.
 * @param entry Destination pointer for the queue entry contents.
 *
 * @return Returns 0 on success, or non-zero otherwise.
 */
int32_t
sli_queue_read(sli4_t *sli4, sli4_queue_t *q, uint8_t *entry)
{
	int32_t		rc = 0;
	uint8_t		*qe = q->dma.virt;
	uint32_t	*qindex = NULL;

	if (SLI_QTYPE_MQ == q->type) {
		qindex = &q->u.r_idx;
	} else {
		qindex = &q->index;
	}

	ocs_lock(&q->lock);

	ocs_dma_sync(&q->dma, OCS_DMASYNC_POSTREAD);

	qe += *qindex * q->size;

	if (!sli_queue_entry_is_valid(q, qe, TRUE)) {
		ocs_unlock(&q->lock);
		return -1;
	}

	if (entry) {
		ocs_memcpy(entry, qe, q->size);
#if defined(OCS_INCLUDE_DEBUG)
		switch(q->type) {
		case SLI_QTYPE_CQ:
			ocs_dump32(OCS_DEBUG_ENABLE_CQ_DUMP, sli4->os, "cq", entry, q->size);
			break;
		case SLI_QTYPE_MQ:
			ocs_dump32(OCS_DEBUG_ENABLE_MQ_DUMP, sli4->os, "mq Compl", entry, 64);
			break;
		case SLI_QTYPE_EQ:
			ocs_dump32(OCS_DEBUG_ENABLE_EQ_DUMP, sli4->os, "eq Compl", entry, q->size);
			break;
		default:
			break;
		}
#endif
	}

	switch (q->type) {
		case SLI_QTYPE_EQ:
		case SLI_QTYPE_CQ:
		case SLI_QTYPE_MQ:
			*qindex = (*qindex + 1) & (q->length - 1);
			if (SLI_QTYPE_MQ != q->type) {
				q->n_posted++;
			}
			break;
		default:
			/* reads don't update the index */
			break;
	}

	ocs_unlock(&q->lock);

	return rc;
}

int32_t
sli_queue_index(sli4_t *sli4, sli4_queue_t *q)
{

	if (q) {
		return q->index;
	} else {
		return -1;
	}
}

int32_t
sli_queue_poke(sli4_t *sli4, sli4_queue_t *q, uint32_t index, uint8_t *entry)
{
	int32_t rc;

	ocs_lock(&q->lock);
		rc = _sli_queue_poke(sli4, q, index, entry);
	ocs_unlock(&q->lock);

	return rc;
}

int32_t
_sli_queue_poke(sli4_t *sli4, sli4_queue_t *q, uint32_t index, uint8_t *entry)
{
	int32_t		rc = 0;
	uint8_t		*qe = q->dma.virt;

	if (index >= q->length) {
		return -1;
	}

	qe += index * q->size;

	if (entry) {
		ocs_memcpy(qe, entry, q->size);
	}

	ocs_dma_sync(&q->dma, OCS_DMASYNC_PREWRITE);

	return rc;
}

/**
 * @ingroup sli
 * @brief Allocate SLI Port resources.
 *
 * @par Description
 * Allocate port-related resources, such as VFI, RPI, XRI, and so on.
 * Resources are modeled using extents, regardless of whether the underlying
 * device implements resource extents. If the device does not implement
 * extents, the SLI layer models this as a single (albeit large) extent.
 *
 * @param sli4 SLI context.
 * @param rtype Resource type (for example, RPI or XRI)
 * @param rid Allocated resource ID.
 * @param index Index into the bitmap.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_resource_alloc(sli4_t *sli4, sli4_resource_e rtype, uint32_t *rid, uint32_t *index)
{
	int32_t		rc = 0;
	uint32_t	size;
	uint32_t	extent_idx;
	uint32_t	item_idx;
	int		status;

	*rid = UINT32_MAX;
	*index = UINT32_MAX;

	switch (rtype) {
	case SLI_RSRC_FCOE_VFI:
	case SLI_RSRC_FCOE_VPI:
	case SLI_RSRC_FCOE_RPI:
	case SLI_RSRC_FCOE_XRI:
		status = ocs_bitmap_find(sli4->config.extent[rtype].use_map,
				sli4->config.extent[rtype].map_size);
		if (status < 0) {
			ocs_log_err(sli4->os, "out of resource %d (alloc=%d)\n",
					rtype, sli4->config.extent[rtype].n_alloc);
			rc = -1;
			break;
		} else {
			*index = status;
		}

		size = sli4->config.extent[rtype].size;

		extent_idx = *index / size;
		item_idx   = *index % size;

		*rid = sli4->config.extent[rtype].base[extent_idx] + item_idx;

		sli4->config.extent[rtype].n_alloc++;
		break;
	default:
		rc = -1;
	}

	return rc;
}

/**
 * @ingroup sli
 * @brief Free the SLI Port resources.
 *
 * @par Description
 * Free port-related resources, such as VFI, RPI, XRI, and so. See discussion of
 * "extent" usage in sli_resource_alloc.
 *
 * @param sli4 SLI context.
 * @param rtype Resource type (for example, RPI or XRI).
 * @param rid Allocated resource ID.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_resource_free(sli4_t *sli4, sli4_resource_e rtype, uint32_t rid)
{
	int32_t		rc = -1;
	uint32_t	x;
	uint32_t	size, *base;

	switch (rtype) {
	case SLI_RSRC_FCOE_VFI:
	case SLI_RSRC_FCOE_VPI:
	case SLI_RSRC_FCOE_RPI:
	case SLI_RSRC_FCOE_XRI:
		/*
		 * Figure out which extent contains the resource ID. I.e. find
		 * the extent such that
		 *   extent->base <= resource ID < extent->base + extent->size
		 */
		base = sli4->config.extent[rtype].base;
		size = sli4->config.extent[rtype].size;

		/*
		 * In the case of FW reset, this may be cleared but the force_free path will
		 * still attempt to free the resource. Prevent a NULL pointer access.
		 */
		if (base != NULL) {
			for (x = 0; x < sli4->config.extent[rtype].number; x++) {
				if ((rid >= base[x]) && (rid < (base[x] + size))) {
					rid -= base[x];
					ocs_bitmap_clear(sli4->config.extent[rtype].use_map,
							 (x * size) + rid);
					rc = 0;
					break;
				}
			}
		}
		break;
	default:
		;
	}

	return rc;
}

int32_t
sli_resource_reset(sli4_t *sli4, sli4_resource_e rtype)
{
	int32_t		rc = -1;
	uint32_t	i;

	switch (rtype) {
	case SLI_RSRC_FCOE_VFI:
	case SLI_RSRC_FCOE_VPI:
	case SLI_RSRC_FCOE_RPI:
	case SLI_RSRC_FCOE_XRI:
		for (i = 0; i < sli4->config.extent[rtype].map_size; i++) {
			ocs_bitmap_clear(sli4->config.extent[rtype].use_map, i);
		}
		rc = 0;
		break;
	default:
		;
	}

	return rc;
}

/**
 * @ingroup sli
 * @brief Parse an EQ entry to retrieve the CQ_ID for this event.
 *
 * @param sli4 SLI context.
 * @param buf Pointer to the EQ entry.
 * @param cq_id CQ_ID for this entry (only valid on success).
 *
 * @return
 * - 0 if success.
 * - < 0 if error.
 * - > 0 if firmware detects EQ overflow.
 */
int32_t
sli_eq_parse(sli4_t *sli4, uint8_t *buf, uint16_t *cq_id)
{
	sli4_eqe_t	*eqe = (void *)buf;
	int32_t		rc = 0;

	if (!sli4 || !buf || !cq_id) {
		ocs_log_err(NULL, "bad parameters sli4=%p buf=%p cq_id=%p\n",
				sli4, buf, cq_id);
		return -1;
	}

	switch (eqe->major_code) {
	case SLI4_MAJOR_CODE_STANDARD:
		*cq_id = eqe->resource_id;
		break;
	case SLI4_MAJOR_CODE_SENTINEL:
		ocs_log_debug(sli4->os, "sentinel EQE\n");
		rc = 1;
		break;
	default:
		ocs_log_test(sli4->os, "Unsupported EQE: major %x minor %x\n",
				eqe->major_code, eqe->minor_code);
		rc = -1;
	}

	return rc;
}

/**
 * @ingroup sli
 * @brief Parse a CQ entry to retrieve the event type and the associated queue.
 *
 * @param sli4 SLI context.
 * @param cq CQ to process.
 * @param cqe Pointer to the CQ entry.
 * @param etype CQ event type.
 * @param q_id Queue ID associated with this completion message
 * (that is, MQ_ID, RQ_ID, and so on).
 *
 * @return
 * - 0 if call completed correctly and CQE status is SUCCESS.
 * - -1 if call failed (no CQE status).
 * - Other value if call completed correctly and return value is a CQE status value.
 */
int32_t
sli_cq_parse(sli4_t *sli4, sli4_queue_t *cq, uint8_t *cqe, sli4_qentry_e *etype,
		uint16_t *q_id)
{
	int32_t	rc = 0;

	if (!sli4 || !cq || !cqe || !etype) {
		ocs_log_err(NULL, "bad parameters sli4=%p cq=%p cqe=%p etype=%p q_id=%p\n",
			    sli4, cq, cqe, etype, q_id);
		return -1;
	}

	if (cq->u.flag.is_mq) {
		sli4_mcqe_t	*mcqe = (void *)cqe;

		if (mcqe->ae) {
			*etype = SLI_QENTRY_ASYNC;
		} else {
			*etype = SLI_QENTRY_MQ;
			rc = sli_cqe_mq(mcqe);
		}
		*q_id = -1;
	} else if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		rc = sli_fc_cqe_parse(sli4, cq, cqe, etype, q_id);
	} else {
		ocs_log_test(sli4->os, "implement CQE parsing type = %#x\n",
			     sli4->port_type);
		rc = -1;
	}

	return rc;
}

/**
 * @ingroup sli
 * @brief Cause chip to enter an unrecoverable error state.
 *
 * @par Description
 * Cause chip to enter an unrecoverable error state. This is
 * used when detecting unexpected FW behavior so FW can be
 * hwted from the driver as soon as error is detected.
 *
 * @param sli4 SLI context.
 * @param dump Generate dump as part of reset.
 *
 * @return Returns 0 if call completed correctly, or -1 if call failed (unsupported chip).
 */
int32_t sli_raise_ue(sli4_t *sli4, uint8_t dump)
{
#define FDD 2
	if (SLI4_IF_TYPE_BE3_SKH_PF == sli_get_if_type(sli4)) {
		switch(sli_get_asic_type(sli4)) {
		case SLI4_ASIC_TYPE_BE3: {
			sli_reg_write(sli4, SLI4_REG_SW_UE_CSR1, 0xffffffff);
			sli_reg_write(sli4, SLI4_REG_SW_UE_CSR2, 0);
			break;
		}
		case SLI4_ASIC_TYPE_SKYHAWK: {
			uint32_t value;
			value = ocs_config_read32(sli4->os, SLI4_SW_UE_REG);
			ocs_config_write32(sli4->os, SLI4_SW_UE_REG, (value | (1U << 24)));
			break;
		}
		default:
			ocs_log_test(sli4->os, "invalid asic type %d\n", sli_get_asic_type(sli4));
			return -1;
		}
	} else if (SLI4_IF_TYPE_LANCER_FC_ETH == sli_get_if_type(sli4)) {	
		if (dump == FDD) {
			sli_reg_write(sli4, SLI4_REG_SLIPORT_CONTROL, SLI4_SLIPORT_CONTROL_FDD | SLI4_SLIPORT_CONTROL_IP);
		} else {
			uint32_t value = SLI4_PHYDEV_CONTROL_FRST;
			if (dump == 1) {
				value |= SLI4_PHYDEV_CONTROL_DD;
			}
			sli_reg_write(sli4, SLI4_REG_PHYSDEV_CONTROL, value);
		}
	} else {
		ocs_log_test(sli4->os, "invalid iftype=%d\n", sli_get_if_type(sli4));
		return -1;
	}
	return 0;
}

/**
 * @ingroup sli
 * @brief Read the SLIPORT_STATUS register to to check if a dump is present.
 *
 * @param sli4 SLI context.
 *
 * @return  Returns 1 if the chip is ready, or 0 if the chip is not ready, 2 if fdp is present.
 */
int32_t sli_dump_is_ready(sli4_t *sli4)
{
	int32_t	rc = 0;
	uint32_t port_val;
	uint32_t bmbx_val;
	uint32_t uerr_lo;
	uint32_t uerr_hi;
	uint32_t uerr_mask_lo;
	uint32_t uerr_mask_hi;

	if (SLI4_IF_TYPE_BE3_SKH_PF == sli_get_if_type(sli4)) {
		/* for iftype=0, dump ready when UE is encountered */
		uerr_lo = sli_reg_read(sli4, SLI4_REG_UERR_STATUS_LO);
		uerr_hi = sli_reg_read(sli4, SLI4_REG_UERR_STATUS_HI);
		uerr_mask_lo = sli_reg_read(sli4, SLI4_REG_UERR_MASK_LO);
		uerr_mask_hi = sli_reg_read(sli4, SLI4_REG_UERR_MASK_HI);
		if ((uerr_lo & ~uerr_mask_lo) || (uerr_hi & ~uerr_mask_hi)) {
			rc = 1;
		}

	} else if (SLI4_IF_TYPE_LANCER_FC_ETH == sli_get_if_type(sli4)) {
		/*
		 * Ensure that the port is ready AND the mailbox is
		 * ready before signaling that the dump is ready to go.
		 */
		port_val = sli_reg_read(sli4, SLI4_REG_SLIPORT_STATUS);
		bmbx_val = sli_reg_read(sli4, SLI4_REG_BMBX);

		if ((bmbx_val & SLI4_BMBX_RDY) &&
		    SLI4_PORT_STATUS_READY(port_val)) { 
		    	if(SLI4_PORT_STATUS_DUMP_PRESENT(port_val)) {
				rc = 1;
			}else if( SLI4_PORT_STATUS_FDP_PRESENT(port_val)) {
				rc = 2;
			}
		}
	} else {
		ocs_log_test(sli4->os, "invalid iftype=%d\n", sli_get_if_type(sli4));
		return -1;
	}
	return rc;
}

/**
 * @ingroup sli
 * @brief Read the SLIPORT_STATUS register to check if a dump is present.
 *
 * @param sli4 SLI context.
 *
 * @return
 * - 0 if call completed correctly and no dump is present.
 * - 1 if call completed and dump is present.
 * - -1 if call failed (unsupported chip).
 */
int32_t sli_dump_is_present(sli4_t *sli4)
{
	uint32_t val;
	uint32_t ready;

	if (SLI4_IF_TYPE_LANCER_FC_ETH != sli_get_if_type(sli4)) {
		ocs_log_test(sli4->os, "Function only supported for I/F type 2");
		return -1;
	}

	/* If the chip is not ready, then there cannot be a dump */
	ready = sli_wait_for_fw_ready(sli4, SLI4_INIT_PORT_DELAY_US);
	if (!ready) {
		return 0;
	}

	val = sli_reg_read(sli4, SLI4_REG_SLIPORT_STATUS);
	if (UINT32_MAX == val) {
		ocs_log_err(sli4->os, "error reading SLIPORT_STATUS\n");
		return -1;
	} else {
		return ((val & SLI4_PORT_STATUS_DIP) ? 1 : 0);
	}
}

/**
 * @ingroup sli
 * @brief Read the SLIPORT_STATUS register to check if the reset required is set.
 *
 * @param sli4 SLI context.
 *
 * @return
 * - 0 if call completed correctly and reset is not required.
 * - 1 if call completed and reset is required.
 * - -1 if call failed.
 */
int32_t sli_reset_required(sli4_t *sli4)
{
	uint32_t val;

	if (SLI4_IF_TYPE_BE3_SKH_PF == sli_get_if_type(sli4)) {
		ocs_log_test(sli4->os, "reset required N/A for iftype 0\n");
		return 0;
	}

	val = sli_reg_read(sli4, SLI4_REG_SLIPORT_STATUS);
	if (UINT32_MAX == val) {
		ocs_log_err(sli4->os, "error reading SLIPORT_STATUS\n");
		return -1;
	} else {
		return ((val & SLI4_PORT_STATUS_RN) ? 1 : 0);
	}
}

/**
 * @ingroup sli
 * @brief Read the SLIPORT_SEMAPHORE and SLIPORT_STATUS registers to check if
 * the port status indicates that a FW error has occurred.
 *
 * @param sli4 SLI context.
 *
 * @return
 * - 0 if call completed correctly and no FW error occurred.
 * - > 0 which indicates that a FW error has occurred.
 * - -1 if call failed.
 */
int32_t sli_fw_error_status(sli4_t *sli4)
{
	uint32_t sliport_semaphore;
	int32_t rc = 0;

	sliport_semaphore = sli_reg_read(sli4, SLI4_REG_SLIPORT_SEMAPHORE);
	if (UINT32_MAX == sliport_semaphore) {
		ocs_log_err(sli4->os, "error reading SLIPORT_SEMAPHORE register\n");
		return 1;
	}
	rc = (SLI4_PORT_SEMAPHORE_IN_ERR(sliport_semaphore) ? 1 : 0);

	if (rc == 0) {
		if (SLI4_IF_TYPE_BE3_SKH_PF == sli4->if_type ||
		    (SLI4_IF_TYPE_BE3_SKH_VF == sli4->if_type)) {
			uint32_t uerr_mask_lo, uerr_mask_hi;
			uint32_t uerr_status_lo, uerr_status_hi;

			uerr_mask_lo = sli_reg_read(sli4, SLI4_REG_UERR_MASK_LO);
			uerr_mask_hi = sli_reg_read(sli4, SLI4_REG_UERR_MASK_HI);
			uerr_status_lo = sli_reg_read(sli4, SLI4_REG_UERR_STATUS_LO);
			uerr_status_hi = sli_reg_read(sli4, SLI4_REG_UERR_STATUS_HI);
			if ((uerr_mask_lo & uerr_status_lo) != 0 ||
			    (uerr_mask_hi & uerr_status_hi) != 0) {
				rc = 1;
			}
		} else if ((SLI4_IF_TYPE_LANCER_FC_ETH == sli4->if_type)) {
			uint32_t sliport_status;

			sliport_status = sli_reg_read(sli4, SLI4_REG_SLIPORT_STATUS);
			rc = (SLI4_PORT_STATUS_ERROR(sliport_status) ? 1 : 0);
		}
	}
	return rc;
}

/**
 * @ingroup sli
 * @brief Determine if the chip FW is in a ready state
 *
 * @param sli4 SLI context.
 *
 * @return
 * - 0 if call completed correctly and FW is not ready.
 * - 1 if call completed correctly and FW is ready.
 * - -1 if call failed.
 */
int32_t
sli_fw_ready(sli4_t *sli4)
{
	uint32_t val;
	int32_t rc = -1;

	/*
	 * Is firmware ready for operation? Check needed depends on IF_TYPE
	 */
	if (SLI4_IF_TYPE_BE3_SKH_PF == sli4->if_type ||
	    SLI4_IF_TYPE_BE3_SKH_VF == sli4->if_type) {
		val = sli_reg_read(sli4, SLI4_REG_SLIPORT_SEMAPHORE);
		rc = ((SLI4_PORT_SEMAPHORE_STATUS_POST_READY ==
		       SLI4_PORT_SEMAPHORE_PORT(val)) &&
		      (!SLI4_PORT_SEMAPHORE_IN_ERR(val)) ? 1 : 0);
	} else if (SLI4_IF_TYPE_LANCER_FC_ETH == sli4->if_type) {
		val = sli_reg_read(sli4, SLI4_REG_SLIPORT_STATUS);
		rc = (SLI4_PORT_STATUS_READY(val) ? 1 : 0);
	}
	return rc;
}

/**
 * @ingroup sli
 * @brief Determine if the link can be configured
 *
 * @param sli4 SLI context.
 *
 * @return
 * - 0 if link is not configurable.
 * - 1 if link is configurable.
 */
int32_t sli_link_is_configurable(sli4_t *sli)
{
	int32_t rc = 0;
	/*
	 * Link config works on: Skyhawk and Lancer
	 * Link config does not work on: LancerG6
	 */

	switch (sli_get_asic_type(sli)) {
	case SLI4_ASIC_TYPE_SKYHAWK:
	case SLI4_ASIC_TYPE_LANCER:
	case SLI4_ASIC_TYPE_CORSAIR:
		rc = 1;
		break;
	case SLI4_ASIC_TYPE_LANCERG6:
	case SLI4_ASIC_TYPE_BE3:
	default:
		rc = 0;
		break;
	}

	return rc;

}

/* vim: set noexpandtab textwidth=120: */

/**
 * @ingroup sli_fc
 * @brief Write an FCOE_WQ_CREATE command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param qmem DMA memory for the queue.
 * @param cq_id Associated CQ_ID.
 * @param ulp The ULP to bind
 *
 * @note This creates a Version 0 message.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_fcoe_wq_create(sli4_t *sli4, void *buf, size_t size,
		       ocs_dma_t *qmem, uint16_t cq_id, uint16_t ulp)
{
	sli4_req_fcoe_wq_create_t	*wq = NULL;
	uint32_t	sli_config_off = 0;
	uint32_t	p;
	uintptr_t	addr;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		uint32_t payload_size;

		/* Payload length must accommodate both request and response */
		payload_size = max(sizeof(sli4_req_fcoe_wq_create_t),
				sizeof(sli4_res_common_create_queue_t));

		sli_config_off = sli_cmd_sli_config(sli4, buf, size, payload_size,
				NULL);
	}
	wq = (sli4_req_fcoe_wq_create_t *)((uint8_t *)buf + sli_config_off);

	wq->hdr.opcode = SLI4_OPC_FCOE_WQ_CREATE;
	wq->hdr.subsystem = SLI4_SUBSYSTEM_FCFCOE;
	wq->hdr.request_length = sizeof(sli4_req_fcoe_wq_create_t) -
					sizeof(sli4_req_hdr_t);
	/* valid values for number of pages: 1-4 (sec 4.5.1) */
	wq->num_pages = sli_page_count(qmem->size, SLI_PAGE_SIZE);
	if (!wq->num_pages || (wq->num_pages > SLI4_FCOE_WQ_CREATE_V0_MAX_PAGES)) {
		return 0;
	}

	wq->cq_id = cq_id;

	if (sli4->config.dual_ulp_capable) {
		wq->dua = 1;
		wq->bqu = 1;
		wq->ulp = ulp;
	}

	for (p = 0, addr = qmem->phys;
			p < wq->num_pages;
			p++, addr += SLI_PAGE_SIZE) {
		wq->page_physical_address[p].low  = ocs_addr32_lo(addr);
		wq->page_physical_address[p].high = ocs_addr32_hi(addr);
	}

	return(sli_config_off + sizeof(sli4_req_fcoe_wq_create_t));
}

/**
 * @ingroup sli_fc
 * @brief Write an FCOE_WQ_CREATE_V1 command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param qmem DMA memory for the queue.
 * @param cq_id Associated CQ_ID.
 * @param ignored This parameter carries the ULP for WQ (ignored for V1)

 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_fcoe_wq_create_v1(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *qmem,
			  uint16_t cq_id, uint16_t ignored)
{
	sli4_req_fcoe_wq_create_v1_t	*wq = NULL;
	uint32_t	sli_config_off = 0;
	uint32_t	p;
	uintptr_t	addr;
	uint32_t	page_size = 0;
	uint32_t	page_bytes = 0;
	uint32_t	n_wqe = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		uint32_t payload_size;

		/* Payload length must accommodate both request and response */
		payload_size = max(sizeof(sli4_req_fcoe_wq_create_v1_t),
				sizeof(sli4_res_common_create_queue_t));

		sli_config_off = sli_cmd_sli_config(sli4, buf, size, payload_size,
				NULL);
	}
	wq = (sli4_req_fcoe_wq_create_v1_t *)((uint8_t *)buf + sli_config_off);

	wq->hdr.opcode = SLI4_OPC_FCOE_WQ_CREATE;
	wq->hdr.subsystem = SLI4_SUBSYSTEM_FCFCOE;
	wq->hdr.request_length = sizeof(sli4_req_fcoe_wq_create_v1_t) -
					sizeof(sli4_req_hdr_t);
	wq->hdr.version = 1;

	n_wqe = qmem->size / sli4->config.wqe_size;

	/* This heuristic to determine the page size is simplistic 
	 * but could be made more sophisticated
	 */
	switch (qmem->size) {
	case 4096:
	case 8192:
	case 16384:
	case 32768:
		page_size = 1;
		break;
	case 65536:
		page_size = 2;
		break;
	case 131072:
		page_size = 4;
		break;
	case 262144:
		page_size = 8;
		break;
	case 524288:
		page_size = 10;
		break;
	default:
		return 0;
	}
	page_bytes = page_size * SLI_PAGE_SIZE;

	/* valid values for number of pages: 1-8 */
	wq->num_pages = sli_page_count(qmem->size, page_bytes);
	if (!wq->num_pages || (wq->num_pages > SLI4_FCOE_WQ_CREATE_V1_MAX_PAGES)) {
		return 0;
	}

	wq->cq_id = cq_id;

	wq->page_size = page_size;

	if (sli4->config.wqe_size == SLI4_WQE_EXT_BYTES) {
		wq->wqe_size = SLI4_WQE_EXT_SIZE;
	} else {
		wq->wqe_size = SLI4_WQE_SIZE;
	}

	wq->wqe_count = n_wqe;

	for (p = 0, addr = qmem->phys;
			p < wq->num_pages;
			p++, addr += page_bytes) {
		wq->page_physical_address[p].low  = ocs_addr32_lo(addr);
		wq->page_physical_address[p].high = ocs_addr32_hi(addr);
	}

	return(sli_config_off + sizeof(sli4_req_fcoe_wq_create_v1_t));
}

/**
 * @ingroup sli_fc
 * @brief Write an FCOE_WQ_DESTROY command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param wq_id WQ_ID.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_fcoe_wq_destroy(sli4_t *sli4, void *buf, size_t size, uint16_t wq_id)
{
	sli4_req_fcoe_wq_destroy_t	*wq = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		uint32_t payload_size;

		/* Payload length must accommodate both request and response */
		payload_size = max(sizeof(sli4_req_fcoe_wq_destroy_t),
				sizeof(sli4_res_hdr_t));

		sli_config_off = sli_cmd_sli_config(sli4, buf, size, payload_size,
				NULL);
	}
	wq = (sli4_req_fcoe_wq_destroy_t *)((uint8_t *)buf + sli_config_off);

	wq->hdr.opcode = SLI4_OPC_FCOE_WQ_DESTROY;
	wq->hdr.subsystem = SLI4_SUBSYSTEM_FCFCOE;
	wq->hdr.request_length = sizeof(sli4_req_fcoe_wq_destroy_t) -
					sizeof(sli4_req_hdr_t);

	wq->wq_id = wq_id;

	return(sli_config_off + sizeof(sli4_req_fcoe_wq_destroy_t));
}

/**
 * @ingroup sli_fc
 * @brief Write an FCOE_POST_SGL_PAGES command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param xri starting XRI
 * @param xri_count XRI
 * @param page0 First SGL memory page.
 * @param page1 Second SGL memory page (optional).
 * @param dma DMA buffer for non-embedded mailbox command (options)
 *
 * if non-embedded mbx command is used, dma buffer must be at least (32 + xri_count*16) in length
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_fcoe_post_sgl_pages(sli4_t *sli4, void *buf, size_t size,
		uint16_t xri, uint32_t xri_count, ocs_dma_t *page0[], ocs_dma_t *page1[], ocs_dma_t *dma)
{
	sli4_req_fcoe_post_sgl_pages_t	*post = NULL;
	uint32_t	sli_config_off = 0;
	uint32_t	i;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		uint32_t payload_size;

		/* Payload length must accommodate both request and response */
		payload_size = max(sizeof(sli4_req_fcoe_post_sgl_pages_t),
				sizeof(sli4_res_hdr_t));

		sli_config_off = sli_cmd_sli_config(sli4, buf, size, payload_size,
				dma);
	}
	if (dma) {
		post = dma->virt;
		ocs_memset(post, 0, dma->size);
	} else {
		post = (sli4_req_fcoe_post_sgl_pages_t *)((uint8_t *)buf + sli_config_off);
	}

	post->hdr.opcode = SLI4_OPC_FCOE_POST_SGL_PAGES;
	post->hdr.subsystem = SLI4_SUBSYSTEM_FCFCOE;
	/* payload size calculation
	 *   4 = xri_start + xri_count
	 *   xri_count = # of XRI's registered
	 *   sizeof(uint64_t) = physical address size
	 *   2 = # of physical addresses per page set
	 */
	post->hdr.request_length = 4 + (xri_count * (sizeof(uint64_t) * 2));

	post->xri_start = xri;
	post->xri_count = xri_count;

	for (i = 0; i < xri_count; i++) {
		post->page_set[i].page0_low  = ocs_addr32_lo(page0[i]->phys);
		post->page_set[i].page0_high = ocs_addr32_hi(page0[i]->phys);
	}

	if (page1) {
		for (i = 0; i < xri_count; i++) {
			post->page_set[i].page1_low  = ocs_addr32_lo(page1[i]->phys);
			post->page_set[i].page1_high = ocs_addr32_hi(page1[i]->phys);
		}
	}

	return dma ? sli_config_off : (sli_config_off + sizeof(sli4_req_fcoe_post_sgl_pages_t));
}

/**
 * @ingroup sli_fc
 * @brief Write an FCOE_RQ_CREATE command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param qmem DMA memory for the queue.
 * @param cq_id Associated CQ_ID.
 * @param ulp This parameter carries the ULP for the RQ
 * @param buffer_size Buffer size pointed to by each RQE.
 *
 * @note This creates a Version 0 message.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_fcoe_rq_create(sli4_t *sli4, void *buf, size_t size,
		ocs_dma_t *qmem, uint16_t cq_id, uint16_t ulp, uint16_t buffer_size)
{
	sli4_req_fcoe_rq_create_t	*rq = NULL;
	uint32_t	sli_config_off = 0;
	uint32_t	p;
	uintptr_t	addr;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		uint32_t payload_size;

		/* Payload length must accommodate both request and response */
		payload_size = max(sizeof(sli4_req_fcoe_rq_create_t),
				sizeof(sli4_res_common_create_queue_t));

		sli_config_off = sli_cmd_sli_config(sli4, buf, size, payload_size,
				NULL);
	}
	rq = (sli4_req_fcoe_rq_create_t *)((uint8_t *)buf + sli_config_off);

	rq->hdr.opcode = SLI4_OPC_FCOE_RQ_CREATE;
	rq->hdr.subsystem = SLI4_SUBSYSTEM_FCFCOE;
	rq->hdr.request_length = sizeof(sli4_req_fcoe_rq_create_t) -
					sizeof(sli4_req_hdr_t);
	/* valid values for number of pages: 1-8 (sec 4.5.6) */
	rq->num_pages = sli_page_count(qmem->size, SLI_PAGE_SIZE);
	if (!rq->num_pages || (rq->num_pages > SLI4_FCOE_RQ_CREATE_V0_MAX_PAGES)) {
		ocs_log_test(sli4->os, "num_pages %d not valid\n", rq->num_pages);
		return 0;
	}

	/*
	 * RQE count is the log base 2 of the total number of entries
	 */
	rq->rqe_count = ocs_lg2(qmem->size / SLI4_FCOE_RQE_SIZE);

	if ((buffer_size < SLI4_FCOE_RQ_CREATE_V0_MIN_BUF_SIZE) ||
			(buffer_size > SLI4_FCOE_RQ_CREATE_V0_MAX_BUF_SIZE)) {
		ocs_log_err(sli4->os, "buffer_size %d out of range (%d-%d)\n",
				buffer_size,
				SLI4_FCOE_RQ_CREATE_V0_MIN_BUF_SIZE,
				SLI4_FCOE_RQ_CREATE_V0_MAX_BUF_SIZE);
		return -1;
	}
	rq->buffer_size = buffer_size;

	rq->cq_id = cq_id;

	if (sli4->config.dual_ulp_capable) {
		rq->dua = 1;
		rq->bqu = 1;
		rq->ulp = ulp;
	}

	for (p = 0, addr = qmem->phys;
			p < rq->num_pages;
			p++, addr += SLI_PAGE_SIZE) {
		rq->page_physical_address[p].low  = ocs_addr32_lo(addr);
		rq->page_physical_address[p].high = ocs_addr32_hi(addr);
	}

	return(sli_config_off + sizeof(sli4_req_fcoe_rq_create_t));
}

/**
 * @ingroup sli_fc
 * @brief Write an FCOE_RQ_CREATE_V1 command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param qmem DMA memory for the queue.
 * @param cq_id Associated CQ_ID.
 * @param ulp This parameter carries the ULP for RQ (ignored for V1)
 * @param buffer_size Buffer size pointed to by each RQE.
 *
 * @note This creates a Version 0 message
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_fcoe_rq_create_v1(sli4_t *sli4, void *buf, size_t size,
			  ocs_dma_t *qmem, uint16_t cq_id, uint16_t ulp,
			  uint16_t buffer_size)
{
	sli4_req_fcoe_rq_create_v1_t	*rq = NULL;
	uint32_t	sli_config_off = 0;
	uint32_t	p;
	uintptr_t	addr;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		uint32_t payload_size;

		/* Payload length must accommodate both request and response */
		payload_size = max(sizeof(sli4_req_fcoe_rq_create_v1_t),
				sizeof(sli4_res_common_create_queue_t));

		sli_config_off = sli_cmd_sli_config(sli4, buf, size, payload_size,
				NULL);
	}
	rq = (sli4_req_fcoe_rq_create_v1_t *)((uint8_t *)buf + sli_config_off);

	rq->hdr.opcode = SLI4_OPC_FCOE_RQ_CREATE;
	rq->hdr.subsystem = SLI4_SUBSYSTEM_FCFCOE;
	rq->hdr.request_length = sizeof(sli4_req_fcoe_rq_create_v1_t) -
					sizeof(sli4_req_hdr_t);
	rq->hdr.version = 1;

	/* Disable "no buffer warnings" to avoid Lancer bug */
	rq->dnb = TRUE;

	/* valid values for number of pages: 1-8 (sec 4.5.6) */
	rq->num_pages = sli_page_count(qmem->size, SLI_PAGE_SIZE);
	if (!rq->num_pages || (rq->num_pages > SLI4_FCOE_RQ_CREATE_V1_MAX_PAGES)) {
		ocs_log_test(sli4->os, "num_pages %d not valid, max %d\n",
                rq->num_pages, SLI4_FCOE_RQ_CREATE_V1_MAX_PAGES);
		return 0;
	}

	/*
	 * RQE count is the total number of entries (note not lg2(# entries))
	 */
	rq->rqe_count = qmem->size / SLI4_FCOE_RQE_SIZE;

	rq->rqe_size = SLI4_FCOE_RQE_SIZE_8;

	rq->page_size = SLI4_FCOE_RQ_PAGE_SIZE_4096;

	if ((buffer_size < sli4->config.rq_min_buf_size) ||
	    (buffer_size > sli4->config.rq_max_buf_size)) {
		ocs_log_err(sli4->os, "buffer_size %d out of range (%d-%d)\n",
				buffer_size,
				sli4->config.rq_min_buf_size,
				sli4->config.rq_max_buf_size);
		return -1;
	}
	rq->buffer_size = buffer_size;

	rq->cq_id = cq_id;

	for (p = 0, addr = qmem->phys;
			p < rq->num_pages;
			p++, addr += SLI_PAGE_SIZE) {
		rq->page_physical_address[p].low  = ocs_addr32_lo(addr);
		rq->page_physical_address[p].high = ocs_addr32_hi(addr);
	}

	return(sli_config_off + sizeof(sli4_req_fcoe_rq_create_v1_t));
}

/**
 * @ingroup sli_fc
 * @brief Write an FCOE_RQ_DESTROY command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param rq_id RQ_ID.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_fcoe_rq_destroy(sli4_t *sli4, void *buf, size_t size, uint16_t rq_id)
{
	sli4_req_fcoe_rq_destroy_t	*rq = NULL;
	uint32_t	sli_config_off = 0;

	if (SLI4_PORT_TYPE_FC == sli4->port_type) {
		uint32_t payload_size;

		/* Payload length must accommodate both request and response */
		payload_size = max(sizeof(sli4_req_fcoe_rq_destroy_t),
				sizeof(sli4_res_hdr_t));

		sli_config_off = sli_cmd_sli_config(sli4, buf, size, payload_size,
				NULL);
	}
	rq = (sli4_req_fcoe_rq_destroy_t *)((uint8_t *)buf + sli_config_off);

	rq->hdr.opcode = SLI4_OPC_FCOE_RQ_DESTROY;
	rq->hdr.subsystem = SLI4_SUBSYSTEM_FCFCOE;
	rq->hdr.request_length = sizeof(sli4_req_fcoe_rq_destroy_t) -
					sizeof(sli4_req_hdr_t);

	rq->rq_id = rq_id;

	return(sli_config_off + sizeof(sli4_req_fcoe_rq_destroy_t));
}

/**
 * @ingroup sli_fc
 * @brief Write an FCOE_READ_FCF_TABLE command.
 *
 * @note
 * The response of this command exceeds the size of an embedded
 * command and requires an external buffer with DMA capability to hold the results.
 * The caller should allocate the ocs_dma_t structure / memory.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param dma Pointer to DMA memory structure. This is allocated by the caller.
 * @param index FCF table index to retrieve.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_fcoe_read_fcf_table(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *dma, uint16_t index)
{
	sli4_req_fcoe_read_fcf_table_t *read_fcf = NULL;

	if (SLI4_PORT_TYPE_FC != sli4->port_type) {
		ocs_log_test(sli4->os, "FCOE_READ_FCF_TABLE only supported on FC\n");
		return -1;
	}

	read_fcf = dma->virt;

	ocs_memset(read_fcf, 0, sizeof(sli4_req_fcoe_read_fcf_table_t));

	read_fcf->hdr.opcode = SLI4_OPC_FCOE_READ_FCF_TABLE;
	read_fcf->hdr.subsystem = SLI4_SUBSYSTEM_FCFCOE;
	read_fcf->hdr.request_length = dma->size -
		sizeof(sli4_req_fcoe_read_fcf_table_t);
	read_fcf->fcf_index = index;

	return sli_cmd_sli_config(sli4, buf, size, 0, dma);
}

/**
 * @ingroup sli_fc
 * @brief Write an FCOE_POST_HDR_TEMPLATES command.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the command.
 * @param size Buffer size, in bytes.
 * @param dma Pointer to DMA memory structure. This is allocated by the caller.
 * @param rpi Starting RPI index for the header templates.
 * @param payload_dma Pointer to DMA memory used to hold larger descriptor counts.
 *
 * @return Returns the number of bytes written.
 */
int32_t
sli_cmd_fcoe_post_hdr_templates(sli4_t *sli4, void *buf, size_t size,
		ocs_dma_t *dma, uint16_t rpi, ocs_dma_t *payload_dma)
{
	sli4_req_fcoe_post_hdr_templates_t *template = NULL;
	uint32_t	sli_config_off = 0;
	uintptr_t	phys = 0;
	uint32_t	i = 0;
	uint32_t	page_count;
	uint32_t	payload_size;

	page_count = sli_page_count(dma->size, SLI_PAGE_SIZE);

	payload_size = sizeof(sli4_req_fcoe_post_hdr_templates_t) +
				page_count * sizeof(sli4_physical_page_descriptor_t);

	if (page_count > 16) {
		/* We can't fit more than 16 descriptors into an embedded mailbox
		   command, it has to be non-embedded */
		if (ocs_dma_alloc(sli4->os, payload_dma, payload_size, 4)) {
			ocs_log_err(sli4->os, "mailbox payload memory allocation fail\n");
			return 0;
		}
		sli_config_off = sli_cmd_sli_config(sli4, buf, size, payload_size, payload_dma);
		template = (sli4_req_fcoe_post_hdr_templates_t *)payload_dma->virt;
	} else {
		sli_config_off = sli_cmd_sli_config(sli4, buf, size, payload_size, NULL);
		template = (sli4_req_fcoe_post_hdr_templates_t *)((uint8_t *)buf + sli_config_off);
	}

	if (UINT16_MAX == rpi) {
		rpi = sli4->config.extent[SLI_RSRC_FCOE_RPI].base[0];
	}

	template->hdr.opcode = SLI4_OPC_FCOE_POST_HDR_TEMPLATES;
	template->hdr.subsystem = SLI4_SUBSYSTEM_FCFCOE;
	template->hdr.request_length = sizeof(sli4_req_fcoe_post_hdr_templates_t) -
					sizeof(sli4_req_hdr_t);

	template->rpi_offset = rpi;
	template->page_count = page_count;
	phys = dma->phys;
	for (i = 0; i < template->page_count; i++) {
		template->page_descriptor[i].low  = ocs_addr32_lo(phys);
		template->page_descriptor[i].high = ocs_addr32_hi(phys);

		phys += SLI_PAGE_SIZE;
	}

	return(sli_config_off + payload_size);
}

int32_t
sli_cmd_fcoe_rediscover_fcf(sli4_t *sli4, void *buf, size_t size, uint16_t index)
{
	sli4_req_fcoe_rediscover_fcf_t *redisc = NULL;
	uint32_t	sli_config_off = 0;

	sli_config_off = sli_cmd_sli_config(sli4, buf, size,
			sizeof(sli4_req_fcoe_rediscover_fcf_t),
			NULL);

	redisc = (sli4_req_fcoe_rediscover_fcf_t *)((uint8_t *)buf + sli_config_off);

	redisc->hdr.opcode = SLI4_OPC_FCOE_REDISCOVER_FCF;
	redisc->hdr.subsystem = SLI4_SUBSYSTEM_FCFCOE;
	redisc->hdr.request_length = sizeof(sli4_req_fcoe_rediscover_fcf_t) -
					sizeof(sli4_req_hdr_t);

	if (index == UINT16_MAX) {
		redisc->fcf_count = 0;
	} else {
		redisc->fcf_count = 1;
		redisc->fcf_index[0] = index;
	}

	return(sli_config_off + sizeof(sli4_req_fcoe_rediscover_fcf_t));
}

/**
 * @ingroup sli_fc
 * @brief Write an ABORT_WQE work queue entry.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the WQE.
 * @param size Buffer size, in bytes.
 * @param type Abort type, such as XRI, abort tag, and request tag.
 * @param send_abts Boolean to cause the hardware to automatically generate an ABTS.
 * @param ids ID of IOs to abort.
 * @param mask Mask applied to the ID values to abort.
 * @param tag Tag value associated with this abort.
 * @param cq_id The id of the completion queue where the WQE response is sent.
 * @param dnrx When set to 1, this field indicates that the SLI Port must not return the associated XRI to the SLI
 *             Port's optimized write XRI pool.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_abort_wqe(sli4_t *sli4, void *buf, size_t size, sli4_abort_type_e type, uint32_t send_abts,
	      uint32_t ids, uint32_t mask, uint16_t tag, uint16_t cq_id)
{
	sli4_abort_wqe_t	*abort = buf;

	ocs_memset(buf, 0, size);

	switch (type) {
	case SLI_ABORT_XRI:
		abort->criteria = SLI4_ABORT_CRITERIA_XRI_TAG;
		if (mask) {
			ocs_log_warn(sli4->os, "warning non-zero mask %#x when aborting XRI %#x\n", mask, ids);
			mask = 0;
		}
		break;
	case SLI_ABORT_ABORT_ID:
		abort->criteria = SLI4_ABORT_CRITERIA_ABORT_TAG;
		break;
	case SLI_ABORT_REQUEST_ID:
		abort->criteria = SLI4_ABORT_CRITERIA_REQUEST_TAG;
		break;
	default:
		ocs_log_test(sli4->os, "unsupported type %#x\n", type);
		return -1;
	}

	abort->ia = send_abts ? 0 : 1;

	/* Suppress ABTS retries */
	abort->ir = 1;

	abort->t_mask = mask;
	abort->t_tag  = ids;
	abort->command = SLI4_WQE_ABORT;
	abort->request_tag = tag;
	abort->qosd = TRUE;
	abort->cq_id = cq_id;
	abort->cmd_type = SLI4_CMD_ABORT_WQE;

	return 0;
}

/**
 * @ingroup sli_fc
 * @brief Write an ELS_REQUEST64_WQE work queue entry.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the WQE.
 * @param size Buffer size, in bytes.
 * @param sgl DMA memory for the ELS request.
 * @param req_type ELS request type.
 * @param req_len Length of ELS request in bytes.
 * @param max_rsp_len Max length of ELS response in bytes.
 * @param timeout Time, in seconds, before an IO times out. Zero means 2 * R_A_TOV.
 * @param xri XRI for this exchange.
 * @param tag IO tag value.
 * @param cq_id The id of the completion queue where the WQE response is sent.
 * @param rnode Destination of ELS request (that is, the remote node).
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_els_request64_wqe(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *sgl, uint8_t req_type,
		      uint32_t req_len, uint32_t max_rsp_len, uint8_t timeout,
		      uint16_t xri, uint16_t tag, uint16_t cq_id, ocs_remote_node_t *rnode)
{
	sli4_els_request64_wqe_t	*els = buf;
	sli4_sge_t	*sge = sgl->virt;
	uint8_t		is_fabric = FALSE;

	ocs_memset(buf, 0, size);

	if (sli4->config.sgl_pre_registered) {
		els->xbl = FALSE;

		els->dbde = TRUE;
		els->els_request_payload.bde_type = SLI4_BDE_TYPE_BDE_64;

		els->els_request_payload.buffer_length = req_len;
		els->els_request_payload.u.data.buffer_address_low  = sge[0].buffer_address_low;
		els->els_request_payload.u.data.buffer_address_high = sge[0].buffer_address_high;
	} else {
		els->xbl = TRUE;

		els->els_request_payload.bde_type = SLI4_BDE_TYPE_BLP;

		els->els_request_payload.buffer_length = 2 * sizeof(sli4_sge_t);
		els->els_request_payload.u.blp.sgl_segment_address_low  = ocs_addr32_lo(sgl->phys);
		els->els_request_payload.u.blp.sgl_segment_address_high = ocs_addr32_hi(sgl->phys);
	}

	els->els_request_payload_length = req_len;
	els->max_response_payload_length = max_rsp_len;

	els->xri_tag = xri;
	els->timer = timeout;
	els->class = SLI4_ELS_REQUEST64_CLASS_3;

	els->command = SLI4_WQE_ELS_REQUEST64;

	els->request_tag = tag;

	if (rnode->node_group) {
		els->hlm = TRUE;
		els->remote_id = rnode->fc_id & 0x00ffffff;
	}

	els->iod = SLI4_ELS_REQUEST64_DIR_READ;

	els->qosd = TRUE;

	/* figure out the ELS_ID value from the request buffer */

	switch (req_type) {
	case FC_ELS_CMD_LOGO:
		els->els_id = SLI4_ELS_REQUEST64_LOGO;
		if (rnode->attached) {
			els->ct = SLI4_ELS_REQUEST64_CONTEXT_RPI;
			els->context_tag = rnode->indicator;
		} else {
			els->ct = SLI4_ELS_REQUEST64_CONTEXT_VPI;
			els->context_tag = rnode->sport->indicator;
		}
		if (FC_ADDR_FABRIC == rnode->fc_id) {
			is_fabric = TRUE;
		}
		break;
	case FC_ELS_CMD_FDISC:
		if (FC_ADDR_FABRIC == rnode->fc_id) {
			is_fabric = TRUE;
		}
		if (0 == rnode->sport->fc_id) {
			els->els_id = SLI4_ELS_REQUEST64_FDISC;
			is_fabric = TRUE;
		} else {
			els->els_id = SLI4_ELS_REQUEST64_OTHER;
		}
		els->ct = SLI4_ELS_REQUEST64_CONTEXT_VPI;
		els->context_tag = rnode->sport->indicator;
		els->sp = TRUE;
		break;
	case FC_ELS_CMD_FLOGI:
		els->els_id = SLI4_ELS_REQUEST64_FLOGIN;
		is_fabric = TRUE;
		if (SLI4_IF_TYPE_BE3_SKH_PF == sli4->if_type) {
			if (!rnode->sport->domain) {
				ocs_log_test(sli4->os, "invalid domain handle\n");
				return -1;
			}
			/*
			 * IF_TYPE 0 skips INIT_VFI/INIT_VPI and therefore must use the
			 * FCFI here
			 */
			els->ct = SLI4_ELS_REQUEST64_CONTEXT_FCFI;
			els->context_tag = rnode->sport->domain->fcf_indicator;
			els->sp = TRUE;
		} else {
			els->ct = SLI4_ELS_REQUEST64_CONTEXT_VPI;
			els->context_tag = rnode->sport->indicator;

			/*
			 * Set SP here ... we haven't done a REG_VPI yet
			 * TODO: need to maybe not set this when we have
			 *       completed VFI/VPI registrations ...
			 *
			 * Use the FC_ID of the SPORT if it has been allocated, otherwise
			 * use an S_ID of zero.
			 */
			els->sp = TRUE;
			if (rnode->sport->fc_id != UINT32_MAX) {
				els->sid = rnode->sport->fc_id;
			}
		}
		break;
	case FC_ELS_CMD_PLOGI:
		els->els_id = SLI4_ELS_REQUEST64_PLOGI;
		els->ct = SLI4_ELS_REQUEST64_CONTEXT_VPI;
		els->context_tag = rnode->sport->indicator;
		break;
	case FC_ELS_CMD_SCR:
		els->els_id = SLI4_ELS_REQUEST64_OTHER;
		els->ct = SLI4_ELS_REQUEST64_CONTEXT_VPI;
		els->context_tag = rnode->sport->indicator;
		break;
	default:
		els->els_id = SLI4_ELS_REQUEST64_OTHER;
		if (rnode->attached) {
			els->ct = SLI4_ELS_REQUEST64_CONTEXT_RPI;
			els->context_tag = rnode->indicator;
		} else {
			els->ct = SLI4_ELS_REQUEST64_CONTEXT_VPI;
			els->context_tag = rnode->sport->indicator;
		}
		break;
	}

	if (is_fabric) {
		els->cmd_type = SLI4_ELS_REQUEST64_CMD_FABRIC;
	} else {
		els->cmd_type = SLI4_ELS_REQUEST64_CMD_NON_FABRIC;
	}

	els->cq_id = cq_id;

	if (SLI4_ELS_REQUEST64_CONTEXT_RPI != els->ct) {
		els->remote_id = rnode->fc_id;
	}
	if (SLI4_ELS_REQUEST64_CONTEXT_VPI == els->ct) {
		els->temporary_rpi = rnode->indicator;
	}

	return 0;
}


/**
 * @ingroup sli_fc
 * @brief Write an FCP_ICMND64_WQE work queue entry.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the WQE.
 * @param size Buffer size, in bytes.
 * @param sgl DMA memory for the scatter gather list.
 * @param xri XRI for this exchange.
 * @param tag IO tag value.
 * @param cq_id The id of the completion queue where the WQE response is sent.
 * @param rpi remote node indicator (RPI)
 * @param rnode Destination request (that is, the remote node).
 * @param timeout Time, in seconds, before an IO times out. Zero means no timeout.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_fcp_icmnd64_wqe(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *sgl,
		    uint16_t xri, uint16_t tag, uint16_t cq_id,
		    uint32_t rpi, ocs_remote_node_t *rnode, uint8_t timeout)
{
	sli4_fcp_icmnd64_wqe_t *icmnd = buf;
	sli4_sge_t	*sge = NULL;

	ocs_memset(buf, 0, size);

	if (!sgl || !sgl->virt) {
		ocs_log_err(sli4->os, "bad parameter sgl=%p virt=%p\n",
			    sgl, sgl ? sgl->virt : NULL);
		return -1;
	}
	sge = sgl->virt;

	if (sli4->config.sgl_pre_registered) {
		icmnd->xbl = FALSE;

		icmnd->dbde = TRUE;
		icmnd->bde.bde_type = SLI4_BDE_TYPE_BDE_64;

		icmnd->bde.buffer_length = sge[0].buffer_length;
		icmnd->bde.u.data.buffer_address_low  = sge[0].buffer_address_low;
		icmnd->bde.u.data.buffer_address_high = sge[0].buffer_address_high;
	} else {
		icmnd->xbl = TRUE;

		icmnd->bde.bde_type = SLI4_BDE_TYPE_BLP;

		icmnd->bde.buffer_length = sgl->size;
		icmnd->bde.u.blp.sgl_segment_address_low  = ocs_addr32_lo(sgl->phys);
		icmnd->bde.u.blp.sgl_segment_address_high = ocs_addr32_hi(sgl->phys);
	}

	icmnd->payload_offset_length = sge[0].buffer_length + sge[1].buffer_length;
	icmnd->xri_tag = xri;
	icmnd->context_tag = rpi;
	icmnd->timer = timeout;

	icmnd->pu = 2;	/* WQE word 4 contains read transfer length */
	icmnd->class = SLI4_ELS_REQUEST64_CLASS_3;
	icmnd->command = SLI4_WQE_FCP_ICMND64;
	icmnd->ct = SLI4_ELS_REQUEST64_CONTEXT_RPI;

	icmnd->abort_tag = xri;

	icmnd->request_tag = tag;
	icmnd->len_loc = 3;
	if (rnode->node_group) {
		icmnd->hlm = TRUE;
		icmnd->remote_n_port_id = rnode->fc_id & 0x00ffffff;
	}
	if (((ocs_node_t *)rnode->node)->fcp2device) {
		icmnd->erp = TRUE;
	}
	icmnd->cmd_type = SLI4_CMD_FCP_ICMND64_WQE;
	icmnd->cq_id = cq_id;

	return  0;
}

/**
 * @ingroup sli_fc
 * @brief Write an FCP_IREAD64_WQE work queue entry.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the WQE.
 * @param size Buffer size, in bytes.
 * @param sgl DMA memory for the scatter gather list.
 * @param first_data_sge Index of first data sge (used if perf hints are enabled)
 * @param xfer_len Data transfer length.
 * @param xri XRI for this exchange.
 * @param tag IO tag value.
 * @param cq_id The id of the completion queue where the WQE response is sent.
 * @param rpi remote node indicator (RPI)
 * @param rnode Destination request (i.e. remote node).
 * @param dif T10 DIF operation, or 0 to disable.
 * @param bs T10 DIF block size, or 0 if DIF is disabled.
 * @param timeout Time, in seconds, before an IO times out. Zero means no timeout.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_fcp_iread64_wqe(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *sgl, uint32_t first_data_sge,
		    uint32_t xfer_len, uint16_t xri, uint16_t tag, uint16_t cq_id,
		    uint32_t rpi, ocs_remote_node_t *rnode,
		    uint8_t dif, uint8_t bs, uint8_t timeout)
{
	sli4_fcp_iread64_wqe_t *iread = buf;
	sli4_sge_t	*sge = NULL;

	ocs_memset(buf, 0, size);

	if (!sgl || !sgl->virt) {
		ocs_log_err(sli4->os, "bad parameter sgl=%p virt=%p\n",
			    sgl, sgl ? sgl->virt : NULL);
		return -1;
	}
	sge = sgl->virt;

	if (sli4->config.sgl_pre_registered) {
		iread->xbl = FALSE;

		iread->dbde = TRUE;
		iread->bde.bde_type = SLI4_BDE_TYPE_BDE_64;

		iread->bde.buffer_length = sge[0].buffer_length;
		iread->bde.u.data.buffer_address_low  = sge[0].buffer_address_low;
		iread->bde.u.data.buffer_address_high = sge[0].buffer_address_high;
	} else {
		iread->xbl = TRUE;

		iread->bde.bde_type = SLI4_BDE_TYPE_BLP;

		iread->bde.buffer_length = sgl->size;
		iread->bde.u.blp.sgl_segment_address_low  = ocs_addr32_lo(sgl->phys);
		iread->bde.u.blp.sgl_segment_address_high = ocs_addr32_hi(sgl->phys);

		/* fill out fcp_cmnd buffer len and change resp buffer to be of type
		 * "skip" (note: response will still be written to sge[1] if necessary) */
		iread->fcp_cmd_buffer_length = sge[0].buffer_length;
		sge[1].sge_type = SLI4_SGE_TYPE_SKIP;
	}

	iread->payload_offset_length = sge[0].buffer_length + sge[1].buffer_length;
	iread->total_transfer_length = xfer_len;

	iread->xri_tag = xri;
	iread->context_tag = rpi;

	iread->timer = timeout;

	iread->pu = 2;	/* WQE word 4 contains read transfer length */
	iread->class = SLI4_ELS_REQUEST64_CLASS_3;
	iread->command = SLI4_WQE_FCP_IREAD64;
	iread->ct = SLI4_ELS_REQUEST64_CONTEXT_RPI;
	iread->dif = dif;
	iread->bs  = bs;

	iread->abort_tag = xri;

	iread->request_tag = tag;
	iread->len_loc = 3;
	if (rnode->node_group) {
		iread->hlm = TRUE;
		iread->remote_n_port_id = rnode->fc_id & 0x00ffffff;
	}
	if (((ocs_node_t *)rnode->node)->fcp2device) {
		iread->erp = TRUE;
	}
	iread->iod = 1;
	iread->cmd_type = SLI4_CMD_FCP_IREAD64_WQE;
	iread->cq_id = cq_id;

	if (sli4->config.perf_hint) {
		iread->first_data_bde.bde_type = SLI4_BDE_TYPE_BDE_64;
		iread->first_data_bde.buffer_length = sge[first_data_sge].buffer_length;
		iread->first_data_bde.u.data.buffer_address_low  = sge[first_data_sge].buffer_address_low;
		iread->first_data_bde.u.data.buffer_address_high = sge[first_data_sge].buffer_address_high;
	}

	return  0;
}


/**
 * @ingroup sli_fc
 * @brief Write an FCP_IWRITE64_WQE work queue entry.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the WQE.
 * @param size Buffer size, in bytes.
 * @param sgl DMA memory for the scatter gather list.
 * @param first_data_sge Index of first data sge (used if perf hints are enabled)
 * @param xfer_len Data transfer length.
 * @param first_burst The number of first burst bytes
 * @param xri XRI for this exchange.
 * @param tag IO tag value.
 * @param cq_id The id of the completion queue where the WQE response is sent.
 * @param rpi remote node indicator (RPI)
 * @param rnode Destination request (i.e. remote node)
 * @param dif T10 DIF operation, or 0 to disable
 * @param bs T10 DIF block size, or 0 if DIF is disabled
 * @param timeout Time, in seconds, before an IO times out. Zero means no timeout.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_fcp_iwrite64_wqe(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *sgl, uint32_t first_data_sge,
		     uint32_t xfer_len, uint32_t first_burst, uint16_t xri, uint16_t tag, uint16_t cq_id,
		     uint32_t rpi, ocs_remote_node_t *rnode,
		     uint8_t dif, uint8_t bs, uint8_t timeout)
{
	sli4_fcp_iwrite64_wqe_t *iwrite = buf;
	sli4_sge_t	*sge = NULL;

	ocs_memset(buf, 0, size);

	if (!sgl || !sgl->virt) {
		ocs_log_err(sli4->os, "bad parameter sgl=%p virt=%p\n",
			    sgl, sgl ? sgl->virt : NULL);
		return -1;
	}
	sge = sgl->virt;

	if (sli4->config.sgl_pre_registered) {
		iwrite->xbl = FALSE;

		iwrite->dbde = TRUE;
		iwrite->bde.bde_type = SLI4_BDE_TYPE_BDE_64;

		iwrite->bde.buffer_length = sge[0].buffer_length;
		iwrite->bde.u.data.buffer_address_low  = sge[0].buffer_address_low;
		iwrite->bde.u.data.buffer_address_high = sge[0].buffer_address_high;
	} else {
		iwrite->xbl = TRUE;

		iwrite->bde.bde_type = SLI4_BDE_TYPE_BLP;

		iwrite->bde.buffer_length = sgl->size;
		iwrite->bde.u.blp.sgl_segment_address_low  = ocs_addr32_lo(sgl->phys);
		iwrite->bde.u.blp.sgl_segment_address_high = ocs_addr32_hi(sgl->phys);

		/* fill out fcp_cmnd buffer len and change resp buffer to be of type
		 * "skip" (note: response will still be written to sge[1] if necessary) */
		iwrite->fcp_cmd_buffer_length = sge[0].buffer_length;
		sge[1].sge_type = SLI4_SGE_TYPE_SKIP;
	}

	iwrite->payload_offset_length = sge[0].buffer_length + sge[1].buffer_length;
	iwrite->total_transfer_length = xfer_len;
	iwrite->initial_transfer_length = MIN(xfer_len, first_burst);

	iwrite->xri_tag = xri;
	iwrite->context_tag = rpi;

	iwrite->timer = timeout;

	iwrite->pu = 2;	/* WQE word 4 contains read transfer length */
	iwrite->class = SLI4_ELS_REQUEST64_CLASS_3;
	iwrite->command = SLI4_WQE_FCP_IWRITE64;
	iwrite->ct = SLI4_ELS_REQUEST64_CONTEXT_RPI;
	iwrite->dif = dif;
	iwrite->bs  = bs;

	iwrite->abort_tag = xri;

	iwrite->request_tag = tag;
	iwrite->len_loc = 3;
	if (rnode->node_group) {
		iwrite->hlm = TRUE;
		iwrite->remote_n_port_id = rnode->fc_id & 0x00ffffff;
	}
	if (((ocs_node_t *)rnode->node)->fcp2device) {
		iwrite->erp = TRUE;
	}
	iwrite->cmd_type = SLI4_CMD_FCP_IWRITE64_WQE;
	iwrite->cq_id = cq_id;

	if (sli4->config.perf_hint) {
		iwrite->first_data_bde.bde_type = SLI4_BDE_TYPE_BDE_64;
		iwrite->first_data_bde.buffer_length = sge[first_data_sge].buffer_length;
		iwrite->first_data_bde.u.data.buffer_address_low  = sge[first_data_sge].buffer_address_low;
		iwrite->first_data_bde.u.data.buffer_address_high = sge[first_data_sge].buffer_address_high;
	}

	return  0;
}

/**
 * @ingroup sli_fc
 * @brief Write an FCP_TRECEIVE64_WQE work queue entry.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the WQE.
 * @param size Buffer size, in bytes.
 * @param sgl DMA memory for the Scatter-Gather List.
 * @param first_data_sge Index of first data sge (used if perf hints are enabled)
 * @param relative_off Relative offset of the IO (if any).
 * @param xfer_len Data transfer length.
 * @param xri XRI for this exchange.
 * @param tag IO tag value.
 * @param xid OX_ID for the exchange.
 * @param cq_id The id of the completion queue where the WQE response is sent.
 * @param rpi remote node indicator (RPI)
 * @param rnode Destination request (i.e. remote node).
 * @param flags Optional attributes, including:
 *  - ACTIVE - IO is already active.
 *  - AUTO RSP - Automatically generate a good FCP_RSP.
 * @param dif T10 DIF operation, or 0 to disable.
 * @param bs T10 DIF block size, or 0 if DIF is disabled.
 * @param csctl value of csctl field.
 * @param app_id value for VM application header.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_fcp_treceive64_wqe(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *sgl, uint32_t first_data_sge,
		       uint32_t relative_off, uint32_t xfer_len, uint16_t xri, uint16_t tag, uint16_t cq_id,
		       uint16_t xid, uint32_t rpi, ocs_remote_node_t *rnode, uint32_t flags, uint8_t dif, uint8_t bs,
		       uint8_t csctl, uint32_t app_id)
{
	sli4_fcp_treceive64_wqe_t *trecv = buf;
	sli4_fcp_128byte_wqe_t *trecv_128 = buf;
	sli4_sge_t	*sge = NULL;

	ocs_memset(buf, 0, size);

	if (!sgl || !sgl->virt) {
		ocs_log_err(sli4->os, "bad parameter sgl=%p virt=%p\n",
			    sgl, sgl ? sgl->virt : NULL);
		return -1;
	}
	sge = sgl->virt;

	if (sli4->config.sgl_pre_registered) {
		trecv->xbl = FALSE;

		trecv->dbde = TRUE;
		trecv->bde.bde_type = SLI4_BDE_TYPE_BDE_64;

		trecv->bde.buffer_length = sge[0].buffer_length;
		trecv->bde.u.data.buffer_address_low  = sge[0].buffer_address_low;
		trecv->bde.u.data.buffer_address_high = sge[0].buffer_address_high;

		trecv->payload_offset_length = sge[0].buffer_length;
	} else {
		trecv->xbl = TRUE;

		/* if data is a single physical address, use a BDE */
		if (!dif && (xfer_len <= sge[2].buffer_length)) {
			trecv->dbde = TRUE;
			trecv->bde.bde_type = SLI4_BDE_TYPE_BDE_64;

			trecv->bde.buffer_length = sge[2].buffer_length;
			trecv->bde.u.data.buffer_address_low  = sge[2].buffer_address_low;
			trecv->bde.u.data.buffer_address_high = sge[2].buffer_address_high;
		} else {
			trecv->bde.bde_type = SLI4_BDE_TYPE_BLP;
			trecv->bde.buffer_length = sgl->size;
			trecv->bde.u.blp.sgl_segment_address_low  = ocs_addr32_lo(sgl->phys);
			trecv->bde.u.blp.sgl_segment_address_high = ocs_addr32_hi(sgl->phys);
		}
	}

	trecv->relative_offset = relative_off;

	if (flags & SLI4_IO_CONTINUATION) {
		trecv->xc = TRUE;
	}
	trecv->xri_tag = xri;

	trecv->context_tag = rpi;

	trecv->pu = TRUE;	/* WQE uses relative offset */

	if (flags & SLI4_IO_AUTO_GOOD_RESPONSE) {
		trecv->ar = TRUE;
	}

	trecv->command = SLI4_WQE_FCP_TRECEIVE64;
	trecv->class = SLI4_ELS_REQUEST64_CLASS_3;
	trecv->ct = SLI4_ELS_REQUEST64_CONTEXT_RPI;
	trecv->dif = dif;
	trecv->bs  = bs;

	trecv->remote_xid = xid;

	trecv->request_tag = tag;

	trecv->iod = 1;

	trecv->len_loc = 0x2;

	if (rnode->node_group) {
		trecv->hlm = TRUE;
		trecv->dword5.dword = rnode->fc_id & 0x00ffffff;
	}

	trecv->cmd_type = SLI4_CMD_FCP_TRECEIVE64_WQE;

	trecv->cq_id = cq_id;

	trecv->fcp_data_receive_length = xfer_len;

	if (sli4->config.perf_hint) {
		trecv->first_data_bde.bde_type = SLI4_BDE_TYPE_BDE_64;
		trecv->first_data_bde.buffer_length = sge[first_data_sge].buffer_length;
		trecv->first_data_bde.u.data.buffer_address_low  = sge[first_data_sge].buffer_address_low;
		trecv->first_data_bde.u.data.buffer_address_high = sge[first_data_sge].buffer_address_high;
	}

	/* The upper 7 bits of csctl is the priority */
	if (csctl & SLI4_MASK_CCP) {
		trecv->ccpe = 1;
		trecv->ccp = (csctl & SLI4_MASK_CCP);
	}

	if (app_id && (sli4->config.wqe_size == SLI4_WQE_EXT_BYTES) && !trecv->eat) {
		trecv->app_id_valid = 1;
		trecv->wqes = 1;
		trecv_128->dw[31] = app_id;
	}
	return 0;
}

/**
 * @ingroup sli_fc
 * @brief Write an FCP_CONT_TRECEIVE64_WQE work queue entry.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the WQE.
 * @param size Buffer size, in bytes.
 * @param sgl DMA memory for the Scatter-Gather List.
 * @param first_data_sge Index of first data sge (used if perf hints are enabled)
 * @param relative_off Relative offset of the IO (if any).
 * @param xfer_len Data transfer length.
 * @param xri XRI for this exchange.
 * @param sec_xri Secondary XRI for this exchange. (BZ 161832 workaround)
 * @param tag IO tag value.
 * @param xid OX_ID for the exchange.
 * @param cq_id The id of the completion queue where the WQE response is sent.
 * @param rpi remote node indicator (RPI)
 * @param rnode Destination request (i.e. remote node).
 * @param flags Optional attributes, including:
 *  - ACTIVE - IO is already active.
 *  - AUTO RSP - Automatically generate a good FCP_RSP.
 * @param dif T10 DIF operation, or 0 to disable.
 * @param bs T10 DIF block size, or 0 if DIF is disabled.
 * @param csctl value of csctl field.
 * @param app_id value for VM application header.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_fcp_cont_treceive64_wqe(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *sgl, uint32_t first_data_sge,
			uint32_t relative_off, uint32_t xfer_len, uint16_t xri, uint16_t sec_xri, uint16_t tag,
			uint16_t cq_id, uint16_t xid, uint32_t rpi, ocs_remote_node_t *rnode, uint32_t flags,
			uint8_t dif, uint8_t bs, uint8_t csctl, uint32_t app_id)
{
	int32_t rc;

	rc = sli_fcp_treceive64_wqe(sli4, buf, size, sgl, first_data_sge, relative_off, xfer_len, xri, tag,
			cq_id, xid, rpi, rnode, flags, dif, bs, csctl, app_id);
	if (rc == 0) {
		sli4_fcp_treceive64_wqe_t *trecv = buf;

		trecv->command = SLI4_WQE_FCP_CONT_TRECEIVE64;
		trecv->dword5.sec_xri_tag = sec_xri;
	}
	return rc;
}

/**
 * @ingroup sli_fc
 * @brief Write an FCP_TRSP64_WQE work queue entry.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the WQE.
 * @param size Buffer size, in bytes.
 * @param sgl DMA memory for the Scatter-Gather List.
 * @param rsp_len Response data length.
 * @param xri XRI for this exchange.
 * @param tag IO tag value.
 * @param cq_id The id of the completion queue where the WQE response is sent.
 * @param xid OX_ID for the exchange.
 * @param rpi remote node indicator (RPI)
 * @param rnode Destination request (i.e. remote node).
 * @param flags Optional attributes, including:
 *  - ACTIVE - IO is already active
 *  - AUTO RSP - Automatically generate a good FCP_RSP.
 * @param csctl value of csctl field.
 * @param port_owned 0/1 to indicate if the XRI is port owned (used to set XBL=0)
 * @param app_id value for VM application header.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_fcp_trsp64_wqe(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *sgl, uint32_t rsp_len,
		   uint16_t xri, uint16_t tag, uint16_t cq_id, uint16_t xid, uint32_t rpi, ocs_remote_node_t *rnode,
		   uint32_t flags, uint8_t csctl, uint8_t port_owned, uint32_t app_id)
{
	sli4_fcp_trsp64_wqe_t *trsp = buf;
	sli4_fcp_128byte_wqe_t *trsp_128 = buf;

	ocs_memset(buf, 0, size);

	if (flags & SLI4_IO_AUTO_GOOD_RESPONSE) {
		trsp->ag = TRUE;
		/*
		 * The SLI-4 documentation states that the BDE is ignored when
		 * using auto-good response, but, at least for IF_TYPE 0 devices,
		 * this does not appear to be true.
		 */
		if (SLI4_IF_TYPE_BE3_SKH_PF == sli4->if_type) {
			trsp->bde.buffer_length = 12;	/* byte size of RSP */
		}
	} else {
		sli4_sge_t	*sge = sgl->virt;

		if (sli4->config.sgl_pre_registered || port_owned) {
			trsp->dbde = TRUE;
		} else {
			trsp->xbl = TRUE;
		}

		trsp->bde.bde_type = SLI4_BDE_TYPE_BDE_64;
		trsp->bde.buffer_length = sge[0].buffer_length;
		trsp->bde.u.data.buffer_address_low  = sge[0].buffer_address_low;
		trsp->bde.u.data.buffer_address_high = sge[0].buffer_address_high;

		trsp->fcp_response_length = rsp_len;
	}

	if (flags & SLI4_IO_CONTINUATION) {
		trsp->xc = TRUE;
	}

	if (rnode->node_group) {
		trsp->hlm = TRUE;
		trsp->dword5 = rnode->fc_id & 0x00ffffff;
	}

	trsp->xri_tag = xri;
	trsp->rpi = rpi;

	trsp->command = SLI4_WQE_FCP_TRSP64;
	trsp->class = SLI4_ELS_REQUEST64_CLASS_3;

	trsp->remote_xid = xid;
	trsp->request_tag = tag;
	trsp->dnrx = ((flags & SLI4_IO_DNRX) == 0 ? 0 : 1);
	trsp->len_loc = 0x1;
	trsp->cq_id = cq_id;
	trsp->cmd_type = SLI4_CMD_FCP_TRSP64_WQE;

	/* The upper 7 bits of csctl is the priority */
	if (csctl & SLI4_MASK_CCP) {
		trsp->ccpe = 1;
		trsp->ccp = (csctl & SLI4_MASK_CCP);
	}

	if (app_id && (sli4->config.wqe_size == SLI4_WQE_EXT_BYTES) && !trsp->eat) {
		trsp->app_id_valid = 1;
		trsp->wqes = 1;
		trsp_128->dw[31] = app_id;
	}
	return 0;
}

/**
 * @ingroup sli_fc
 * @brief Write an FCP_TSEND64_WQE work queue entry.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the WQE.
 * @param size Buffer size, in bytes.
 * @param sgl DMA memory for the scatter gather list.
 * @param first_data_sge Index of first data sge (used if perf hints are enabled)
 * @param relative_off Relative offset of the IO (if any).
 * @param xfer_len Data transfer length.
 * @param xri XRI for this exchange.
 * @param tag IO tag value.
 * @param cq_id The id of the completion queue where the WQE response is sent.
 * @param xid OX_ID for the exchange.
 * @param rpi remote node indicator (RPI)
 * @param rnode Destination request (i.e. remote node).
 * @param flags Optional attributes, including:
 *  - ACTIVE - IO is already active.
 *  - AUTO RSP - Automatically generate a good FCP_RSP.
 * @param dif T10 DIF operation, or 0 to disable.
 * @param bs T10 DIF block size, or 0 if DIF is disabled.
 * @param csctl value of csctl field.
 * @param app_id value for VM application header.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_fcp_tsend64_wqe(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *sgl, uint32_t first_data_sge,
		    uint32_t relative_off, uint32_t xfer_len,
		    uint16_t xri, uint16_t tag, uint16_t cq_id, uint16_t xid, uint32_t rpi, ocs_remote_node_t *rnode,
		    uint32_t flags, uint8_t dif, uint8_t bs, uint8_t csctl, uint32_t app_id)
{
	sli4_fcp_tsend64_wqe_t *tsend = buf;
	sli4_fcp_128byte_wqe_t *tsend_128 = buf;
	sli4_sge_t	*sge = NULL;

	ocs_memset(buf, 0, size);

	if (!sgl || !sgl->virt) {
		ocs_log_err(sli4->os, "bad parameter sgl=%p virt=%p\n",
			    sgl, sgl ? sgl->virt : NULL);
		return -1;
	}
	sge = sgl->virt;

	if (sli4->config.sgl_pre_registered) {
		tsend->xbl = FALSE;

		tsend->dbde = TRUE;
		tsend->bde.bde_type = SLI4_BDE_TYPE_BDE_64;

		/* TSEND64_WQE specifies first two SGE are skipped
		 * (i.e. 3rd is valid) */
		tsend->bde.buffer_length = sge[2].buffer_length;
		tsend->bde.u.data.buffer_address_low  = sge[2].buffer_address_low;
		tsend->bde.u.data.buffer_address_high = sge[2].buffer_address_high;
	} else {
		tsend->xbl = TRUE;

		/* if data is a single physical address, use a BDE */
		if (!dif && (xfer_len <= sge[2].buffer_length)) {
			tsend->dbde = TRUE;
			tsend->bde.bde_type = SLI4_BDE_TYPE_BDE_64;
			/* TSEND64_WQE specifies first two SGE are skipped
			 * (i.e. 3rd is valid) */
			tsend->bde.buffer_length = sge[2].buffer_length;
			tsend->bde.u.data.buffer_address_low  = sge[2].buffer_address_low;
			tsend->bde.u.data.buffer_address_high = sge[2].buffer_address_high;
		} else {
			tsend->bde.bde_type = SLI4_BDE_TYPE_BLP;
			tsend->bde.buffer_length = sgl->size;
			tsend->bde.u.blp.sgl_segment_address_low  = ocs_addr32_lo(sgl->phys);
			tsend->bde.u.blp.sgl_segment_address_high = ocs_addr32_hi(sgl->phys);
		}
	}

	tsend->relative_offset = relative_off;

	if (flags & SLI4_IO_CONTINUATION) {
		tsend->xc = TRUE;
	}
	tsend->xri_tag = xri;

	tsend->rpi = rpi;

	tsend->pu = TRUE;	/* WQE uses relative offset */

	if (flags & SLI4_IO_AUTO_GOOD_RESPONSE) {
		tsend->ar = TRUE;
	}

	tsend->command = SLI4_WQE_FCP_TSEND64;
	tsend->class = SLI4_ELS_REQUEST64_CLASS_3;
	tsend->ct = SLI4_ELS_REQUEST64_CONTEXT_RPI;
	tsend->dif = dif;
	tsend->bs  = bs;

	tsend->remote_xid = xid;

	tsend->request_tag = tag;

	tsend->len_loc = 0x2;

	if (rnode->node_group) {
		tsend->hlm = TRUE;
		tsend->dword5 = rnode->fc_id & 0x00ffffff;
	}

	tsend->cq_id = cq_id;

	tsend->cmd_type = SLI4_CMD_FCP_TSEND64_WQE;

	tsend->fcp_data_transmit_length = xfer_len;

	if (sli4->config.perf_hint) {
		tsend->first_data_bde.bde_type = SLI4_BDE_TYPE_BDE_64;
		tsend->first_data_bde.buffer_length = sge[first_data_sge].buffer_length;
		tsend->first_data_bde.u.data.buffer_address_low  = sge[first_data_sge].buffer_address_low;
		tsend->first_data_bde.u.data.buffer_address_high = sge[first_data_sge].buffer_address_high;
	}

	/* The upper 7 bits of csctl is the priority */
	if (csctl & SLI4_MASK_CCP) {
		tsend->ccpe = 1;
		tsend->ccp = (csctl & SLI4_MASK_CCP);
	}

	if (app_id && (sli4->config.wqe_size == SLI4_WQE_EXT_BYTES) && !tsend->eat) {
		tsend->app_id_valid = 1;
		tsend->wqes = 1;
		tsend_128->dw[31] = app_id;
	}
	return 0;
}

/**
 * @ingroup sli_fc
 * @brief Write a GEN_REQUEST64 work queue entry.
 *
 * @note This WQE is only used to send FC-CT commands.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the WQE.
 * @param size Buffer size, in bytes.
 * @param sgl DMA memory for the request.
 * @param req_len Length of request.
 * @param max_rsp_len Max length of response.
 * @param timeout Time, in seconds, before an IO times out. Zero means infinite.
 * @param xri XRI for this exchange.
 * @param tag IO tag value.
 * @param cq_id The id of the completion queue where the WQE response is sent.
 * @param rnode Destination of request (that is, the remote node).
 * @param r_ctl R_CTL value for sequence.
 * @param type TYPE value for sequence.
 * @param df_ctl DF_CTL value for sequence.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_gen_request64_wqe(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *sgl,
		      uint32_t req_len, uint32_t max_rsp_len, uint8_t timeout,
		      uint16_t xri, uint16_t tag, uint16_t cq_id, ocs_remote_node_t *rnode,
		      uint8_t r_ctl, uint8_t type, uint8_t df_ctl)
{
	sli4_gen_request64_wqe_t	*gen = buf;
	sli4_sge_t	*sge = NULL;

	ocs_memset(buf, 0, size);

	if (!sgl || !sgl->virt) {
		ocs_log_err(sli4->os, "bad parameter sgl=%p virt=%p\n",
			    sgl, sgl ? sgl->virt : NULL);
		return -1;
	}
	sge = sgl->virt;

	if (sli4->config.sgl_pre_registered) {
		gen->xbl = FALSE;

		gen->dbde = TRUE;
		gen->bde.bde_type = SLI4_BDE_TYPE_BDE_64;

		gen->bde.buffer_length = req_len;
		gen->bde.u.data.buffer_address_low  = sge[0].buffer_address_low;
		gen->bde.u.data.buffer_address_high = sge[0].buffer_address_high;
	} else {
		gen->xbl = TRUE;

		gen->bde.bde_type = SLI4_BDE_TYPE_BLP;

		gen->bde.buffer_length = 2 * sizeof(sli4_sge_t);
		gen->bde.u.blp.sgl_segment_address_low  = ocs_addr32_lo(sgl->phys);
		gen->bde.u.blp.sgl_segment_address_high = ocs_addr32_hi(sgl->phys);
	}

	gen->request_payload_length = req_len;
	gen->max_response_payload_length = max_rsp_len;

	gen->df_ctl = df_ctl;
	gen->type = type;
	gen->r_ctl = r_ctl;

	gen->xri_tag = xri;

	gen->ct = SLI4_ELS_REQUEST64_CONTEXT_RPI;
	gen->context_tag = rnode->indicator;

	gen->class = SLI4_ELS_REQUEST64_CLASS_3;

	gen->command = SLI4_WQE_GEN_REQUEST64;

	gen->timer = timeout;

	gen->request_tag = tag;

	gen->iod = SLI4_ELS_REQUEST64_DIR_READ;

	gen->qosd = TRUE;

	if (rnode->node_group) {
		gen->hlm = TRUE;
		gen->remote_n_port_id = rnode->fc_id & 0x00ffffff;
	}

	gen->cmd_type = SLI4_CMD_GEN_REQUEST64_WQE;

	gen->cq_id = cq_id;

	return 0;
}

/**
 * @ingroup sli_fc
 * @brief Write a SEND_FRAME work queue entry
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the WQE.
 * @param size Buffer size, in bytes.
 * @param sof Start of frame value
 * @param eof End of frame value
 * @param hdr Pointer to FC header data
 * @param payload DMA memory for the payload.
 * @param req_len Length of payload.
 * @param timeout Time, in seconds, before an IO times out. Zero means infinite.
 * @param xri XRI for this exchange.
 * @param req_tag IO tag value.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_send_frame_wqe(sli4_t *sli4, void *buf, size_t size, uint8_t sof, uint8_t eof, uint32_t *hdr,
		   ocs_dma_t *payload, uint32_t req_len, uint8_t timeout,
		   uint16_t xri, uint16_t req_tag)
{
	sli4_send_frame_wqe_t *sf = buf;

	ocs_memset(buf, 0, size);

	sf->dbde = TRUE;
	sf->bde.buffer_length = req_len;
	sf->bde.u.data.buffer_address_low = ocs_addr32_lo(payload->phys);
	sf->bde.u.data.buffer_address_high = ocs_addr32_hi(payload->phys);

	/* Copy FC header */
	sf->fc_header_0_1[0] = hdr[0];
	sf->fc_header_0_1[1] = hdr[1];
	sf->fc_header_2_5[0] = hdr[2];
	sf->fc_header_2_5[1] = hdr[3];
	sf->fc_header_2_5[2] = hdr[4];
	sf->fc_header_2_5[3] = hdr[5];

	sf->frame_length = req_len;

	sf->xri_tag = xri;
	sf->pu = 0;
	sf->context_tag = 0;


	sf->ct = 0;
	sf->command = SLI4_WQE_SEND_FRAME;
	sf->class = SLI4_ELS_REQUEST64_CLASS_3;
	sf->timer = timeout;

	sf->request_tag = req_tag;
	sf->eof = eof;
	sf->sof = sof;

	sf->qosd = 0;
	sf->lenloc = 1;
	sf->xc = 0;

	sf->xbl = 1;

	sf->cmd_type = SLI4_CMD_SEND_FRAME_WQE;
	sf->cq_id = 0xffff;

	return 0;
}

/**
 * @ingroup sli_fc
 * @brief Write a XMIT_SEQUENCE64 work queue entry.
 *
 * This WQE is used to send FC-CT response frames.
 *
 * @note This API implements a restricted use for this WQE, a TODO: would
 * include passing in sequence initiative, and full SGL's
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the WQE.
 * @param size Buffer size, in bytes.
 * @param payload DMA memory for the request.
 * @param payload_len Length of request.
 * @param timeout Time, in seconds, before an IO times out. Zero means infinite.
 * @param ox_id originator exchange ID
 * @param xri XRI for this exchange.
 * @param tag IO tag value.
 * @param rnode Destination of request (that is, the remote node).
 * @param r_ctl R_CTL value for sequence.
 * @param type TYPE value for sequence.
 * @param df_ctl DF_CTL value for sequence.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_xmit_sequence64_wqe(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *payload,
		      uint32_t payload_len, uint8_t timeout, uint16_t ox_id,
		      uint16_t xri, uint16_t tag, ocs_remote_node_t *rnode,
		      uint8_t r_ctl, uint8_t type, uint8_t df_ctl)
{
	sli4_xmit_sequence64_wqe_t	*xmit = buf;

	ocs_memset(buf, 0, size);

	if ((payload == NULL) || (payload->virt == NULL)) {
		ocs_log_err(sli4->os, "bad parameter sgl=%p virt=%p\n",
			    payload, payload ? payload->virt : NULL);
		return -1;
	}

	if (sli4->config.sgl_pre_registered) {
		xmit->dbde = TRUE;
	} else {
		xmit->xbl = TRUE;
	}

	xmit->bde.bde_type = SLI4_BDE_TYPE_BDE_64;
	xmit->bde.buffer_length = payload_len;
	xmit->bde.u.data.buffer_address_low  = ocs_addr32_lo(payload->phys);
	xmit->bde.u.data.buffer_address_high = ocs_addr32_hi(payload->phys);
	xmit->sequence_payload_len = payload_len;

	xmit->remote_n_port_id = rnode->fc_id & 0x00ffffff;

	xmit->relative_offset = 0;

	xmit->si = 0;			/* sequence initiative - this matches what is seen from
					 * FC switches in response to FCGS commands */
	xmit->ft = 0;			/* force transmit */
	xmit->xo = 0;			/* exchange responder */
	xmit->ls = 1;			/* last in seqence */
	xmit->df_ctl = df_ctl;
	xmit->type = type;
	xmit->r_ctl = r_ctl;

	xmit->xri_tag = xri;
	xmit->context_tag = rnode->indicator;

	xmit->dif = 0;
	xmit->ct = SLI4_ELS_REQUEST64_CONTEXT_RPI;
	xmit->bs = 0;

	xmit->command = SLI4_WQE_XMIT_SEQUENCE64;
	xmit->class = SLI4_ELS_REQUEST64_CLASS_3;
	xmit->pu = 0;
	xmit->timer = timeout;

	xmit->abort_tag = 0;
	xmit->request_tag = tag;
	xmit->remote_xid = ox_id;

	xmit->iod = SLI4_ELS_REQUEST64_DIR_READ;

	if (rnode->node_group) {
		xmit->hlm = TRUE;
		xmit->remote_n_port_id = rnode->fc_id & 0x00ffffff;
	}

	xmit->cmd_type = SLI4_CMD_XMIT_SEQUENCE64_WQE;

	xmit->len_loc = 2;

	xmit->cq_id = 0xFFFF;

	return 0;
}

/**
 * @ingroup sli_fc
 * @brief Write a REQUEUE_XRI_WQE work queue entry.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the WQE.
 * @param size Buffer size, in bytes.
 * @param xri XRI for this exchange.
 * @param tag IO tag value.
 * @param cq_id The id of the completion queue where the WQE response is sent.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_requeue_xri_wqe(sli4_t *sli4, void *buf, size_t size, uint16_t xri, uint16_t tag, uint16_t cq_id)
{
	sli4_requeue_xri_wqe_t	*requeue = buf;

	ocs_memset(buf, 0, size);

	requeue->command = SLI4_WQE_REQUEUE_XRI;
	requeue->xri_tag = xri;
	requeue->request_tag = tag;
	requeue->xc = 1;
	requeue->qosd = 1;
	requeue->cq_id = cq_id;
	requeue->cmd_type = SLI4_CMD_REQUEUE_XRI_WQE;
	return 0;
}

int32_t
sli_xmit_bcast64_wqe(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *payload,
		uint32_t payload_len, uint8_t timeout, uint16_t xri, uint16_t tag,
		uint16_t cq_id, ocs_remote_node_t *rnode,
		uint8_t r_ctl, uint8_t type, uint8_t df_ctl)
{
	sli4_xmit_bcast64_wqe_t *bcast = buf;

	/* Command requires a temporary RPI (i.e. unused remote node) */
	if (rnode->attached) {
		ocs_log_test(sli4->os, "remote node %d in use\n", rnode->indicator);
		return -1;
	}

	ocs_memset(buf, 0, size);

	bcast->dbde = TRUE;
	bcast->sequence_payload.bde_type = SLI4_BDE_TYPE_BDE_64;
	bcast->sequence_payload.buffer_length = payload_len;
	bcast->sequence_payload.u.data.buffer_address_low  = ocs_addr32_lo(payload->phys);
	bcast->sequence_payload.u.data.buffer_address_high = ocs_addr32_hi(payload->phys);

	bcast->sequence_payload_length = payload_len;

	bcast->df_ctl = df_ctl;
	bcast->type = type;
	bcast->r_ctl = r_ctl;

	bcast->xri_tag = xri;

	bcast->ct = SLI4_ELS_REQUEST64_CONTEXT_VPI;
	bcast->context_tag = rnode->sport->indicator;

	bcast->class = SLI4_ELS_REQUEST64_CLASS_3;

	bcast->command = SLI4_WQE_XMIT_BCAST64;

	bcast->timer = timeout;

	bcast->request_tag = tag;

	bcast->temporary_rpi = rnode->indicator;

	bcast->len_loc = 0x1;

	bcast->iod = SLI4_ELS_REQUEST64_DIR_WRITE;

	bcast->cmd_type = SLI4_CMD_XMIT_BCAST64_WQE;

	bcast->cq_id = cq_id;

	return 0;
}

/**
 * @ingroup sli_fc
 * @brief Write an XMIT_BLS_RSP64_WQE work queue entry.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the WQE.
 * @param size Buffer size, in bytes.
 * @param payload Contents of the BLS payload to be sent.
 * @param xri XRI for this exchange.
 * @param tag IO tag value.
 * @param cq_id The id of the completion queue where the WQE response is sent.
 * @param rnode Destination of request (that is, the remote node).
 * @param s_id Source ID to use in the response. If UINT32_MAX, use SLI Port's ID.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_xmit_bls_rsp64_wqe(sli4_t *sli4, void *buf, size_t size, sli_bls_payload_t *payload,
		       uint16_t xri, uint16_t tag, uint16_t cq_id, ocs_remote_node_t *rnode, uint32_t s_id)
{
	sli4_xmit_bls_rsp_wqe_t *bls = buf;

	/*
	 * Callers can either specify RPI or S_ID, but not both
	 */
	if (rnode->attached && (s_id != UINT32_MAX)) {
		ocs_log_test(sli4->os, "S_ID specified for attached remote node %d\n",
			     rnode->indicator);
		return -1;
	}

	ocs_memset(buf, 0, size);

	if (SLI_BLS_ACC == payload->type) {
		bls->payload_word0 = (payload->u.acc.seq_id_last << 16) |
			(payload->u.acc.seq_id_validity << 24);
		bls->high_seq_cnt = payload->u.acc.high_seq_cnt;
		bls->low_seq_cnt = payload->u.acc.low_seq_cnt;
	} else if (SLI_BLS_RJT == payload->type) {
		bls->payload_word0 = *((uint32_t *)&payload->u.rjt);
		bls->ar = TRUE;
	} else {
		ocs_log_test(sli4->os, "bad BLS type %#x\n",
				payload->type);
		return -1;
	}

	bls->ox_id = payload->ox_id;
	bls->rx_id = payload->rx_id;

	if (rnode->attached) {
		bls->ct = SLI4_ELS_REQUEST64_CONTEXT_RPI;
		bls->context_tag = rnode->indicator;
	} else {
		bls->ct = SLI4_ELS_REQUEST64_CONTEXT_VPI;
		bls->context_tag = rnode->sport->indicator;

		if (UINT32_MAX != s_id) {
			bls->local_n_port_id = s_id & 0x00ffffff;
		} else {
			bls->local_n_port_id = rnode->sport->fc_id & 0x00ffffff;
		}
		bls->remote_id = rnode->fc_id & 0x00ffffff;

		bls->temporary_rpi = rnode->indicator;
	}

	bls->xri_tag = xri;

	bls->class = SLI4_ELS_REQUEST64_CLASS_3;

	bls->command = SLI4_WQE_XMIT_BLS_RSP;

	bls->request_tag = tag;

	bls->qosd = TRUE;

	if (rnode->node_group) {
		bls->hlm = TRUE;
		bls->remote_id = rnode->fc_id & 0x00ffffff;
	}

	bls->cq_id = cq_id;

	bls->cmd_type = SLI4_CMD_XMIT_BLS_RSP64_WQE;

	return 0;
}

/**
 * @ingroup sli_fc
 * @brief Write a XMIT_ELS_RSP64_WQE work queue entry.
 *
 * @param sli4 SLI context.
 * @param buf Destination buffer for the WQE.
 * @param size Buffer size, in bytes.
 * @param rsp DMA memory for the ELS response.
 * @param rsp_len Length of ELS response, in bytes.
 * @param xri XRI for this exchange.
 * @param tag IO tag value.
 * @param cq_id The id of the completion queue where the WQE response is sent.
 * @param ox_id OX_ID of the exchange containing the request.
 * @param rnode Destination of the ELS response (that is, the remote node).
 * @param flags Optional attributes, including:
 *  - SLI4_IO_CONTINUATION - IO is already active.
 * @param s_id S_ID used for special responses.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_xmit_els_rsp64_wqe(sli4_t *sli4, void *buf, size_t size, ocs_dma_t *rsp,
		       uint32_t rsp_len, uint16_t xri, uint16_t tag, uint16_t cq_id,
		       uint16_t ox_id, ocs_remote_node_t *rnode, uint32_t flags, uint32_t s_id)
{
	sli4_xmit_els_rsp64_wqe_t	*els = buf;

	ocs_memset(buf, 0, size);

	if (sli4->config.sgl_pre_registered) {
		els->dbde = TRUE;
	} else {
		els->xbl = TRUE;
	}

	els->els_response_payload.bde_type = SLI4_BDE_TYPE_BDE_64;
	els->els_response_payload.buffer_length = rsp_len;
	els->els_response_payload.u.data.buffer_address_low  = ocs_addr32_lo(rsp->phys);
	els->els_response_payload.u.data.buffer_address_high = ocs_addr32_hi(rsp->phys);

	els->els_response_payload_length = rsp_len;

	els->xri_tag = xri;

	els->class = SLI4_ELS_REQUEST64_CLASS_3;

	els->command = SLI4_WQE_ELS_RSP64;

	els->request_tag = tag;

	els->ox_id = ox_id;

	els->iod = SLI4_ELS_REQUEST64_DIR_WRITE;

	els->qosd = TRUE;

	if (flags & SLI4_IO_CONTINUATION) {
		els->xc = TRUE;
	}

	if (rnode->attached) {
		els->ct = SLI4_ELS_REQUEST64_CONTEXT_RPI;
		els->context_tag = rnode->indicator;
	} else {
		els->ct = SLI4_ELS_REQUEST64_CONTEXT_VPI;
		els->context_tag = rnode->sport->indicator;
		els->remote_id = rnode->fc_id & 0x00ffffff;
		els->temporary_rpi = rnode->indicator;
		if (UINT32_MAX != s_id) {
			els->sp = TRUE;
			els->s_id = s_id & 0x00ffffff;
		}
	}

	if (rnode->node_group) {
		els->hlm = TRUE;
		els->remote_id = rnode->fc_id & 0x00ffffff;
	}

	els->cmd_type = SLI4_ELS_REQUEST64_CMD_GEN;

	els->cq_id = cq_id;

	return 0;
}

/**
 * @ingroup sli_fc
 * @brief Process an asynchronous Link State event entry.
 *
 * @par Description
 * Parses Asynchronous Completion Queue Entry (ACQE),
 * creates an abstracted event, and calls registered callback functions.
 *
 * @param sli4 SLI context.
 * @param acqe Pointer to the ACQE.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_fc_process_link_state(sli4_t *sli4, void *acqe)
{
	sli4_link_state_t	*link_state = acqe;
	sli4_link_event_t	event = { 0 };
	int32_t			rc = 0;

	if (!sli4->link) {
		/* bail if there is no callback */
		return 0;
	}

	if (SLI4_LINK_TYPE_ETHERNET == link_state->link_type) {
		event.topology = SLI_LINK_TOPO_NPORT;
		event.medium   = SLI_LINK_MEDIUM_ETHERNET;
	} else {
		/* TODO is this supported for anything other than FCoE? */
		ocs_log_test(sli4->os, "unsupported link type %#x\n",
				link_state->link_type);
		event.topology = SLI_LINK_TOPO_MAX;
		event.medium   = SLI_LINK_MEDIUM_MAX;
		rc = -1;
	}

	switch (link_state->port_link_status) {
	case SLI4_PORT_LINK_STATUS_PHYSICAL_DOWN:
	case SLI4_PORT_LINK_STATUS_LOGICAL_DOWN:
		event.status = SLI_LINK_STATUS_DOWN;
		break;
	case SLI4_PORT_LINK_STATUS_PHYSICAL_UP:
	case SLI4_PORT_LINK_STATUS_LOGICAL_UP:
		event.status = SLI_LINK_STATUS_UP;
		break;
	default:
		ocs_log_test(sli4->os, "unsupported link status %#x\n",
				link_state->port_link_status);
		event.status = SLI_LINK_STATUS_MAX;
		rc = -1;
	}

	switch (link_state->port_speed) {
	case 0:
		event.speed = 0;
		break;
	case 1:
		event.speed = 10;
		break;
	case 2:
		event.speed = 100;
		break;
	case 3:
		event.speed = 1000;
		break;
	case 4:
		event.speed = 10000;
		break;
	case 5:
		event.speed = 20000;
		break;
	case 6:
		event.speed = 25000;
		break;
	case 7:
		event.speed = 40000;
		break;
	case 8:
		event.speed = 100000;
		break;
	default:
		ocs_log_test(sli4->os, "unsupported port_speed %#x\n",
				link_state->port_speed);
		rc = -1;
	}

	sli4->link(sli4->link_arg, (void *)&event);

	return rc;
}

/**
 * @ingroup sli_fc
 * @brief Process an asynchronous Link Attention event entry.
 *
 * @par Description
 * Parses Asynchronous Completion Queue Entry (ACQE),
 * creates an abstracted event, and calls the registered callback functions.
 *
 * @param sli4 SLI context.
 * @param acqe Pointer to the ACQE.
 *
 * @todo XXX all events return LINK_UP.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_fc_process_link_attention(sli4_t *sli4, void *acqe)
{
	sli4_link_attention_t	*link_attn = acqe;
	sli4_link_event_t	event = { 0 };

	ocs_log_debug(sli4->os, "link_number=%d attn_type=%#x topology=%#x port_speed=%#x "
			"port_fault=%#x shared_link_status=%#x logical_link_speed=%#x "
			"event_tag=%#x\n", link_attn->link_number, link_attn->attn_type,
			link_attn->topology, link_attn->port_speed, link_attn->port_fault,
			link_attn->shared_link_status, link_attn->logical_link_speed,
			link_attn->event_tag);

	if (!sli4->link) {
		return 0;
	}

	event.medium   = SLI_LINK_MEDIUM_FC;

	switch (link_attn->attn_type) {
	case SLI4_LINK_ATTN_TYPE_LINK_UP:
		event.status = SLI_LINK_STATUS_UP;
		break;
	case SLI4_LINK_ATTN_TYPE_LINK_DOWN:
		event.status = SLI_LINK_STATUS_DOWN;
		break;
	case SLI4_LINK_ATTN_TYPE_NO_HARD_ALPA:
		ocs_log_debug(sli4->os, "attn_type: no hard alpa\n");
		event.status = SLI_LINK_STATUS_NO_ALPA;
		break;
	default:
		ocs_log_test(sli4->os, "attn_type: unknown\n");
		break;
	}

	switch (link_attn->event_type) {
	case SLI4_FC_EVENT_LINK_ATTENTION:
		break;
	case SLI4_FC_EVENT_SHARED_LINK_ATTENTION:
		ocs_log_debug(sli4->os, "event_type: FC shared link event \n");
		break;
	default:
		ocs_log_test(sli4->os, "event_type: unknown\n");
		break;
	}

	switch (link_attn->topology) {
	case SLI4_LINK_ATTN_P2P:
		event.topology = SLI_LINK_TOPO_NPORT;
		break;
	case SLI4_LINK_ATTN_FC_AL:
		event.topology = SLI_LINK_TOPO_LOOP;
		break;
	case SLI4_LINK_ATTN_INTERNAL_LOOPBACK:
		ocs_log_debug(sli4->os, "topology Internal loopback\n");
		event.topology = SLI_LINK_TOPO_LOOPBACK_INTERNAL;
		break;
	case SLI4_LINK_ATTN_SERDES_LOOPBACK:
		ocs_log_debug(sli4->os, "topology serdes loopback\n");
		event.topology = SLI_LINK_TOPO_LOOPBACK_EXTERNAL;
		break;
	default:
		ocs_log_test(sli4->os, "topology: unknown\n");
		break;
	}

	event.speed    = link_attn->port_speed * 1000;

	sli4->link(sli4->link_arg, (void *)&event);

	return 0;
}

/**
 * @ingroup sli_fc
 * @brief Parse an FC/FCoE work queue CQ entry.
 *
 * @param sli4 SLI context.
 * @param cq CQ to process.
 * @param cqe Pointer to the CQ entry.
 * @param etype CQ event type.
 * @param r_id Resource ID associated with this completion message (such as the IO tag).
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_fc_cqe_parse(sli4_t *sli4, sli4_queue_t *cq, uint8_t *cqe, sli4_qentry_e *etype,
		uint16_t *r_id)
{
	uint8_t		code = cqe[SLI4_CQE_CODE_OFFSET];
	int32_t		rc = -1;

	switch (code) {
	case SLI4_CQE_CODE_WORK_REQUEST_COMPLETION:
	{
		sli4_fc_wcqe_t *wcqe = (void *)cqe;

		*etype = SLI_QENTRY_WQ;
		*r_id = wcqe->request_tag;
		rc = wcqe->status;

		/* Flag errors except for FCP_RSP_FAILURE */
		if (rc && (rc != SLI4_FC_WCQE_STATUS_FCP_RSP_FAILURE)) {

			ocs_log_test(sli4->os, "WCQE: status=%#x hw_status=%#x tag=%#x w1=%#x w2=%#x xb=%d\n",
				wcqe->status, wcqe->hw_status,
				wcqe->request_tag, wcqe->wqe_specific_1,
				wcqe->wqe_specific_2, wcqe->xb);
			ocs_log_test(sli4->os, "      %08X %08X %08X %08X\n", ((uint32_t*) cqe)[0], ((uint32_t*) cqe)[1],
				((uint32_t*) cqe)[2], ((uint32_t*) cqe)[3]);
		}

		/* TODO: need to pass additional status back out of here as well
		 * as status (could overload rc as status/addlstatus are only 8 bits each)
		 */

		break;
	}
	case SLI4_CQE_CODE_RQ_ASYNC:
	{
		sli4_fc_async_rcqe_t *rcqe = (void *)cqe;

		*etype = SLI_QENTRY_RQ;
		*r_id = rcqe->rq_id;
		rc = rcqe->status;
		break;
	}
	case SLI4_CQE_CODE_RQ_ASYNC_V1:
	{
		sli4_fc_async_rcqe_v1_t *rcqe = (void *)cqe;

		*etype = SLI_QENTRY_RQ;
		*r_id = rcqe->rq_id;
		rc = rcqe->status;
		break;
	}
	case SLI4_CQE_CODE_OPTIMIZED_WRITE_CMD:
	{
		sli4_fc_optimized_write_cmd_cqe_t *optcqe = (void *)cqe;

		*etype = SLI_QENTRY_OPT_WRITE_CMD;
		*r_id = optcqe->rq_id;
		rc = optcqe->status;
		break;
	}
	case SLI4_CQE_CODE_OPTIMIZED_WRITE_DATA:
	{
		sli4_fc_optimized_write_data_cqe_t *dcqe = (void *)cqe;

		*etype = SLI_QENTRY_OPT_WRITE_DATA;
		*r_id = dcqe->xri;
		rc = dcqe->status;

		/* Flag errors */
		if (rc != SLI4_FC_WCQE_STATUS_SUCCESS) {
			ocs_log_test(sli4->os, "Optimized DATA CQE: status=%#x hw_status=%#x xri=%#x dpl=%#x w3=%#x xb=%d\n",
				dcqe->status, dcqe->hw_status,
				dcqe->xri, dcqe->total_data_placed,
				((uint32_t*) cqe)[3], dcqe->xb);
		}
		break;
	}
	case SLI4_CQE_CODE_RQ_COALESCING:
	{
		sli4_fc_coalescing_rcqe_t *rcqe = (void *)cqe;

		*etype = SLI_QENTRY_RQ;
		*r_id = rcqe->rq_id;
		rc = rcqe->status;
		break;
	}
	case SLI4_CQE_CODE_XRI_ABORTED:
	{
		sli4_fc_xri_aborted_cqe_t *xa = (void *)cqe;

		*etype = SLI_QENTRY_XABT;
		*r_id = xa->xri;
		rc = 0;
		break;
	}
	case SLI4_CQE_CODE_RELEASE_WQE: {
		sli4_fc_wqec_t *wqec = (void*) cqe;

		*etype = SLI_QENTRY_WQ_RELEASE;
		*r_id = wqec->wq_id;
		rc = 0;
		break;
	}
	default:
		ocs_log_test(sli4->os, "CQE completion code %d not handled\n", code);
		*etype = SLI_QENTRY_MAX;
		*r_id = UINT16_MAX;
	}

	return rc;
}

/**
 * @ingroup sli_fc
 * @brief Return the ELS/CT response length.
 *
 * @param sli4 SLI context.
 * @param cqe Pointer to the CQ entry.
 *
 * @return Returns the length, in bytes.
 */
uint32_t
sli_fc_response_length(sli4_t *sli4, uint8_t *cqe)
{
	sli4_fc_wcqe_t *wcqe = (void *)cqe;

	return wcqe->wqe_specific_1;
}

/**
 * @ingroup sli_fc
 * @brief Return the FCP IO length.
 *
 * @param sli4 SLI context.
 * @param cqe Pointer to the CQ entry.
 *
 * @return Returns the length, in bytes.
 */
uint32_t
sli_fc_io_length(sli4_t *sli4, uint8_t *cqe)
{
	sli4_fc_wcqe_t *wcqe = (void *)cqe;

	return wcqe->wqe_specific_1;
}

/**
 * @ingroup sli_fc
 * @brief Retrieve the D_ID from the completion.
 *
 * @param sli4 SLI context.
 * @param cqe Pointer to the CQ entry.
 * @param d_id Pointer where the D_ID is written.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_fc_els_did(sli4_t *sli4, uint8_t *cqe, uint32_t *d_id)
{
	sli4_fc_wcqe_t *wcqe = (void *)cqe;

	*d_id = 0;

	if (wcqe->status) {
		return -1;
	} else {
		*d_id = wcqe->wqe_specific_2 & 0x00ffffff;
		return 0;
	}
}

uint32_t
sli_fc_ext_status(sli4_t *sli4, uint8_t *cqe)
{
	sli4_fc_wcqe_t *wcqe = (void *)cqe;
	uint32_t	mask;

	switch (wcqe->status) {
	case SLI4_FC_WCQE_STATUS_FCP_RSP_FAILURE:
		mask = UINT32_MAX;
		break;
	case SLI4_FC_WCQE_STATUS_LOCAL_REJECT:
	case SLI4_FC_WCQE_STATUS_CMD_REJECT:
		mask = 0xff;
		break;
	case SLI4_FC_WCQE_STATUS_NPORT_RJT:
	case SLI4_FC_WCQE_STATUS_FABRIC_RJT:
	case SLI4_FC_WCQE_STATUS_NPORT_BSY:
	case SLI4_FC_WCQE_STATUS_FABRIC_BSY:
	case SLI4_FC_WCQE_STATUS_LS_RJT:
		mask = UINT32_MAX;
		break;
	case SLI4_FC_WCQE_STATUS_DI_ERROR:
		mask = UINT32_MAX;
		break;
	default:
		mask = 0;
	}

	return wcqe->wqe_specific_2 & mask;
}

/**
 * @ingroup sli_fc
 * @brief Retrieve the RQ index from the completion.
 *
 * @param sli4 SLI context.
 * @param cqe Pointer to the CQ entry.
 * @param rq_id Pointer where the rq_id is written.
 * @param index Pointer where the index is written.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_fc_rqe_rqid_and_index(sli4_t *sli4, uint8_t *cqe, uint16_t *rq_id, uint32_t *index)
{
	sli4_fc_async_rcqe_t	*rcqe = (void *)cqe;
	sli4_fc_async_rcqe_v1_t	*rcqe_v1 = (void *)cqe;
	int32_t	rc = -1;
	uint8_t	code = 0;

	*rq_id = 0;
	*index = UINT32_MAX;

	code = cqe[SLI4_CQE_CODE_OFFSET];

	if (code == SLI4_CQE_CODE_RQ_ASYNC) {
		*rq_id = rcqe->rq_id;
		if (SLI4_FC_ASYNC_RQ_SUCCESS == rcqe->status) {
			*index = rcqe->rq_element_index;
			rc = 0;
		} else {
			*index = rcqe->rq_element_index;
			rc = rcqe->status;
			ocs_log_test(sli4->os, "status=%02x (%s) rq_id=%d, index=%x pdpl=%x sof=%02x eof=%02x hdpl=%x\n",
				rcqe->status, sli_fc_get_status_string(rcqe->status), rcqe->rq_id,
				rcqe->rq_element_index, rcqe->payload_data_placement_length, rcqe->sof_byte,
				rcqe->eof_byte, rcqe->header_data_placement_length);
		}
	} else if (code == SLI4_CQE_CODE_RQ_ASYNC_V1) {
		*rq_id = rcqe_v1->rq_id;
		if (SLI4_FC_ASYNC_RQ_SUCCESS == rcqe_v1->status) {
			*index = rcqe_v1->rq_element_index;
			rc = 0;
		} else {
			*index = rcqe_v1->rq_element_index;
			rc = rcqe_v1->status;
			ocs_log_test(sli4->os, "status=%02x (%s) rq_id=%d, index=%x pdpl=%x sof=%02x eof=%02x hdpl=%x\n",
				rcqe_v1->status, sli_fc_get_status_string(rcqe_v1->status),
				rcqe_v1->rq_id, rcqe_v1->rq_element_index,
				rcqe_v1->payload_data_placement_length, rcqe_v1->sof_byte,
				rcqe_v1->eof_byte, rcqe_v1->header_data_placement_length);
		}
	} else if (code == SLI4_CQE_CODE_OPTIMIZED_WRITE_CMD) {
		sli4_fc_optimized_write_cmd_cqe_t *optcqe = (void *)cqe;

		*rq_id = optcqe->rq_id;
		if (SLI4_FC_ASYNC_RQ_SUCCESS == optcqe->status) {
			*index = optcqe->rq_element_index;
			rc = 0;
		} else {
			*index = optcqe->rq_element_index;
			rc = optcqe->status;
			ocs_log_test(sli4->os, "status=%02x (%s) rq_id=%d, index=%x pdpl=%x hdpl=%x oox=%d agxr=%d xri=0x%x rpi=0x%x\n",
				optcqe->status, sli_fc_get_status_string(optcqe->status), optcqe->rq_id,
				optcqe->rq_element_index, optcqe->payload_data_placement_length,
				optcqe->header_data_placement_length, optcqe->oox, optcqe->agxr, optcqe->xri,
				optcqe->rpi);
		}
	} else if (code == SLI4_CQE_CODE_RQ_COALESCING) {
		sli4_fc_coalescing_rcqe_t	*rcqe = (void *)cqe;

		*rq_id = rcqe->rq_id;
		if (SLI4_FC_COALESCE_RQ_SUCCESS == rcqe->status) {
			*index = rcqe->rq_element_index;
			rc = 0;
		} else {
			*index = UINT32_MAX;
			rc = rcqe->status;

			ocs_log_test(sli4->os, "status=%02x (%s) rq_id=%d, index=%x rq_id=%#x sdpl=%x\n",
				rcqe->status, sli_fc_get_status_string(rcqe->status), rcqe->rq_id,
				rcqe->rq_element_index, rcqe->rq_id, rcqe->sequence_reporting_placement_length);
		}
	} else {
		*index = UINT32_MAX;

		rc = rcqe->status;

		ocs_log_debug(sli4->os, "status=%02x rq_id=%d, index=%x pdpl=%x sof=%02x eof=%02x hdpl=%x\n",
			rcqe->status, rcqe->rq_id, rcqe->rq_element_index, rcqe->payload_data_placement_length,
			rcqe->sof_byte, rcqe->eof_byte, rcqe->header_data_placement_length);
	}

	return rc;
}

/**
 * @ingroup sli_fc
 * @brief Process an asynchronous FCoE event entry.
 *
 * @par Description
 * Parses Asynchronous Completion Queue Entry (ACQE),
 * creates an abstracted event, and calls the registered callback functions.
 *
 * @param sli4 SLI context.
 * @param acqe Pointer to the ACQE.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
sli_fc_process_fcoe(sli4_t *sli4, void *acqe)
{
	sli4_fcoe_fip_t	*fcoe = acqe;
	sli4_fip_event_t event = { 0 };
	uint32_t	mask = UINT32_MAX;

	ocs_log_debug(sli4->os, "ACQE FCoE FIP type=%02x count=%d tag=%#x\n",
			fcoe->event_type,
			fcoe->fcf_count,
			fcoe->event_tag);

	if (!sli4->fip) {
		return 0;
	}

	event.type = fcoe->event_type;
	event.index = UINT32_MAX;

	switch (fcoe->event_type) {
	case SLI4_FCOE_FIP_FCF_DISCOVERED:
		ocs_log_debug(sli4->os, "FCF Discovered index=%d\n", fcoe->event_information);
		break;
	case SLI4_FCOE_FIP_FCF_TABLE_FULL:
		ocs_log_debug(sli4->os, "FCF Table Full\n");
		mask = 0;
		break;
	case SLI4_FCOE_FIP_FCF_DEAD:
		ocs_log_debug(sli4->os, "FCF Dead/Gone index=%d\n", fcoe->event_information);
		break;
	case SLI4_FCOE_FIP_FCF_CLEAR_VLINK:
		mask = UINT16_MAX;
		ocs_log_debug(sli4->os, "Clear VLINK Received VPI=%#x\n", fcoe->event_information & mask);
		break;
	case SLI4_FCOE_FIP_FCF_MODIFIED:
		ocs_log_debug(sli4->os, "FCF Modified\n");
		break;
	default:
		ocs_log_test(sli4->os, "bad FCoE type %#x", fcoe->event_type);
		mask = 0;
	}

	if (mask != 0) {
		event.index = fcoe->event_information & mask;
	}

	sli4->fip(sli4->fip_arg, &event);

	return 0;
}

/**
 * @ingroup sli_fc
 * @brief Allocate a receive queue.
 *
 * @par Description
 * Allocates DMA memory and configures the requested queue type.
 *
 * @param sli4 SLI context.
 * @param q Pointer to the queue object for the header.
 * @param n_entries Number of entries to allocate.
 * @param buffer_size buffer size for the queue.
 * @param cq Associated CQ.
 * @param ulp The ULP to bind
 * @param is_hdr Used to validate the rq_id and set the type of queue
 *
 * @return Returns 0 on success, or -1 on failure.
 */
int32_t
sli_fc_rq_alloc(sli4_t *sli4, sli4_queue_t *q,
		uint32_t n_entries, uint32_t buffer_size,
		sli4_queue_t *cq, uint16_t ulp, uint8_t is_hdr)
{
	int32_t (*rq_create)(sli4_t *, void *, size_t, ocs_dma_t *, uint16_t, uint16_t, uint16_t);

	if ((sli4 == NULL) || (q == NULL)) {
		void *os = sli4 != NULL ? sli4->os : NULL;

		ocs_log_err(os, "bad parameter sli4=%p q=%p\n", sli4, q);
		return -1;
	}

	if (__sli_queue_init(sli4, q, SLI_QTYPE_RQ, SLI4_FCOE_RQE_SIZE,
				n_entries, SLI_PAGE_SIZE)) {
		return -1;
	}

	if (sli4->if_type == SLI4_IF_TYPE_BE3_SKH_PF) {
		rq_create = sli_cmd_fcoe_rq_create;
	} else {
		rq_create = sli_cmd_fcoe_rq_create_v1;
	}

	if (rq_create(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE, &q->dma,
		      cq->id, ulp, buffer_size)) {
		if (__sli_create_queue(sli4, q)) {
			ocs_dma_free(sli4->os, &q->dma);
			return -1;
		}
		if (is_hdr && q->id & 1) {
			ocs_log_test(sli4->os, "bad header RQ_ID %d\n", q->id);
			ocs_dma_free(sli4->os, &q->dma);
			return -1;
		} else if (!is_hdr  && (q->id & 1) == 0) {
			ocs_log_test(sli4->os, "bad data RQ_ID %d\n", q->id);
			ocs_dma_free(sli4->os, &q->dma);
			return -1;
		}
	} else {
		return -1;
	}
	q->u.flag.is_hdr = is_hdr;
	if (SLI4_IF_TYPE_BE3_SKH_PF == sli4->if_type) {
		q->u.flag.rq_batch = TRUE;
	}
	return 0;
}


/**
 * @ingroup sli_fc
 * @brief Allocate a receive queue set.
 *
 * @param sli4 SLI context.
 * @param num_rq_pairs to create
 * @param qs Pointers to the queue objects for both header and data.
 *	Length of this arrays should be 2 * num_rq_pairs
 * @param base_cq_id. Assumes base_cq_id : (base_cq_id + num_rq_pairs) cqs as allotted.
 * @param n_entries number of entries in each RQ queue.
 * @param header_buffer_size
 * @param payload_buffer_size
 * @param ulp The ULP to bind
 *
 * @return Returns 0 on success, or -1 on failure.
 */
int32_t
sli_fc_rq_set_alloc(sli4_t *sli4, uint32_t num_rq_pairs,
		    sli4_queue_t *qs[], uint32_t base_cq_id,
		    uint32_t n_entries, uint32_t header_buffer_size,
		    uint32_t payload_buffer_size,  uint16_t ulp)
{
	uint32_t i, p, offset = 0;
	uint32_t payload_size, total_page_count = 0;
	uintptr_t addr;
	ocs_dma_t dma;
	sli4_res_common_create_queue_set_t *rsp = NULL;
	sli4_req_fcoe_rq_create_v2_t    *req = NULL;

	ocs_memset(&dma, 0, sizeof(dma));

	for (i = 0; i < (num_rq_pairs * 2); i++) {
		if (__sli_queue_init(sli4, qs[i], SLI_QTYPE_RQ, SLI4_FCOE_RQE_SIZE,
					n_entries, SLI_PAGE_SIZE)) {
			goto error;
		}
	}

	total_page_count = sli_page_count(qs[0]->dma.size, SLI_PAGE_SIZE) * num_rq_pairs * 2;

	/* Payload length must accommodate both request and response */
	payload_size = max((sizeof(sli4_req_fcoe_rq_create_v1_t) + (8 * total_page_count)),
			 sizeof(sli4_res_common_create_queue_set_t));

	if (ocs_dma_alloc(sli4->os, &dma, payload_size, SLI_PAGE_SIZE)) {
		ocs_log_err(sli4->os, "DMA allocation failed\n");
		goto error;
	}
	ocs_memset(dma.virt, 0, payload_size);

	if (sli_cmd_sli_config(sli4, sli4->bmbx.virt, SLI4_BMBX_SIZE,
			payload_size, &dma) == -1) {
		goto error;
	}
	req = (sli4_req_fcoe_rq_create_v2_t *)((uint8_t *)dma.virt);

	/* Fill Header fields */
	req->hdr.opcode    = SLI4_OPC_FCOE_RQ_CREATE;
	req->hdr.subsystem = SLI4_SUBSYSTEM_FCFCOE;
	req->hdr.version   = 2;
	req->hdr.request_length = sizeof(sli4_req_fcoe_rq_create_v2_t) - sizeof(sli4_req_hdr_t)
					+ (8 * total_page_count);

	/* Fill Payload fields */
	req->dnb           = TRUE;
	req->num_pages     = sli_page_count(qs[0]->dma.size, SLI_PAGE_SIZE);
	req->rqe_count     = qs[0]->dma.size / SLI4_FCOE_RQE_SIZE;
	req->rqe_size      = SLI4_FCOE_RQE_SIZE_8;
	req->page_size     = SLI4_FCOE_RQ_PAGE_SIZE_4096;
	req->rq_count      = num_rq_pairs * 2;
	req->base_cq_id    = base_cq_id;
	req->hdr_buffer_size     = header_buffer_size;
	req->payload_buffer_size = payload_buffer_size;

	for (i = 0; i < (num_rq_pairs * 2); i++) {
		for (p = 0, addr = qs[i]->dma.phys; p < req->num_pages; p++, addr += SLI_PAGE_SIZE) {
			req->page_physical_address[offset].low  = ocs_addr32_lo(addr);
			req->page_physical_address[offset].high = ocs_addr32_hi(addr);
			offset++;
		}
	}

	if (sli_bmbx_command(sli4)){
		ocs_log_crit(sli4->os, "bootstrap mailbox write faild RQSet\n");
		goto error;
	}


	rsp = (void *)((uint8_t *)dma.virt);
	if (rsp->hdr.status) {
		ocs_log_err(sli4->os, "bad create RQSet status=%#x addl=%#x\n",
			rsp->hdr.status, rsp->hdr.additional_status);
		goto error;
	} else {
		for (i = 0; i < (num_rq_pairs * 2); i++) {
			qs[i]->id = i + rsp->q_id;
			if ((qs[i]->id & 1) == 0) {
				qs[i]->u.flag.is_hdr = TRUE;
			} else {
				qs[i]->u.flag.is_hdr = FALSE;
			}
			qs[i]->doorbell_offset = regmap[SLI4_REG_FCOE_RQ_DOORBELL][sli4->if_type].off;
			qs[i]->doorbell_rset = regmap[SLI4_REG_FCOE_RQ_DOORBELL][sli4->if_type].rset;
		}
	}

	ocs_dma_free(sli4->os, &dma);

	return 0;

error:
	for (i = 0; i < (num_rq_pairs * 2); i++) {
		if (qs[i]->dma.size) {
			ocs_dma_free(sli4->os, &qs[i]->dma);
		}
	}

	if (dma.size) {
		ocs_dma_free(sli4->os, &dma);
	}

	return -1;
}

/**
 * @ingroup sli_fc
 * @brief Get the RPI resource requirements.
 *
 * @param sli4 SLI context.
 * @param n_rpi Number of RPIs desired.
 *
 * @return Returns the number of bytes needed. This value may be zero.
 */
uint32_t
sli_fc_get_rpi_requirements(sli4_t *sli4, uint32_t n_rpi)
{
	uint32_t	bytes = 0;

	/* Check if header templates needed */
	if (sli4->config.hdr_template_req) {
		/* round up to a page */
		bytes = SLI_ROUND_PAGE(n_rpi * SLI4_FCOE_HDR_TEMPLATE_SIZE);
	}

	return bytes;
}

/**
 * @ingroup sli_fc
 * @brief Return a text string corresponding to a CQE status value
 *
 * @param status Status value
 *
 * @return Returns corresponding string, otherwise "unknown"
 */
const char *
sli_fc_get_status_string(uint32_t status)
{
	static struct {
		uint32_t code;
		const char *label;
	} lookup[] = {
		{SLI4_FC_WCQE_STATUS_SUCCESS,			"SUCCESS"},
		{SLI4_FC_WCQE_STATUS_FCP_RSP_FAILURE,		"FCP_RSP_FAILURE"},
		{SLI4_FC_WCQE_STATUS_REMOTE_STOP,		"REMOTE_STOP"},
		{SLI4_FC_WCQE_STATUS_LOCAL_REJECT,		"LOCAL_REJECT"},
		{SLI4_FC_WCQE_STATUS_NPORT_RJT,			"NPORT_RJT"},
		{SLI4_FC_WCQE_STATUS_FABRIC_RJT,		"FABRIC_RJT"},
		{SLI4_FC_WCQE_STATUS_NPORT_BSY,			"NPORT_BSY"},
		{SLI4_FC_WCQE_STATUS_FABRIC_BSY,		"FABRIC_BSY"},
		{SLI4_FC_WCQE_STATUS_LS_RJT,			"LS_RJT"},
		{SLI4_FC_WCQE_STATUS_CMD_REJECT,		"CMD_REJECT"},
		{SLI4_FC_WCQE_STATUS_FCP_TGT_LENCHECK,		"FCP_TGT_LENCHECK"},
		{SLI4_FC_WCQE_STATUS_RQ_BUF_LEN_EXCEEDED,	"BUF_LEN_EXCEEDED"},
		{SLI4_FC_WCQE_STATUS_RQ_INSUFF_BUF_NEEDED,	"RQ_INSUFF_BUF_NEEDED"},
		{SLI4_FC_WCQE_STATUS_RQ_INSUFF_FRM_DISC,	"RQ_INSUFF_FRM_DESC"},
		{SLI4_FC_WCQE_STATUS_RQ_DMA_FAILURE,		"RQ_DMA_FAILURE"},
		{SLI4_FC_WCQE_STATUS_FCP_RSP_TRUNCATE,		"FCP_RSP_TRUNCATE"},
		{SLI4_FC_WCQE_STATUS_DI_ERROR,			"DI_ERROR"},
		{SLI4_FC_WCQE_STATUS_BA_RJT,			"BA_RJT"},
		{SLI4_FC_WCQE_STATUS_RQ_INSUFF_XRI_NEEDED,	"RQ_INSUFF_XRI_NEEDED"},
		{SLI4_FC_WCQE_STATUS_RQ_INSUFF_XRI_DISC,	"INSUFF_XRI_DISC"},
		{SLI4_FC_WCQE_STATUS_RX_ERROR_DETECT,		"RX_ERROR_DETECT"},
		{SLI4_FC_WCQE_STATUS_RX_ABORT_REQUEST,		"RX_ABORT_REQUEST"},
		};
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(lookup); i++) {
		if (status == lookup[i].code) {
			return lookup[i].label;
		}
	}
	return "unknown";
}
