/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2013 Emulex
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Emulex Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * freebsd-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

/* $FreeBSD$ */

#include "oce_if.h"

static void copy_stats_to_sc_xe201(POCE_SOFTC sc);
static void copy_stats_to_sc_be3(POCE_SOFTC sc);
static void copy_stats_to_sc_be2(POCE_SOFTC sc);
static void copy_stats_to_sc_sh(POCE_SOFTC sc);
static int  oce_sysctl_loopback(SYSCTL_HANDLER_ARGS);
static int  oce_sys_aic_enable(SYSCTL_HANDLER_ARGS);
static int  oce_be3_fwupgrade(POCE_SOFTC sc, const struct firmware *fw);
static int oce_skyhawk_fwupgrade(POCE_SOFTC sc, const struct firmware *fw);
static int  oce_sys_fwupgrade(SYSCTL_HANDLER_ARGS);
static int  oce_lancer_fwupgrade(POCE_SOFTC sc, const struct firmware *fw);
static int oce_sysctl_sfp_vpd_dump(SYSCTL_HANDLER_ARGS);
static boolean_t oce_phy_flashing_required(POCE_SOFTC sc);
static boolean_t oce_img_flashing_required(POCE_SOFTC sc, const char *p,
				int img_optype, uint32_t img_offset,
				uint32_t img_size, uint32_t hdrs_size);
static void oce_add_stats_sysctls_be3(POCE_SOFTC sc,
				struct sysctl_ctx_list *ctx,
				struct sysctl_oid *stats_node);
static void oce_add_stats_sysctls_xe201(POCE_SOFTC sc,
				struct sysctl_ctx_list *ctx,
				struct sysctl_oid *stats_node);


extern char component_revision[32];
uint8_t sfp_vpd_dump_buffer[TRANSCEIVER_DATA_SIZE];

struct flash_img_attri {
	int img_offset;
	int img_size;
	int img_type;
	bool skip_image;
	int optype;
};

void
oce_add_sysctls(POCE_SOFTC sc)
{

	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->dev);
	struct sysctl_oid_list *child = SYSCTL_CHILDREN(tree);
	struct sysctl_oid *stats_node;

	SYSCTL_ADD_STRING(ctx, child,
			OID_AUTO, "component_revision",
			CTLFLAG_RD,
			component_revision,
			sizeof(component_revision),
			"EMULEX One-Connect device driver revision");

	SYSCTL_ADD_STRING(ctx, child,
			OID_AUTO, "firmware_version",
			CTLFLAG_RD,
			sc->fw_version,
			sizeof(sc->fw_version),
			"EMULEX One-Connect Firmware Version");

	SYSCTL_ADD_INT(ctx, child,
			OID_AUTO, "max_rsp_handled",
			CTLFLAG_RW,
			&oce_max_rsp_handled,
			sizeof(oce_max_rsp_handled),
			"Maximum receive frames handled per interupt");

	if ((sc->function_mode & FNM_FLEX10_MODE) || 
	    (sc->function_mode & FNM_UMC_MODE))
		SYSCTL_ADD_UINT(ctx, child,
				OID_AUTO, "speed",
				CTLFLAG_RD,
				&sc->qos_link_speed,
				0,"QOS Speed");
	else
		SYSCTL_ADD_UINT(ctx, child,
				OID_AUTO, "speed",
				CTLFLAG_RD,
				&sc->speed,
				0,"Link Speed");

	if (sc->function_mode & FNM_UMC_MODE)
		SYSCTL_ADD_UINT(ctx, child,
				OID_AUTO, "pvid",
				CTLFLAG_RD,
				&sc->pvid,
				0,"PVID");

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "loop_back",
		CTLTYPE_INT | CTLFLAG_RW, (void *)sc, 0,
		oce_sysctl_loopback, "I", "Loop Back Tests");

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "fw_upgrade",
		CTLTYPE_STRING | CTLFLAG_RW, (void *)sc, 0,
		oce_sys_fwupgrade, "A", "Firmware ufi file");

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "aic_enable",
		CTLTYPE_INT | CTLFLAG_RW, (void *)sc, 1,
		oce_sys_aic_enable, "I", "aic flags");

        /*
         *  Dumps Transceiver data
	 *  "sysctl dev.oce.0.sfp_vpd_dump=0"
         *  "sysctl -x dev.oce.0.sfp_vpd_dump_buffer" for hex dump
         *  "sysctl -b dev.oce.0.sfp_vpd_dump_buffer > sfp.bin" for binary dump
         */
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "sfp_vpd_dump",
			CTLTYPE_INT | CTLFLAG_RW, (void *)sc, 0, oce_sysctl_sfp_vpd_dump,
			"I", "Initiate a sfp_vpd_dump operation");
	SYSCTL_ADD_OPAQUE(ctx, child, OID_AUTO, "sfp_vpd_dump_buffer",
			CTLFLAG_RD, sfp_vpd_dump_buffer,
			TRANSCEIVER_DATA_SIZE, "IU", "Access sfp_vpd_dump buffer");

	stats_node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "stats",
				CTLFLAG_RD, NULL, "Ethernet Statistics");

	if (IS_BE(sc) || IS_SH(sc))
		oce_add_stats_sysctls_be3(sc, ctx, stats_node);
	else
		oce_add_stats_sysctls_xe201(sc, ctx, stats_node);


}


static uint32_t
oce_loopback_test(struct oce_softc *sc, uint8_t loopback_type)
{
	uint32_t status = 0;

	oce_mbox_cmd_set_loopback(sc, sc->port_id, loopback_type, 1);
	status = oce_mbox_cmd_test_loopback(sc, sc->port_id, loopback_type,
				1500, 2, 0xabc);
	oce_mbox_cmd_set_loopback(sc, sc->port_id, OCE_NO_LOOPBACK, 1);

	return status;
}

static int
oce_sys_aic_enable(SYSCTL_HANDLER_ARGS)
{
	int value = 0;
	uint32_t status, vector;
	POCE_SOFTC sc = (struct oce_softc *)arg1;
	struct oce_aic_obj *aic;

	/* set current value for proper sysctl logging */
	value = sc->aic_obj[0].enable;
	status = sysctl_handle_int(oidp, &value, 0, req);
	if (status || !req->newptr)
		return status; 

	for (vector = 0; vector < sc->intr_count; vector++) {
		aic = &sc->aic_obj[vector];

		if (value == 0){
			aic->max_eqd = aic->min_eqd = aic->et_eqd = 0;
			aic->enable = 0;
		}
		else {
			aic->max_eqd = OCE_MAX_EQD;
			aic->min_eqd = OCE_MIN_EQD;
			aic->et_eqd = OCE_MIN_EQD;
			aic->enable = TRUE;
		}
	}
	return 0;
}

static int
oce_sysctl_loopback(SYSCTL_HANDLER_ARGS)
{
	int value = 0;
	uint32_t status;  
	struct oce_softc *sc  = (struct oce_softc *)arg1;

	status = sysctl_handle_int(oidp, &value, 0, req);
	if (status || !req->newptr)
		return status; 

	if (value != 1) {
		device_printf(sc->dev,
			"Not a Valid value. Set to loop_back=1 to run tests\n");
		return 0;
	}

	if ((status = oce_loopback_test(sc, OCE_MAC_LOOPBACK))) {
		device_printf(sc->dev,
			"MAC Loopback Test = Failed (Error status = %d)\n",
			 status);
	} else
		device_printf(sc->dev, "MAC Loopback Test = Success\n");

	if ((status = oce_loopback_test(sc, OCE_PHY_LOOPBACK))) {
		device_printf(sc->dev,
			"PHY Loopback Test = Failed (Error status = %d)\n",
			 status);
	} else
		device_printf(sc->dev, "PHY Loopback Test = Success\n");

	if ((status = oce_loopback_test(sc, OCE_ONE_PORT_EXT_LOOPBACK))) {
		device_printf(sc->dev,
			"EXT Loopback Test = Failed (Error status = %d)\n",
			 status);
	} else
		device_printf(sc->dev, "EXT Loopback Test = Success\n");

	return 0;
}


