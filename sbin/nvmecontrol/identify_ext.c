/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012-2013 Intel Corporation
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nvmecontrol.h"
#include "nvmecontrol_ext.h"

void
nvme_print_controller(struct nvme_controller_data *cdata)
{
	uint8_t str[128];
	char cbuf[UINT128_DIG + 1];
	uint16_t oncs, oacs;
	uint8_t compare, write_unc, dsm, vwc_present;
	uint8_t security, fmt, fw, nsmgmt;
	uint8_t	fw_slot1_ro, fw_num_slots;
	uint8_t ns_smart;
	uint8_t sqes_max, sqes_min;
	uint8_t cqes_max, cqes_min;

	oncs = cdata->oncs;
	compare = (oncs >> NVME_CTRLR_DATA_ONCS_COMPARE_SHIFT) &
		NVME_CTRLR_DATA_ONCS_COMPARE_MASK;
	write_unc = (oncs >> NVME_CTRLR_DATA_ONCS_WRITE_UNC_SHIFT) &
		NVME_CTRLR_DATA_ONCS_WRITE_UNC_MASK;
	dsm = (oncs >> NVME_CTRLR_DATA_ONCS_DSM_SHIFT) &
		NVME_CTRLR_DATA_ONCS_DSM_MASK;
	vwc_present = (cdata->vwc >> NVME_CTRLR_DATA_VWC_PRESENT_SHIFT) &
		NVME_CTRLR_DATA_VWC_PRESENT_MASK;

	oacs = cdata->oacs;
	security = (oacs >> NVME_CTRLR_DATA_OACS_SECURITY_SHIFT) &
		NVME_CTRLR_DATA_OACS_SECURITY_MASK;
	fmt = (oacs >> NVME_CTRLR_DATA_OACS_FORMAT_SHIFT) &
		NVME_CTRLR_DATA_OACS_FORMAT_MASK;
	fw = (oacs >> NVME_CTRLR_DATA_OACS_FIRMWARE_SHIFT) &
		NVME_CTRLR_DATA_OACS_FIRMWARE_MASK;
	nsmgmt = (oacs >> NVME_CTRLR_DATA_OACS_NSMGMT_SHIFT) &
		NVME_CTRLR_DATA_OACS_NSMGMT_MASK;

	fw_num_slots = (cdata->frmw >> NVME_CTRLR_DATA_FRMW_NUM_SLOTS_SHIFT) &
		NVME_CTRLR_DATA_FRMW_NUM_SLOTS_MASK;
	fw_slot1_ro = (cdata->frmw >> NVME_CTRLR_DATA_FRMW_SLOT1_RO_SHIFT) &
		NVME_CTRLR_DATA_FRMW_SLOT1_RO_MASK;

	ns_smart = (cdata->lpa >> NVME_CTRLR_DATA_LPA_NS_SMART_SHIFT) &
		NVME_CTRLR_DATA_LPA_NS_SMART_MASK;

	sqes_min = (cdata->sqes >> NVME_CTRLR_DATA_SQES_MIN_SHIFT) &
		NVME_CTRLR_DATA_SQES_MIN_MASK;
	sqes_max = (cdata->sqes >> NVME_CTRLR_DATA_SQES_MAX_SHIFT) &
		NVME_CTRLR_DATA_SQES_MAX_MASK;

	cqes_min = (cdata->cqes >> NVME_CTRLR_DATA_CQES_MIN_SHIFT) &
		NVME_CTRLR_DATA_CQES_MIN_MASK;
	cqes_max = (cdata->cqes >> NVME_CTRLR_DATA_CQES_MAX_SHIFT) &
		NVME_CTRLR_DATA_CQES_MAX_MASK;

	printf("Controller Capabilities/Features\n");
	printf("================================\n");
	printf("Vendor ID:                   %04x\n", cdata->vid);
	printf("Subsystem Vendor ID:         %04x\n", cdata->ssvid);
	nvme_strvis(str, cdata->sn, sizeof(str), NVME_SERIAL_NUMBER_LENGTH);
	printf("Serial Number:               %s\n", str);
	nvme_strvis(str, cdata->mn, sizeof(str), NVME_MODEL_NUMBER_LENGTH);
	printf("Model Number:                %s\n", str);
	nvme_strvis(str, cdata->fr, sizeof(str), NVME_FIRMWARE_REVISION_LENGTH);
	printf("Firmware Version:            %s\n", str);
	printf("Recommended Arb Burst:       %d\n", cdata->rab);
	printf("IEEE OUI Identifier:         %02x %02x %02x\n",
		cdata->ieee[0], cdata->ieee[1], cdata->ieee[2]);
	printf("Multi-Path I/O Capabilities: %s%s%s%s\n",
	    (cdata->mic == 0) ? "Not Supported" : "",
	    ((cdata->mic >> NVME_CTRLR_DATA_MIC_SRIOVVF_SHIFT) &
	     NVME_CTRLR_DATA_MIC_SRIOVVF_MASK) ? "SR-IOV VF, " : "",
	    ((cdata->mic >> NVME_CTRLR_DATA_MIC_MCTRLRS_SHIFT) &
	     NVME_CTRLR_DATA_MIC_MCTRLRS_MASK) ? "Multiple controllers, " : "",
	    ((cdata->mic >> NVME_CTRLR_DATA_MIC_MPORTS_SHIFT) &
	     NVME_CTRLR_DATA_MIC_MPORTS_MASK) ? "Multiple ports" : "");
	/* TODO: Use CAP.MPSMIN to determine true memory page size. */
	printf("Max Data Transfer Size:      ");
	if (cdata->mdts == 0)
		printf("Unlimited\n");
	else
		printf("%ld\n", PAGE_SIZE * (1L << cdata->mdts));
	printf("Controller ID:               0x%02x\n", cdata->ctrlr_id);
	printf("Version:                     %d.%d.%d\n",
	    (cdata->ver >> 16) & 0xffff, (cdata->ver >> 8) & 0xff,
	    cdata->ver & 0xff);
	printf("\n");

	printf("Admin Command Set Attributes\n");
	printf("============================\n");
	printf("Security Send/Receive:       %s\n",
		security ? "Supported" : "Not Supported");
	printf("Format NVM:                  %s\n",
		fmt ? "Supported" : "Not Supported");
	printf("Firmware Activate/Download:  %s\n",
		fw ? "Supported" : "Not Supported");
	printf("Namespace Managment:         %s\n",
		nsmgmt ? "Supported" : "Not Supported");
	printf("Device Self-test:            %sSupported\n",
	    ((oacs >> NVME_CTRLR_DATA_OACS_SELFTEST_SHIFT) &
	     NVME_CTRLR_DATA_OACS_SELFTEST_MASK) ? "" : "Not ");
	printf("Directives:                  %sSupported\n",
	    ((oacs >> NVME_CTRLR_DATA_OACS_DIRECTIVES_SHIFT) &
	     NVME_CTRLR_DATA_OACS_DIRECTIVES_MASK) ? "" : "Not ");
	printf("NVMe-MI Send/Receive:        %sSupported\n",
	    ((oacs >> NVME_CTRLR_DATA_OACS_NVMEMI_SHIFT) &
	     NVME_CTRLR_DATA_OACS_NVMEMI_MASK) ? "" : "Not ");
	printf("Virtualization Management:   %sSupported\n",
	    ((oacs >> NVME_CTRLR_DATA_OACS_VM_SHIFT) &
	     NVME_CTRLR_DATA_OACS_VM_MASK) ? "" : "Not ");
	printf("Doorbell Buffer Config       %sSupported\n",
	    ((oacs >> NVME_CTRLR_DATA_OACS_DBBUFFER_SHIFT) &
	     NVME_CTRLR_DATA_OACS_DBBUFFER_MASK) ? "" : "Not ");
	printf("Abort Command Limit:         %d\n", cdata->acl+1);
	printf("Async Event Request Limit:   %d\n", cdata->aerl+1);
	printf("Number of Firmware Slots:    ");
	if (fw != 0)
		printf("%d\n", fw_num_slots);
	else
		printf("N/A\n");
	printf("Firmware Slot 1 Read-Only:   ");
	if (fw != 0)
		printf("%s\n", fw_slot1_ro ? "Yes" : "No");
	else
		printf("N/A\n");
	printf("Per-Namespace SMART Log:     %s\n",
		ns_smart ? "Yes" : "No");
	printf("Error Log Page Entries:      %d\n", cdata->elpe+1);
	printf("Number of Power States:      %d\n", cdata->npss+1);

	printf("\n");
	printf("NVM Command Set Attributes\n");
	printf("==========================\n");
	printf("Submission Queue Entry Size\n");
	printf("  Max:                       %d\n", 1 << sqes_max);
	printf("  Min:                       %d\n", 1 << sqes_min);
	printf("Completion Queue Entry Size\n");
	printf("  Max:                       %d\n", 1 << cqes_max);
	printf("  Min:                       %d\n", 1 << cqes_min);
	printf("Number of Namespaces:        %d\n", cdata->nn);
	printf("Compare Command:             %s\n",
		compare ? "Supported" : "Not Supported");
	printf("Write Uncorrectable Command: %s\n",
		write_unc ? "Supported" : "Not Supported");
	printf("Dataset Management Command:  %s\n",
		dsm ? "Supported" : "Not Supported");
	printf("Write Zeroes Command:        %sSupported\n",
	    ((oncs >> NVME_CTRLR_DATA_ONCS_WRZERO_SHIFT) &
	     NVME_CTRLR_DATA_ONCS_WRZERO_MASK) ? "" : "Not ");
	printf("Save Features:               %sSupported\n",
	    ((oncs >> NVME_CTRLR_DATA_ONCS_SAVEFEAT_SHIFT) &
	     NVME_CTRLR_DATA_ONCS_SAVEFEAT_MASK) ? "" : "Not ");
	printf("Reservations:                %sSupported\n",
	    ((oncs >> NVME_CTRLR_DATA_ONCS_RESERV_SHIFT) &
	     NVME_CTRLR_DATA_ONCS_RESERV_MASK) ? "" : "Not ");
	printf("Timestamp feature:           %sSupported\n",
	    ((oncs >> NVME_CTRLR_DATA_ONCS_TIMESTAMP_SHIFT) &
	     NVME_CTRLR_DATA_ONCS_TIMESTAMP_MASK) ? "" : "Not ");
	printf("Fused Operation Support:     %s%s\n",
	    (cdata->fuses == 0) ? "Not Supported" : "",
	    ((cdata->fuses >> NVME_CTRLR_DATA_FUSES_CNW_SHIFT) &
	     NVME_CTRLR_DATA_FUSES_CNW_MASK) ? "Compare and Write" : "");
	printf("Format NVM Attributes:       %s%s Erase, %s Format\n",
	    ((cdata->fna >> NVME_CTRLR_DATA_FNA_CRYPTO_ERASE_SHIFT) &
	     NVME_CTRLR_DATA_FNA_CRYPTO_ERASE_MASK) ? "Crypto Erase, " : "",
	    ((cdata->fna >> NVME_CTRLR_DATA_FNA_ERASE_ALL_SHIFT) &
	     NVME_CTRLR_DATA_FNA_ERASE_ALL_MASK) ? "All-NVM" : "Per-NS",
	    ((cdata->fna >> NVME_CTRLR_DATA_FNA_FORMAT_ALL_SHIFT) &
	     NVME_CTRLR_DATA_FNA_FORMAT_ALL_MASK) ? "All-NVM" : "Per-NS");
	printf("Volatile Write Cache:        %s\n",
		vwc_present ? "Present" : "Not Present");

	if (nsmgmt) {
		printf("\n");
		printf("Namespace Drive Attributes\n");
		printf("==========================\n");
		printf("NVM total cap:               %s\n",
			   uint128_to_str(to128(cdata->untncap.tnvmcap), cbuf, sizeof(cbuf)));
		printf("NVM unallocated cap:         %s\n",
			   uint128_to_str(to128(cdata->untncap.unvmcap), cbuf, sizeof(cbuf)));
	}
}