static int
oce_sys_fwupgrade(SYSCTL_HANDLER_ARGS)
{
	char ufiname[256] = {0};
	uint32_t status = 1;
	struct oce_softc *sc  = (struct oce_softc *)arg1;
	const struct firmware *fw;

	status = sysctl_handle_string(oidp, ufiname, sizeof(ufiname), req);
	if (status || !req->newptr)
		return status;

	fw = firmware_get(ufiname);
	if (fw == NULL) {
		device_printf(sc->dev, "Unable to get Firmware. "
			"Make sure %s is copied to /boot/modules\n", ufiname);
		return ENOENT;
	}

	if (IS_BE(sc)) {
		if ((sc->flags & OCE_FLAGS_BE2)) {
			device_printf(sc->dev, 
				"Flashing not supported for BE2 yet.\n");
			status = 1;
			goto done;
		}
		status = oce_be3_fwupgrade(sc, fw);
	} else if (IS_SH(sc)) {
		status = oce_skyhawk_fwupgrade(sc,fw);
	} else
		status = oce_lancer_fwupgrade(sc, fw);
done:
	if (status) {
		device_printf(sc->dev, "Firmware Upgrade failed\n");
	} else {
		device_printf(sc->dev, "Firmware Flashed successfully\n");
	}

	/* Release Firmware*/
	firmware_put(fw, FIRMWARE_UNLOAD);

	return status;
}

static void oce_fill_flash_img_data(POCE_SOFTC sc, const struct flash_sec_info * fsec,
				struct flash_img_attri *pimg, int i,
				const struct firmware *fw, int bin_offset)
{
	if (IS_SH(sc)) {
		pimg->img_offset = HOST_32(fsec->fsec_entry[i].offset);
		pimg->img_size   = HOST_32(fsec->fsec_entry[i].pad_size);
	}

	pimg->img_type = HOST_32(fsec->fsec_entry[i].type);
	pimg->skip_image = FALSE;
	switch (pimg->img_type) {
		case IMG_ISCSI:
			pimg->optype = 0;
			if (IS_BE3(sc)) {
				pimg->img_offset = 2097152;
				pimg->img_size   = 2097152;
			}
			break;
		case IMG_REDBOOT:
			pimg->optype = 1;
			if (IS_BE3(sc)) {
				pimg->img_offset = 262144;
				pimg->img_size   = 1048576;
			}
			if (!oce_img_flashing_required(sc, fw->data,
						pimg->optype,
						pimg->img_offset,
						pimg->img_size,
						bin_offset))
				pimg->skip_image = TRUE;
			break;
		case IMG_BIOS:
			pimg->optype = 2;
			if (IS_BE3(sc)) {
				pimg->img_offset = 12582912;
				pimg->img_size   = 524288;
			}
			break;
		case IMG_PXEBIOS:
			pimg->optype = 3;
			if (IS_BE3(sc)) {
				pimg->img_offset =  13107200;
				pimg->img_size   = 524288;
			}
			break;
		case IMG_FCOEBIOS:
			pimg->optype = 8;
			if (IS_BE3(sc)) {
				pimg->img_offset = 13631488;
				pimg->img_size   = 524288;
			}
			break;
		case IMG_ISCSI_BAK:
			pimg->optype = 9;
			if (IS_BE3(sc)) {
				pimg->img_offset = 4194304;
				pimg->img_size   = 2097152;
			}
			break;
		case IMG_FCOE:
			pimg->optype = 10;
			if (IS_BE3(sc)) {
				pimg->img_offset = 6291456;
				pimg->img_size   = 2097152;
			}
			break;
		case IMG_FCOE_BAK:
			pimg->optype = 11;
			if (IS_BE3(sc)) {
				pimg->img_offset = 8388608;
				pimg->img_size   = 2097152;
			}
			break;
		case IMG_NCSI:
			pimg->optype = 13;
			if (IS_BE3(sc)) {
				pimg->img_offset = 15990784;
				pimg->img_size   = 262144;
			}
			break;
		case IMG_PHY:
			pimg->optype = 99;
			if (IS_BE3(sc)) {
				pimg->img_offset = 1310720;
				pimg->img_size   = 262144;
			}
			if (!oce_phy_flashing_required(sc))
				pimg->skip_image = TRUE;
			break;
		default:
			pimg->skip_image = TRUE;
			break;
	}

}

static int
oce_sh_be3_flashdata(POCE_SOFTC sc, const struct firmware *fw, int32_t num_imgs)
{
	char cookie[2][16] =    {"*** SE FLAS", "H DIRECTORY *** "};
	const char *p = (const char *)fw->data;
	const struct flash_sec_info *fsec = NULL;
	struct mbx_common_read_write_flashrom *req;
	int rc = 0, i, bin_offset = 0, opcode, num_bytes;
	OCE_DMA_MEM dma_mem;
	struct flash_img_attri imgatt;

	/* Validate Cookie */
	bin_offset = (sizeof(struct flash_file_hdr) +
			(num_imgs * sizeof(struct image_hdr)));
	p += bin_offset;
	while (p < ((const char *)fw->data + fw->datasize)) {
		fsec = (const struct flash_sec_info *)p;
		if (!memcmp(cookie, fsec->cookie, sizeof(cookie)))
			break;
		fsec = NULL;
		p += 32;
	}

	if (!fsec) {
		device_printf(sc->dev,
				"Invalid Cookie. Firmware image corrupted ?\n");
		return EINVAL;
	}

	rc = oce_dma_alloc(sc, sizeof(struct mbx_common_read_write_flashrom),
				&dma_mem, 0);
	if (rc) {
		device_printf(sc->dev,
				"Memory allocation failure while flashing\n");
		return ENOMEM;
	}
	req = OCE_DMAPTR(&dma_mem, struct mbx_common_read_write_flashrom);

	if (IS_SH(sc))
		num_imgs = HOST_32(fsec->fsec_hdr.num_images);
	else if (IS_BE3(sc))
		num_imgs = MAX_FLASH_COMP;

	for (i = 0; i < num_imgs; i++) {

		bzero(&imgatt, sizeof(struct flash_img_attri));

		oce_fill_flash_img_data(sc, fsec, &imgatt, i, fw, bin_offset);

		if (imgatt.skip_image)
			continue;

		p = fw->data;
		p = p + bin_offset + imgatt.img_offset;

		if ((p + imgatt.img_size) > ((const char *)fw->data + fw->datasize)) {
			rc = 1;
			goto ret;
		}

		while (imgatt.img_size) {

			if (imgatt.img_size > 32*1024)
				num_bytes = 32*1024;
			else
				num_bytes = imgatt.img_size;
			imgatt.img_size -= num_bytes;

			if (!imgatt.img_size)
				opcode = FLASHROM_OPER_FLASH;
			else
				opcode = FLASHROM_OPER_SAVE;

			memcpy(req->data_buffer, p, num_bytes);
			p += num_bytes;

			rc = oce_mbox_write_flashrom(sc, imgatt.optype, opcode,
					&dma_mem, num_bytes);
			if (rc) {
				device_printf(sc->dev,
						"cmd to write to flash rom failed.\n");
				rc = EIO;
				goto ret;
			}
			/* Leave the CPU for others for some time */
			pause("yield", 10);

		}

	}

ret:
	oce_dma_free(sc, &dma_mem);
	return rc;
}

#define UFI_TYPE2               2
#define UFI_TYPE3               3
#define UFI_TYPE3R              10
#define UFI_TYPE4               4
#define UFI_TYPE4R              11
static int oce_get_ufi_type(POCE_SOFTC sc,
                           const struct flash_file_hdr *fhdr)
{
        if (fhdr == NULL)
                goto be_get_ufi_exit;

        if (IS_SH(sc) && fhdr->build[0] == '4') {
                if (fhdr->asic_type_rev >= 0x10)
                        return UFI_TYPE4R;
                else
                        return UFI_TYPE4;
        } else if (IS_BE3(sc) && fhdr->build[0] == '3') {
                if (fhdr->asic_type_rev == 0x10)
                        return UFI_TYPE3R;
                else
                        return UFI_TYPE3;
        } else if (IS_BE2(sc) && fhdr->build[0] == '2')
                return UFI_TYPE2;

be_get_ufi_exit:
        device_printf(sc->dev,
                "UFI and Interface are not compatible for flashing\n");
        return -1;
}


static int
oce_skyhawk_fwupgrade(POCE_SOFTC sc, const struct firmware *fw)
{
	int rc = 0, num_imgs = 0, i = 0, ufi_type;
	const struct flash_file_hdr *fhdr;
	const struct image_hdr *img_ptr;

	fhdr = (const struct flash_file_hdr *)fw->data;

	ufi_type = oce_get_ufi_type(sc, fhdr);

	/* Display flash version */
	device_printf(sc->dev, "Flashing Firmware %s\n", &fhdr->build[2]);

	num_imgs = fhdr->num_imgs;
	for (i = 0; i < num_imgs; i++) {
		img_ptr = (const struct image_hdr *)((const char *)fw->data +
				sizeof(struct flash_file_hdr) +
				(i * sizeof(struct image_hdr)));

		if (img_ptr->imageid != 1)
			continue;

		switch (ufi_type) {
			case UFI_TYPE4R:
				rc = oce_sh_be3_flashdata(sc, fw,
						num_imgs);
				break;
			case UFI_TYPE4:
				if (sc->asic_revision < 0x10)
					rc = oce_sh_be3_flashdata(sc, fw,
								   num_imgs);
				else {
					rc = -1;
					device_printf(sc->dev,
						"Cant load SH A0 UFI on B0\n");
				}
				break;
			default:
				rc = -1;
				break;

		}
	}

	return rc;
}

static int
oce_be3_fwupgrade(POCE_SOFTC sc, const struct firmware *fw)
{
	int rc = 0, num_imgs = 0, i = 0;
	const struct flash_file_hdr *fhdr;
	const struct image_hdr *img_ptr;

	fhdr = (const struct flash_file_hdr *)fw->data;
	if (fhdr->build[0] != '3') {
		device_printf(sc->dev, "Invalid BE3 firmware image\n");
		return EINVAL;
	}
	/* Display flash version */
	device_printf(sc->dev, "Flashing Firmware %s\n", &fhdr->build[2]);

	num_imgs = fhdr->num_imgs;
	for (i = 0; i < num_imgs; i++) {
		img_ptr = (const struct image_hdr *)((const char *)fw->data +
				sizeof(struct flash_file_hdr) +
				(i * sizeof(struct image_hdr)));
		if (img_ptr->imageid == 1) {
			rc = oce_sh_be3_flashdata(sc, fw, num_imgs);

			break;
		}
	}

	return rc;
}


static boolean_t
oce_phy_flashing_required(POCE_SOFTC sc)
{
	int status = 0;
	struct oce_phy_info phy_info;

	status = oce_mbox_get_phy_info(sc, &phy_info);
	if (status)
		return FALSE;

	if ((phy_info.phy_type == TN_8022) &&
		(phy_info.interface_type == PHY_TYPE_BASET_10GB)) {
		return TRUE;
	}

	return FALSE;
}


static boolean_t
oce_img_flashing_required(POCE_SOFTC sc, const char *p,
				int img_optype, uint32_t img_offset,
				uint32_t img_size, uint32_t hdrs_size)
{
	uint32_t crc_offset;
	uint8_t flashed_crc[4];
	int status;

	crc_offset = hdrs_size + img_offset + img_size - 4;

	p += crc_offset;

	status = oce_mbox_get_flashrom_crc(sc, flashed_crc,
			(img_size - 4), img_optype);
	if (status)
		return TRUE; /* Some thing worng. ReFlash */

	/*update redboot only if crc does not match*/
	if (bcmp(flashed_crc, p, 4))
		return TRUE;
	else
		return FALSE;
}


static int
oce_lancer_fwupgrade(POCE_SOFTC sc, const struct firmware *fw)
{

	int rc = 0;
	OCE_DMA_MEM dma_mem;
	const uint8_t *data = NULL;
	uint8_t *dest_image_ptr = NULL;
	size_t size = 0;
	uint32_t data_written = 0, chunk_size = 0;
	uint32_t offset = 0, add_status = 0;

	if (!IS_ALIGNED(fw->datasize, sizeof(uint32_t))) {
		device_printf(sc->dev,
			"Lancer FW image is not 4 byte aligned.");
		return EINVAL;
	}

	rc = oce_dma_alloc(sc, 32*1024, &dma_mem, 0);
	if (rc) {
		device_printf(sc->dev,
			"Memory allocation failure while flashing Lancer\n");
		return ENOMEM;
	}

	size = fw->datasize;
	data = fw->data;
	dest_image_ptr = OCE_DMAPTR(&dma_mem, uint8_t);

	while (size) {
		chunk_size = MIN(size, (32*1024));

		bcopy(data, dest_image_ptr, chunk_size);

		rc = oce_mbox_lancer_write_flashrom(sc, chunk_size, offset,
				&dma_mem, &data_written, &add_status);

		if (rc)
			break;

		size	-= data_written;
		data	+= data_written;
		offset	+= data_written;
		pause("yield", 10);

	}

	if (!rc)
		/* Commit the firmware*/
		rc = oce_mbox_lancer_write_flashrom(sc, 0, offset, &dma_mem,
						&data_written, &add_status);
	if (rc) {
		device_printf(sc->dev, "Lancer firmware load error. "
			"Addstatus = 0x%x, status = %d \n", add_status, rc);
		rc = EIO;
	}
	oce_dma_free(sc, &dma_mem);
	return rc;

}


static void
oce_add_stats_sysctls_be3(POCE_SOFTC sc,
				  struct sysctl_ctx_list *ctx,
				  struct sysctl_oid *stats_node)
{
	struct sysctl_oid *rx_stats_node, *tx_stats_node;
	struct sysctl_oid_list *rx_stat_list, *tx_stat_list;
	struct sysctl_oid_list *queue_stats_list;
	struct sysctl_oid *queue_stats_node;
	struct oce_drv_stats *stats;
	char prefix[32];
	int i;

	stats = &sc->oce_stats_info;

	rx_stats_node = SYSCTL_ADD_NODE(ctx,
					SYSCTL_CHILDREN(stats_node), 
					OID_AUTO,"rx", CTLFLAG_RD, 
					NULL, "RX Ethernet Statistics");
	rx_stat_list = SYSCTL_CHILDREN(rx_stats_node);

	
	SYSCTL_ADD_QUAD(ctx, rx_stat_list, OID_AUTO, "total_pkts",
			CTLFLAG_RD, &stats->rx.t_rx_pkts,
			"Total Received Packets");
	SYSCTL_ADD_QUAD(ctx, rx_stat_list, OID_AUTO, "total_bytes",
			CTLFLAG_RD, &stats->rx.t_rx_bytes,
			"Total Received Bytes");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "total_frags",
			CTLFLAG_RD, &stats->rx.t_rx_frags, 0,
			"Total Received Fragements");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "total_mcast_pkts",
			CTLFLAG_RD, &stats->rx.t_rx_mcast_pkts, 0,
			"Total Received Multicast Packets");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "total_ucast_pkts",
			CTLFLAG_RD, &stats->rx.t_rx_ucast_pkts, 0,
			"Total Received Unicast Packets");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "total_rxcp_errs",
			CTLFLAG_RD, &stats->rx.t_rxcp_errs, 0,
			"Total Receive completion errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "pause_frames",
			CTLFLAG_RD, &stats->u0.be.rx_pause_frames, 0,
			"Pause Frames");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "priority_pause_frames",
			CTLFLAG_RD, &stats->u0.be.rx_priority_pause_frames, 0,
			"Priority Pause Frames");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "control_frames",
			CTLFLAG_RD, &stats->u0.be.rx_control_frames, 0,
			"Control Frames");
	
	for (i = 0; i < sc->nrqs; i++) {
		sprintf(prefix, "queue%d",i);
		queue_stats_node = SYSCTL_ADD_NODE(ctx, 
						SYSCTL_CHILDREN(rx_stats_node),
						OID_AUTO, prefix, CTLFLAG_RD,
						NULL, "Queue name");
		queue_stats_list = SYSCTL_CHILDREN(queue_stats_node);
		
		SYSCTL_ADD_QUAD(ctx, queue_stats_list, OID_AUTO, "rx_pkts",
			CTLFLAG_RD, &sc->rq[i]->rx_stats.rx_pkts,
			"Receive Packets");
		SYSCTL_ADD_QUAD(ctx, queue_stats_list, OID_AUTO, "rx_bytes",
			CTLFLAG_RD, &sc->rq[i]->rx_stats.rx_bytes,
			"Recived Bytes");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO, "rx_frags",
			CTLFLAG_RD, &sc->rq[i]->rx_stats.rx_frags, 0,
			"Received Fragments");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO,
				"rx_mcast_pkts", CTLFLAG_RD,
				&sc->rq[i]->rx_stats.rx_mcast_pkts, 0,
					"Received Multicast Packets");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO,
				"rx_ucast_pkts", CTLFLAG_RD, 
				&sc->rq[i]->rx_stats.rx_ucast_pkts, 0,
					"Received Unicast Packets");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO, "rxcp_err",
			CTLFLAG_RD, &sc->rq[i]->rx_stats.rxcp_err, 0,
			"Received Completion Errors");
		if(IS_SH(sc)) {
			SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO, "rx_drops_no_frags",
                        	CTLFLAG_RD, &sc->rq[i]->rx_stats.rx_drops_no_frags, 0,
                        	"num of packet drops due to no fragments");
		}
	}
	
	rx_stats_node = SYSCTL_ADD_NODE(ctx,
					SYSCTL_CHILDREN(rx_stats_node),
					OID_AUTO, "err", CTLFLAG_RD,
					NULL, "Receive Error Stats");
	rx_stat_list = SYSCTL_CHILDREN(rx_stats_node);
	
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "crc_errs",
			CTLFLAG_RD, &stats->u0.be.rx_crc_errors, 0,
			"CRC Errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "pbuf_errors",
			CTLFLAG_RD, &stats->u0.be.rx_drops_no_pbuf, 0,
			"Drops due to pbuf full");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "erx_errors",
			CTLFLAG_RD, &stats->u0.be.rx_drops_no_erx_descr, 0,
			"ERX Errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "alignment_errors",
			CTLFLAG_RD, &stats->u0.be.rx_drops_too_many_frags, 0,
			"RX Alignmnet Errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "in_range_errors",
			CTLFLAG_RD, &stats->u0.be.rx_in_range_errors, 0,
			"In Range Errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "out_range_errors",
			CTLFLAG_RD, &stats->u0.be.rx_out_range_errors, 0,
			"Out Range Errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "frame_too_long",
			CTLFLAG_RD, &stats->u0.be.rx_frame_too_long, 0,
			"Frame Too Long");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "address_match_errors",
			CTLFLAG_RD, &stats->u0.be.rx_address_match_errors, 0,
			"Address Match Errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "dropped_too_small",
			CTLFLAG_RD, &stats->u0.be.rx_dropped_too_small, 0,
			"Dropped Too Small");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "dropped_too_short",
			CTLFLAG_RD, &stats->u0.be.rx_dropped_too_short, 0,
			"Dropped Too Short");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO,
			"dropped_header_too_small", CTLFLAG_RD,
			&stats->u0.be.rx_dropped_header_too_small, 0,
			"Dropped Header Too Small");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "dropped_tcp_length",
			CTLFLAG_RD, &stats->u0.be.rx_dropped_tcp_length, 0,
			"Dropped TCP Length");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "dropped_runt",
			CTLFLAG_RD, &stats->u0.be.rx_dropped_runt, 0,
			"Dropped runt");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "ip_checksum_errs",
			CTLFLAG_RD, &stats->u0.be.rx_ip_checksum_errs, 0,
			"IP Checksum Errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "tcp_checksum_errs",
			CTLFLAG_RD, &stats->u0.be.rx_tcp_checksum_errs, 0,
			"TCP Checksum Errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "udp_checksum_errs",
			CTLFLAG_RD, &stats->u0.be.rx_udp_checksum_errs, 0,
			"UDP Checksum Errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "fifo_overflow_drop",
			CTLFLAG_RD, &stats->u0.be.rxpp_fifo_overflow_drop, 0,
			"FIFO Overflow Drop");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO,
			"input_fifo_overflow_drop", CTLFLAG_RD,
			&stats->u0.be.rx_input_fifo_overflow_drop, 0,
			"Input FIFO Overflow Drop");

	tx_stats_node = SYSCTL_ADD_NODE(ctx,
					SYSCTL_CHILDREN(stats_node), OID_AUTO,
					"tx",CTLFLAG_RD, NULL,
					"TX Ethernet Statistics");
	tx_stat_list = SYSCTL_CHILDREN(tx_stats_node);

	SYSCTL_ADD_QUAD(ctx, tx_stat_list, OID_AUTO, "total_tx_pkts",
			CTLFLAG_RD, &stats->tx.t_tx_pkts,
			"Total Transmit Packets");
	SYSCTL_ADD_QUAD(ctx, tx_stat_list, OID_AUTO, "total_tx_bytes",
			CTLFLAG_RD, &stats->tx.t_tx_bytes,
			"Total Transmit Bytes");
	SYSCTL_ADD_UINT(ctx, tx_stat_list, OID_AUTO, "total_tx_reqs",
			CTLFLAG_RD, &stats->tx.t_tx_reqs, 0,
			"Total Transmit Requests");
	SYSCTL_ADD_UINT(ctx, tx_stat_list, OID_AUTO, "total_tx_stops",
			CTLFLAG_RD, &stats->tx.t_tx_stops, 0,
			"Total Transmit Stops");
	SYSCTL_ADD_UINT(ctx, tx_stat_list, OID_AUTO, "total_tx_wrbs",
			CTLFLAG_RD, &stats->tx.t_tx_wrbs, 0,
			"Total Transmit WRB's");
	SYSCTL_ADD_UINT(ctx, tx_stat_list, OID_AUTO, "total_tx_compl",
			CTLFLAG_RD, &stats->tx.t_tx_compl, 0,
			"Total Transmit Completions");
	SYSCTL_ADD_UINT(ctx, tx_stat_list, OID_AUTO,
			"total_ipv6_ext_hdr_tx_drop", CTLFLAG_RD,
			&stats->tx.t_ipv6_ext_hdr_tx_drop, 0,
			"Total Transmit IPV6 Drops");
	SYSCTL_ADD_UINT(ctx, tx_stat_list, OID_AUTO, "pauseframes",
			CTLFLAG_RD, &stats->u0.be.tx_pauseframes, 0,
			"Pause Frames");
	SYSCTL_ADD_UINT(ctx, tx_stat_list, OID_AUTO, "priority_pauseframes",
			CTLFLAG_RD, &stats->u0.be.tx_priority_pauseframes, 0,
			"Priority Pauseframes");
	SYSCTL_ADD_UINT(ctx, tx_stat_list, OID_AUTO, "controlframes",
			CTLFLAG_RD, &stats->u0.be.tx_controlframes, 0,
			"Tx Control Frames");

	for (i = 0; i < sc->nwqs; i++) {
		sprintf(prefix, "queue%d",i);
		queue_stats_node = SYSCTL_ADD_NODE(ctx, 
						SYSCTL_CHILDREN(tx_stats_node),
						OID_AUTO, prefix, CTLFLAG_RD,
						NULL, "Queue name");
		queue_stats_list = SYSCTL_CHILDREN(queue_stats_node);

		SYSCTL_ADD_QUAD(ctx, queue_stats_list, OID_AUTO, "tx_pkts",
			CTLFLAG_RD, &sc->wq[i]->tx_stats.tx_pkts,
			"Transmit Packets");
		SYSCTL_ADD_QUAD(ctx, queue_stats_list, OID_AUTO, "tx_bytes",
			CTLFLAG_RD, &sc->wq[i]->tx_stats.tx_bytes,
			"Transmit Bytes");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO, "tx_reqs",
			CTLFLAG_RD, &sc->wq[i]->tx_stats.tx_reqs, 0,
			"Transmit Requests");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO, "tx_stops",
			CTLFLAG_RD, &sc->wq[i]->tx_stats.tx_stops, 0,
			"Transmit Stops");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO, "tx_wrbs",
			CTLFLAG_RD, &sc->wq[i]->tx_stats.tx_wrbs, 0,
			"Transmit WRB's");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO, "tx_compl",
			CTLFLAG_RD, &sc->wq[i]->tx_stats.tx_compl, 0,
			"Transmit Completions");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO,
			"ipv6_ext_hdr_tx_drop",CTLFLAG_RD,
			&sc->wq[i]->tx_stats.ipv6_ext_hdr_tx_drop, 0,
			"Transmit IPV6 Ext Header Drop");

	}
	return;
}


static void
oce_add_stats_sysctls_xe201(POCE_SOFTC sc,
				  struct sysctl_ctx_list *ctx,
				  struct sysctl_oid *stats_node)
{
	struct sysctl_oid *rx_stats_node, *tx_stats_node;
	struct sysctl_oid_list *rx_stat_list, *tx_stat_list;
	struct sysctl_oid_list *queue_stats_list;
	struct sysctl_oid *queue_stats_node;
	struct oce_drv_stats *stats;
	char prefix[32];
	int i;

	stats = &sc->oce_stats_info;

	rx_stats_node = SYSCTL_ADD_NODE(ctx,
					SYSCTL_CHILDREN(stats_node),
					OID_AUTO, "rx", CTLFLAG_RD,
					NULL, "RX Ethernet Statistics");
	rx_stat_list = SYSCTL_CHILDREN(rx_stats_node);

	
	SYSCTL_ADD_QUAD(ctx, rx_stat_list, OID_AUTO, "total_pkts",
			CTLFLAG_RD, &stats->rx.t_rx_pkts,
			"Total Received Packets");
	SYSCTL_ADD_QUAD(ctx, rx_stat_list, OID_AUTO, "total_bytes",
			CTLFLAG_RD, &stats->rx.t_rx_bytes,
			"Total Received Bytes");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "total_frags",
			CTLFLAG_RD, &stats->rx.t_rx_frags, 0,
			"Total Received Fragements");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "total_mcast_pkts",
			CTLFLAG_RD, &stats->rx.t_rx_mcast_pkts, 0,
			"Total Received Multicast Packets");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "total_ucast_pkts",
			CTLFLAG_RD, &stats->rx.t_rx_ucast_pkts, 0,
			"Total Received Unicast Packets");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "total_rxcp_errs",
			CTLFLAG_RD, &stats->rx.t_rxcp_errs, 0,
			"Total Receive completion errors");
	SYSCTL_ADD_UQUAD(ctx, rx_stat_list, OID_AUTO, "pause_frames",
			CTLFLAG_RD, &stats->u0.xe201.rx_pause_frames,
			"Pause Frames");
	SYSCTL_ADD_UQUAD(ctx, rx_stat_list, OID_AUTO, "control_frames",
			CTLFLAG_RD, &stats->u0.xe201.rx_control_frames,
			"Control Frames");
	
	for (i = 0; i < sc->nrqs; i++) {
		sprintf(prefix, "queue%d",i);
		queue_stats_node = SYSCTL_ADD_NODE(ctx, 
						SYSCTL_CHILDREN(rx_stats_node),
						OID_AUTO, prefix, CTLFLAG_RD,
						NULL, "Queue name");
		queue_stats_list = SYSCTL_CHILDREN(queue_stats_node);
		
		SYSCTL_ADD_QUAD(ctx, queue_stats_list, OID_AUTO, "rx_pkts",
			CTLFLAG_RD, &sc->rq[i]->rx_stats.rx_pkts,
			"Receive Packets");
		SYSCTL_ADD_QUAD(ctx, queue_stats_list, OID_AUTO, "rx_bytes",
			CTLFLAG_RD, &sc->rq[i]->rx_stats.rx_bytes,
			"Recived Bytes");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO, "rx_frags",
			CTLFLAG_RD, &sc->rq[i]->rx_stats.rx_frags, 0,
			"Received Fragments");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO,
			"rx_mcast_pkts", CTLFLAG_RD,
			&sc->rq[i]->rx_stats.rx_mcast_pkts, 0,
			"Received Multicast Packets");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO,
			"rx_ucast_pkts",CTLFLAG_RD,
			&sc->rq[i]->rx_stats.rx_ucast_pkts, 0,
			"Received Unicast Packets");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO, "rxcp_err",
			CTLFLAG_RD, &sc->rq[i]->rx_stats.rxcp_err, 0,
			"Received Completion Errors");
		
	}

	rx_stats_node = SYSCTL_ADD_NODE(ctx,
					SYSCTL_CHILDREN(rx_stats_node),
					OID_AUTO, "err", CTLFLAG_RD,
					NULL, "Receive Error Stats");
	rx_stat_list = SYSCTL_CHILDREN(rx_stats_node);
	
	SYSCTL_ADD_UQUAD(ctx, rx_stat_list, OID_AUTO, "crc_errs",
			CTLFLAG_RD, &stats->u0.xe201.rx_crc_errors,
			"CRC Errors");
	SYSCTL_ADD_UQUAD(ctx, rx_stat_list, OID_AUTO, "alignment_errors",
			CTLFLAG_RD, &stats->u0.xe201.rx_alignment_errors,
			"RX Alignmnet Errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "in_range_errors",
			CTLFLAG_RD, &stats->u0.xe201.rx_in_range_errors, 0,
			"In Range Errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "out_range_errors",
			CTLFLAG_RD, &stats->u0.xe201.rx_out_of_range_errors, 0,
			"Out Range Errors");
	SYSCTL_ADD_UQUAD(ctx, rx_stat_list, OID_AUTO, "frame_too_long",
			CTLFLAG_RD, &stats->u0.xe201.rx_frames_too_long,
			"Frame Too Long");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "address_match_errors",
			CTLFLAG_RD, &stats->u0.xe201.rx_address_match_errors, 0,
			"Address Match Errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "dropped_too_small",
			CTLFLAG_RD, &stats->u0.xe201.rx_dropped_too_small, 0,
			"Dropped Too Small");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "dropped_too_short",
			CTLFLAG_RD, &stats->u0.xe201.rx_dropped_too_short, 0,
			"Dropped Too Short");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO,
			"dropped_header_too_small", CTLFLAG_RD,
			&stats->u0.xe201.rx_dropped_header_too_small, 0,
			"Dropped Header Too Small");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO,
			"dropped_tcp_length", CTLFLAG_RD,
			&stats->u0.xe201.rx_dropped_invalid_tcp_length, 0,
			"Dropped TCP Length");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "dropped_runt",
			CTLFLAG_RD, &stats->u0.xe201.rx_dropped_runt, 0,
			"Dropped runt");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "ip_checksum_errs",
			CTLFLAG_RD, &stats->u0.xe201.rx_ip_checksum_errors, 0,
			"IP Checksum Errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "tcp_checksum_errs",
			CTLFLAG_RD, &stats->u0.xe201.rx_tcp_checksum_errors, 0,
			"TCP Checksum Errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "udp_checksum_errs",
			CTLFLAG_RD, &stats->u0.xe201.rx_udp_checksum_errors, 0,
			"UDP Checksum Errors");
	SYSCTL_ADD_UINT(ctx, rx_stat_list, OID_AUTO, "input_fifo_overflow_drop",
			CTLFLAG_RD, &stats->u0.xe201.rx_fifo_overflow, 0,
			"Input FIFO Overflow Drop");

	tx_stats_node = SYSCTL_ADD_NODE(ctx,
					SYSCTL_CHILDREN(stats_node),
					OID_AUTO, "tx", CTLFLAG_RD,
					NULL, "TX Ethernet Statistics");
	tx_stat_list = SYSCTL_CHILDREN(tx_stats_node);

	SYSCTL_ADD_QUAD(ctx, tx_stat_list, OID_AUTO, "total_tx_pkts",
			CTLFLAG_RD, &stats->tx.t_tx_pkts,
			"Total Transmit Packets");
	SYSCTL_ADD_QUAD(ctx, tx_stat_list, OID_AUTO, "total_tx_bytes",
			CTLFLAG_RD, &stats->tx.t_tx_bytes,
			"Total Transmit Bytes");
	SYSCTL_ADD_UINT(ctx, tx_stat_list, OID_AUTO, "total_tx_reqs",
			CTLFLAG_RD, &stats->tx.t_tx_reqs, 0,
			"Total Transmit Requests");
	SYSCTL_ADD_UINT(ctx, tx_stat_list, OID_AUTO, "total_tx_stops",
			CTLFLAG_RD, &stats->tx.t_tx_stops, 0,
			"Total Transmit Stops");
	SYSCTL_ADD_UINT(ctx, tx_stat_list, OID_AUTO, "total_tx_wrbs",
			CTLFLAG_RD, &stats->tx.t_tx_wrbs, 0,
			"Total Transmit WRB's");
	SYSCTL_ADD_UINT(ctx, tx_stat_list, OID_AUTO, "total_tx_compl",
			CTLFLAG_RD, &stats->tx.t_tx_compl, 0,
			"Total Transmit Completions");
	SYSCTL_ADD_UINT(ctx, tx_stat_list, OID_AUTO,
			"total_ipv6_ext_hdr_tx_drop",
			CTLFLAG_RD, &stats->tx.t_ipv6_ext_hdr_tx_drop, 0,
			"Total Transmit IPV6 Drops");
	SYSCTL_ADD_UQUAD(ctx, tx_stat_list, OID_AUTO, "pauseframes",
			CTLFLAG_RD, &stats->u0.xe201.tx_pause_frames,
			"Pause Frames");
	SYSCTL_ADD_UQUAD(ctx, tx_stat_list, OID_AUTO, "controlframes",
			CTLFLAG_RD, &stats->u0.xe201.tx_control_frames,
			"Tx Control Frames");

	for (i = 0; i < sc->nwqs; i++) {
		sprintf(prefix, "queue%d",i);
		queue_stats_node = SYSCTL_ADD_NODE(ctx, 
						SYSCTL_CHILDREN(tx_stats_node),
						OID_AUTO, prefix, CTLFLAG_RD,
						NULL, "Queue name");
		queue_stats_list = SYSCTL_CHILDREN(queue_stats_node);

		SYSCTL_ADD_QUAD(ctx, queue_stats_list, OID_AUTO, "tx_pkts",
			CTLFLAG_RD, &sc->wq[i]->tx_stats.tx_pkts,
			"Transmit Packets");
		SYSCTL_ADD_QUAD(ctx, queue_stats_list, OID_AUTO, "tx_bytes",
			CTLFLAG_RD, &sc->wq[i]->tx_stats.tx_bytes,
			"Transmit Bytes");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO, "tx_reqs",
			CTLFLAG_RD, &sc->wq[i]->tx_stats.tx_reqs, 0,
			"Transmit Requests");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO, "tx_stops",
			CTLFLAG_RD, &sc->wq[i]->tx_stats.tx_stops, 0,
			"Transmit Stops");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO, "tx_wrbs",
			CTLFLAG_RD, &sc->wq[i]->tx_stats.tx_wrbs, 0,
			"Transmit WRB's");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO, "tx_compl",
			CTLFLAG_RD, &sc->wq[i]->tx_stats.tx_compl, 0,
			"Transmit Completions");
		SYSCTL_ADD_UINT(ctx, queue_stats_list, OID_AUTO,
			"ipv6_ext_hdr_tx_drop", CTLFLAG_RD,
			&sc->wq[i]->tx_stats.ipv6_ext_hdr_tx_drop, 0,
			"Transmit IPV6 Ext Header Drop");

	}
	return;
}


void 
oce_refresh_queue_stats(POCE_SOFTC sc)
{
	struct oce_drv_stats *adapter_stats;
	int i;

	adapter_stats = &sc->oce_stats_info;
	
	/* Caluculate total TX and TXstats from all queues */
	
	bzero(&adapter_stats->rx, sizeof(struct oce_rx_stats));
	for (i = 0; i < sc->nrqs; i++) {
		
		adapter_stats->rx.t_rx_pkts += sc->rq[i]->rx_stats.rx_pkts;
		adapter_stats->rx.t_rx_bytes += sc->rq[i]->rx_stats.rx_bytes;
		adapter_stats->rx.t_rx_frags += sc->rq[i]->rx_stats.rx_frags;
		adapter_stats->rx.t_rx_mcast_pkts += 
					sc->rq[i]->rx_stats.rx_mcast_pkts;
		adapter_stats->rx.t_rx_ucast_pkts +=
					sc->rq[i]->rx_stats.rx_ucast_pkts;
		adapter_stats->rx.t_rxcp_errs += sc-> rq[i]->rx_stats.rxcp_err;
	}

	bzero(&adapter_stats->tx, sizeof(struct oce_tx_stats));
	for (i = 0; i < sc->nwqs; i++) {
		adapter_stats->tx.t_tx_reqs += sc->wq[i]->tx_stats.tx_reqs;
		adapter_stats->tx.t_tx_stops += sc->wq[i]->tx_stats.tx_stops;
		adapter_stats->tx.t_tx_wrbs += sc->wq[i]->tx_stats.tx_wrbs;
		adapter_stats->tx.t_tx_compl += sc->wq[i]->tx_stats.tx_compl;
		adapter_stats->tx.t_tx_bytes += sc->wq[i]->tx_stats.tx_bytes;
		adapter_stats->tx.t_tx_pkts += sc->wq[i]->tx_stats.tx_pkts;
		adapter_stats->tx.t_ipv6_ext_hdr_tx_drop +=
				sc->wq[i]->tx_stats.ipv6_ext_hdr_tx_drop;
	}

}



static void
copy_stats_to_sc_xe201(POCE_SOFTC sc)
{
	struct oce_xe201_stats *adapter_stats;
	struct mbx_get_pport_stats *nic_mbx;
	struct pport_stats *port_stats;

	nic_mbx = OCE_DMAPTR(&sc->stats_mem, struct mbx_get_pport_stats);
	port_stats = &nic_mbx->params.rsp.pps;
	adapter_stats = &sc->oce_stats_info.u0.xe201;

	adapter_stats->tx_pkts = port_stats->tx_pkts;
	adapter_stats->tx_unicast_pkts = port_stats->tx_unicast_pkts;
	adapter_stats->tx_multicast_pkts = port_stats->tx_multicast_pkts;
	adapter_stats->tx_broadcast_pkts = port_stats->tx_broadcast_pkts;
	adapter_stats->tx_bytes = port_stats->tx_bytes;
	adapter_stats->tx_unicast_bytes = port_stats->tx_unicast_bytes;
	adapter_stats->tx_multicast_bytes = port_stats->tx_multicast_bytes;
	adapter_stats->tx_broadcast_bytes = port_stats->tx_broadcast_bytes;
	adapter_stats->tx_discards = port_stats->tx_discards;
	adapter_stats->tx_errors = port_stats->tx_errors;
	adapter_stats->tx_pause_frames = port_stats->tx_pause_frames;
	adapter_stats->tx_pause_on_frames = port_stats->tx_pause_on_frames;
	adapter_stats->tx_pause_off_frames = port_stats->tx_pause_off_frames;
	adapter_stats->tx_internal_mac_errors =
		port_stats->tx_internal_mac_errors;
	adapter_stats->tx_control_frames = port_stats->tx_control_frames;
	adapter_stats->tx_pkts_64_bytes = port_stats->tx_pkts_64_bytes;
	adapter_stats->tx_pkts_65_to_127_bytes =
		port_stats->tx_pkts_65_to_127_bytes;
	adapter_stats->tx_pkts_128_to_255_bytes =
		port_stats->tx_pkts_128_to_255_bytes;
	adapter_stats->tx_pkts_256_to_511_bytes =
		port_stats->tx_pkts_256_to_511_bytes;
	adapter_stats->tx_pkts_512_to_1023_bytes =
		port_stats->tx_pkts_512_to_1023_bytes;
	adapter_stats->tx_pkts_1024_to_1518_bytes =
		port_stats->tx_pkts_1024_to_1518_bytes;
	adapter_stats->tx_pkts_1519_to_2047_bytes =
		port_stats->tx_pkts_1519_to_2047_bytes;
	adapter_stats->tx_pkts_2048_to_4095_bytes =
		port_stats->tx_pkts_2048_to_4095_bytes;
	adapter_stats->tx_pkts_4096_to_8191_bytes =
		port_stats->tx_pkts_4096_to_8191_bytes;
	adapter_stats->tx_pkts_8192_to_9216_bytes =
		port_stats->tx_pkts_8192_to_9216_bytes;
	adapter_stats->tx_lso_pkts = port_stats->tx_lso_pkts;
	adapter_stats->rx_pkts = port_stats->rx_pkts;
	adapter_stats->rx_unicast_pkts = port_stats->rx_unicast_pkts;
	adapter_stats->rx_multicast_pkts = port_stats->rx_multicast_pkts;
	adapter_stats->rx_broadcast_pkts = port_stats->rx_broadcast_pkts;
	adapter_stats->rx_bytes = port_stats->rx_bytes;
	adapter_stats->rx_unicast_bytes = port_stats->rx_unicast_bytes;
	adapter_stats->rx_multicast_bytes = port_stats->rx_multicast_bytes;
	adapter_stats->rx_broadcast_bytes = port_stats->rx_broadcast_bytes;
	adapter_stats->rx_unknown_protos = port_stats->rx_unknown_protos;
	adapter_stats->rx_discards = port_stats->rx_discards;
	adapter_stats->rx_errors = port_stats->rx_errors;
	adapter_stats->rx_crc_errors = port_stats->rx_crc_errors;
	adapter_stats->rx_alignment_errors = port_stats->rx_alignment_errors;
	adapter_stats->rx_symbol_errors = port_stats->rx_symbol_errors;
	adapter_stats->rx_pause_frames = port_stats->rx_pause_frames;
	adapter_stats->rx_pause_on_frames = port_stats->rx_pause_on_frames;
	adapter_stats->rx_pause_off_frames = port_stats->rx_pause_off_frames;
	adapter_stats->rx_frames_too_long = port_stats->rx_frames_too_long;
	adapter_stats->rx_internal_mac_errors =
		port_stats->rx_internal_mac_errors;
	adapter_stats->rx_undersize_pkts = port_stats->rx_undersize_pkts;
	adapter_stats->rx_oversize_pkts = port_stats->rx_oversize_pkts;
	adapter_stats->rx_fragment_pkts = port_stats->rx_fragment_pkts;
	adapter_stats->rx_jabbers = port_stats->rx_jabbers;
	adapter_stats->rx_control_frames = port_stats->rx_control_frames;
	adapter_stats->rx_control_frames_unknown_opcode =
		port_stats->rx_control_frames_unknown_opcode;
	adapter_stats->rx_in_range_errors = port_stats->rx_in_range_errors;
	adapter_stats->rx_out_of_range_errors =
		port_stats->rx_out_of_range_errors;
	adapter_stats->rx_address_match_errors =
		port_stats->rx_address_match_errors;
	adapter_stats->rx_vlan_mismatch_errors =
		port_stats->rx_vlan_mismatch_errors;
	adapter_stats->rx_dropped_too_small = port_stats->rx_dropped_too_small;
	adapter_stats->rx_dropped_too_short = port_stats->rx_dropped_too_short;
	adapter_stats->rx_dropped_header_too_small =
		port_stats->rx_dropped_header_too_small;
	adapter_stats->rx_dropped_invalid_tcp_length =
		port_stats->rx_dropped_invalid_tcp_length;
	adapter_stats->rx_dropped_runt = port_stats->rx_dropped_runt;
	adapter_stats->rx_ip_checksum_errors =
		port_stats->rx_ip_checksum_errors;
	adapter_stats->rx_tcp_checksum_errors =
		port_stats->rx_tcp_checksum_errors;
	adapter_stats->rx_udp_checksum_errors =
		port_stats->rx_udp_checksum_errors;
	adapter_stats->rx_non_rss_pkts = port_stats->rx_non_rss_pkts;
	adapter_stats->rx_ipv4_pkts = port_stats->rx_ipv4_pkts;
	adapter_stats->rx_ipv6_pkts = port_stats->rx_ipv6_pkts;
	adapter_stats->rx_ipv4_bytes = port_stats->rx_ipv4_bytes;
	adapter_stats->rx_ipv6_bytes = port_stats->rx_ipv6_bytes;
	adapter_stats->rx_nic_pkts = port_stats->rx_nic_pkts;
	adapter_stats->rx_tcp_pkts = port_stats->rx_tcp_pkts;
	adapter_stats->rx_iscsi_pkts = port_stats->rx_iscsi_pkts;
	adapter_stats->rx_management_pkts = port_stats->rx_management_pkts;
	adapter_stats->rx_switched_unicast_pkts =
		port_stats->rx_switched_unicast_pkts;
	adapter_stats->rx_switched_multicast_pkts =
		port_stats->rx_switched_multicast_pkts;
	adapter_stats->rx_switched_broadcast_pkts =
		port_stats->rx_switched_broadcast_pkts;
	adapter_stats->num_forwards = port_stats->num_forwards;
	adapter_stats->rx_fifo_overflow = port_stats->rx_fifo_overflow;
	adapter_stats->rx_input_fifo_overflow =
		port_stats->rx_input_fifo_overflow;
	adapter_stats->rx_drops_too_many_frags =
		port_stats->rx_drops_too_many_frags;
	adapter_stats->rx_drops_invalid_queue =
		port_stats->rx_drops_invalid_queue;
	adapter_stats->rx_drops_mtu = port_stats->rx_drops_mtu;
	adapter_stats->rx_pkts_64_bytes = port_stats->rx_pkts_64_bytes;
	adapter_stats->rx_pkts_65_to_127_bytes =
		port_stats->rx_pkts_65_to_127_bytes;
	adapter_stats->rx_pkts_128_to_255_bytes =
		port_stats->rx_pkts_128_to_255_bytes;
	adapter_stats->rx_pkts_256_to_511_bytes =
		port_stats->rx_pkts_256_to_511_bytes;
	adapter_stats->rx_pkts_512_to_1023_bytes =
		port_stats->rx_pkts_512_to_1023_bytes;
	adapter_stats->rx_pkts_1024_to_1518_bytes =
		port_stats->rx_pkts_1024_to_1518_bytes;
	adapter_stats->rx_pkts_1519_to_2047_bytes =
		port_stats->rx_pkts_1519_to_2047_bytes;
	adapter_stats->rx_pkts_2048_to_4095_bytes =
		port_stats->rx_pkts_2048_to_4095_bytes;
	adapter_stats->rx_pkts_4096_to_8191_bytes =
		port_stats->rx_pkts_4096_to_8191_bytes;
	adapter_stats->rx_pkts_8192_to_9216_bytes =
		port_stats->rx_pkts_8192_to_9216_bytes;
}



static void
copy_stats_to_sc_be2(POCE_SOFTC sc)
{
	struct oce_be_stats *adapter_stats;
	struct oce_pmem_stats *pmem;
	struct oce_rxf_stats_v0 *rxf_stats;
	struct oce_port_rxf_stats_v0 *port_stats;
	struct mbx_get_nic_stats_v0 *nic_mbx;
	uint32_t port = sc->port_id;

	nic_mbx = OCE_DMAPTR(&sc->stats_mem, struct mbx_get_nic_stats_v0);
	pmem = &nic_mbx->params.rsp.stats.pmem;
	rxf_stats = &nic_mbx->params.rsp.stats.rxf;
	port_stats = &nic_mbx->params.rsp.stats.rxf.port[port];
	
	adapter_stats = &sc->oce_stats_info.u0.be;

	
	/* Update stats */
	adapter_stats->rx_pause_frames = port_stats->rx_pause_frames;
	adapter_stats->rx_crc_errors = port_stats->rx_crc_errors;
	adapter_stats->rx_control_frames = port_stats->rx_control_frames;
	adapter_stats->rx_in_range_errors = port_stats->rx_in_range_errors;
	adapter_stats->rx_frame_too_long = port_stats->rx_frame_too_long;
	adapter_stats->rx_dropped_runt = port_stats->rx_dropped_runt;
	adapter_stats->rx_ip_checksum_errs = port_stats->rx_ip_checksum_errs;
	adapter_stats->rx_tcp_checksum_errs = port_stats->rx_tcp_checksum_errs;
	adapter_stats->rx_udp_checksum_errs = port_stats->rx_udp_checksum_errs;
	adapter_stats->rxpp_fifo_overflow_drop =
					port_stats->rxpp_fifo_overflow_drop;
	adapter_stats->rx_dropped_tcp_length =
		port_stats->rx_dropped_tcp_length;
	adapter_stats->rx_dropped_too_small = port_stats->rx_dropped_too_small;
	adapter_stats->rx_dropped_too_short = port_stats->rx_dropped_too_short;
	adapter_stats->rx_out_range_errors = port_stats->rx_out_range_errors;
	adapter_stats->rx_dropped_header_too_small =
		port_stats->rx_dropped_header_too_small;
	adapter_stats->rx_input_fifo_overflow_drop =
		port_stats->rx_input_fifo_overflow_drop;
	adapter_stats->rx_address_match_errors =
		port_stats->rx_address_match_errors;
	adapter_stats->rx_alignment_symbol_errors =
		port_stats->rx_alignment_symbol_errors;
	adapter_stats->tx_pauseframes = port_stats->tx_pauseframes;
	adapter_stats->tx_controlframes = port_stats->tx_controlframes;
	
	if (sc->if_id)
		adapter_stats->jabber_events = rxf_stats->port1_jabber_events;
	else
		adapter_stats->jabber_events = rxf_stats->port0_jabber_events;

	adapter_stats->rx_drops_no_pbuf = rxf_stats->rx_drops_no_pbuf;
	adapter_stats->rx_drops_no_txpb = rxf_stats->rx_drops_no_txpb;
	adapter_stats->rx_drops_no_erx_descr = rxf_stats->rx_drops_no_erx_descr;
	adapter_stats->rx_drops_invalid_ring = rxf_stats->rx_drops_invalid_ring;
	adapter_stats->forwarded_packets = rxf_stats->forwarded_packets;
	adapter_stats->rx_drops_mtu = rxf_stats->rx_drops_mtu;
	adapter_stats->rx_drops_no_tpre_descr =
		rxf_stats->rx_drops_no_tpre_descr;
	adapter_stats->rx_drops_too_many_frags =
		rxf_stats->rx_drops_too_many_frags;
	adapter_stats->eth_red_drops = pmem->eth_red_drops;
}


static void
copy_stats_to_sc_be3(POCE_SOFTC sc)
{
	struct oce_be_stats *adapter_stats;
	struct oce_pmem_stats *pmem;
	struct oce_rxf_stats_v1 *rxf_stats;
	struct oce_port_rxf_stats_v1 *port_stats;
	struct mbx_get_nic_stats_v1 *nic_mbx;
	uint32_t port = sc->port_id;

	nic_mbx = OCE_DMAPTR(&sc->stats_mem, struct mbx_get_nic_stats_v1);
	pmem = &nic_mbx->params.rsp.stats.pmem;
	rxf_stats = &nic_mbx->params.rsp.stats.rxf;
	port_stats = &nic_mbx->params.rsp.stats.rxf.port[port];

	adapter_stats = &sc->oce_stats_info.u0.be;

	/* Update stats */
	adapter_stats->pmem_fifo_overflow_drop =
		port_stats->pmem_fifo_overflow_drop;
	adapter_stats->rx_priority_pause_frames =
		port_stats->rx_priority_pause_frames;
	adapter_stats->rx_pause_frames = port_stats->rx_pause_frames;
	adapter_stats->rx_crc_errors = port_stats->rx_crc_errors;
	adapter_stats->rx_control_frames = port_stats->rx_control_frames;
	adapter_stats->rx_in_range_errors = port_stats->rx_in_range_errors;
	adapter_stats->rx_frame_too_long = port_stats->rx_frame_too_long;
	adapter_stats->rx_dropped_runt = port_stats->rx_dropped_runt;
	adapter_stats->rx_ip_checksum_errs = port_stats->rx_ip_checksum_errs;
	adapter_stats->rx_tcp_checksum_errs = port_stats->rx_tcp_checksum_errs;
	adapter_stats->rx_udp_checksum_errs = port_stats->rx_udp_checksum_errs;
	adapter_stats->rx_dropped_tcp_length =
		port_stats->rx_dropped_tcp_length;
	adapter_stats->rx_dropped_too_small = port_stats->rx_dropped_too_small;
	adapter_stats->rx_dropped_too_short = port_stats->rx_dropped_too_short;
	adapter_stats->rx_out_range_errors = port_stats->rx_out_range_errors;
	adapter_stats->rx_dropped_header_too_small =
		port_stats->rx_dropped_header_too_small;
	adapter_stats->rx_input_fifo_overflow_drop =
		port_stats->rx_input_fifo_overflow_drop;
	adapter_stats->rx_address_match_errors =
		port_stats->rx_address_match_errors;
	adapter_stats->rx_alignment_symbol_errors =
		port_stats->rx_alignment_symbol_errors;
	adapter_stats->rxpp_fifo_overflow_drop =
		port_stats->rxpp_fifo_overflow_drop;
	adapter_stats->tx_pauseframes = port_stats->tx_pauseframes;
	adapter_stats->tx_controlframes = port_stats->tx_controlframes;
	adapter_stats->jabber_events = port_stats->jabber_events;

	adapter_stats->rx_drops_no_pbuf = rxf_stats->rx_drops_no_pbuf;
	adapter_stats->rx_drops_no_txpb = rxf_stats->rx_drops_no_txpb;
	adapter_stats->rx_drops_no_erx_descr = rxf_stats->rx_drops_no_erx_descr;
	adapter_stats->rx_drops_invalid_ring = rxf_stats->rx_drops_invalid_ring;
	adapter_stats->forwarded_packets = rxf_stats->forwarded_packets;
	adapter_stats->rx_drops_mtu = rxf_stats->rx_drops_mtu;
	adapter_stats->rx_drops_no_tpre_descr =
		rxf_stats->rx_drops_no_tpre_descr;
	adapter_stats->rx_drops_too_many_frags =
		rxf_stats->rx_drops_too_many_frags;

	adapter_stats->eth_red_drops = pmem->eth_red_drops;
}

static void
copy_stats_to_sc_sh(POCE_SOFTC sc)
{
        struct oce_be_stats *adapter_stats;
        struct oce_pmem_stats *pmem;
        struct oce_rxf_stats_v2 *rxf_stats;
        struct oce_port_rxf_stats_v2 *port_stats;
        struct mbx_get_nic_stats_v2 *nic_mbx;
	struct oce_erx_stats_v2 *erx_stats;
        uint32_t port = sc->port_id;

        nic_mbx = OCE_DMAPTR(&sc->stats_mem, struct mbx_get_nic_stats_v2);
        pmem = &nic_mbx->params.rsp.stats.pmem;
        rxf_stats = &nic_mbx->params.rsp.stats.rxf;
	erx_stats = &nic_mbx->params.rsp.stats.erx;
        port_stats = &nic_mbx->params.rsp.stats.rxf.port[port];

        adapter_stats = &sc->oce_stats_info.u0.be;

        /* Update stats */
        adapter_stats->pmem_fifo_overflow_drop =
                port_stats->pmem_fifo_overflow_drop;
        adapter_stats->rx_priority_pause_frames =
                port_stats->rx_priority_pause_frames;
        adapter_stats->rx_pause_frames = port_stats->rx_pause_frames;
        adapter_stats->rx_crc_errors = port_stats->rx_crc_errors;
        adapter_stats->rx_control_frames = port_stats->rx_control_frames;
        adapter_stats->rx_in_range_errors = port_stats->rx_in_range_errors;
        adapter_stats->rx_frame_too_long = port_stats->rx_frame_too_long;
        adapter_stats->rx_dropped_runt = port_stats->rx_dropped_runt;
        adapter_stats->rx_ip_checksum_errs = port_stats->rx_ip_checksum_errs;
        adapter_stats->rx_tcp_checksum_errs = port_stats->rx_tcp_checksum_errs;
        adapter_stats->rx_udp_checksum_errs = port_stats->rx_udp_checksum_errs;
        adapter_stats->rx_dropped_tcp_length =
                port_stats->rx_dropped_tcp_length;
        adapter_stats->rx_dropped_too_small = port_stats->rx_dropped_too_small;
        adapter_stats->rx_dropped_too_short = port_stats->rx_dropped_too_short;
        adapter_stats->rx_out_range_errors = port_stats->rx_out_range_errors;
        adapter_stats->rx_dropped_header_too_small =
                port_stats->rx_dropped_header_too_small;
        adapter_stats->rx_input_fifo_overflow_drop =
                port_stats->rx_input_fifo_overflow_drop;
        adapter_stats->rx_address_match_errors =
                port_stats->rx_address_match_errors;
        adapter_stats->rx_alignment_symbol_errors =
                port_stats->rx_alignment_symbol_errors;
        adapter_stats->rxpp_fifo_overflow_drop =
                port_stats->rxpp_fifo_overflow_drop;
        adapter_stats->tx_pauseframes = port_stats->tx_pauseframes;
        adapter_stats->tx_controlframes = port_stats->tx_controlframes;
        adapter_stats->jabber_events = port_stats->jabber_events;

        adapter_stats->rx_drops_no_pbuf = rxf_stats->rx_drops_no_pbuf;
        adapter_stats->rx_drops_no_txpb = rxf_stats->rx_drops_no_txpb;
        adapter_stats->rx_drops_no_erx_descr = rxf_stats->rx_drops_no_erx_descr;
        adapter_stats->rx_drops_invalid_ring = rxf_stats->rx_drops_invalid_ring;
        adapter_stats->forwarded_packets = rxf_stats->forwarded_packets;
        adapter_stats->rx_drops_mtu = rxf_stats->rx_drops_mtu;
        adapter_stats->rx_drops_no_tpre_descr =
                rxf_stats->rx_drops_no_tpre_descr;
        adapter_stats->rx_drops_too_many_frags =
                rxf_stats->rx_drops_too_many_frags;

        adapter_stats->eth_red_drops = pmem->eth_red_drops;

	/* populate erx stats */
	for (int i = 0; i < sc->nrqs; i++) 
		sc->rq[i]->rx_stats.rx_drops_no_frags = erx_stats->rx_drops_no_fragments[sc->rq[i]->rq_id];
}



int
oce_stats_init(POCE_SOFTC sc)
{
	int rc = 0, sz = 0;


        if( IS_BE2(sc) ) 
		sz = sizeof(struct mbx_get_nic_stats_v0);
        else if( IS_BE3(sc) ) 
		sz = sizeof(struct mbx_get_nic_stats_v1);
        else if( IS_SH(sc)) 
		sz = sizeof(struct mbx_get_nic_stats_v2);
        else if( IS_XE201(sc) )
		sz = sizeof(struct mbx_get_pport_stats);

	rc = oce_dma_alloc(sc, sz, &sc->stats_mem, 0);

	return rc;
}


void
oce_stats_free(POCE_SOFTC sc)
{

	oce_dma_free(sc, &sc->stats_mem);

}


int
oce_refresh_nic_stats(POCE_SOFTC sc)
{
	int rc = 0, reset = 0;

	if( IS_BE2(sc) ) {
		rc = oce_mbox_get_nic_stats_v0(sc, &sc->stats_mem);
		if (!rc)
			copy_stats_to_sc_be2(sc);
	}else if( IS_BE3(sc) ) {
		rc = oce_mbox_get_nic_stats_v1(sc, &sc->stats_mem);
		if (!rc)
			copy_stats_to_sc_be3(sc);
	}else if( IS_SH(sc)) {
		rc = oce_mbox_get_nic_stats_v2(sc, &sc->stats_mem);
		if (!rc)
			copy_stats_to_sc_sh(sc);
	}else if( IS_XE201(sc) ){
		rc = oce_mbox_get_pport_stats(sc, &sc->stats_mem, reset);
		if (!rc)
			copy_stats_to_sc_xe201(sc);
	}

	return rc;
}

static int 
oce_sysctl_sfp_vpd_dump(SYSCTL_HANDLER_ARGS)
{
	int result = 0, error;
	int rc = 0;
	POCE_SOFTC sc = (POCE_SOFTC) arg1;

	/* sysctl default handler */
	error = sysctl_handle_int(oidp, &result, 0, req);
	if (error || !req->newptr)
		return (error);

	if(result == -1) {
		return EINVAL;
	}
	bzero((char *)sfp_vpd_dump_buffer, TRANSCEIVER_DATA_SIZE);

	rc = oce_mbox_read_transrecv_data(sc, PAGE_NUM_A0);
	if(rc)
		return rc;

	rc = oce_mbox_read_transrecv_data(sc, PAGE_NUM_A2);
	if(rc)
		return rc;

	return rc;
}
