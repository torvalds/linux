/*-
 * Copyright (c) 2018 Microsemi Corporation.
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

/* $FreeBSD$ */

#include "smartpqi_includes.h"

#define SG_FLAG_LAST	0x40000000
#define SG_FLAG_CHAIN	0x80000000

/* Subroutine to find out embedded sgl count in IU */
static inline
uint32_t pqisrc_embedded_sgl_count(uint32_t elem_alloted)
{
	uint32_t embedded_sgl_count = MAX_EMBEDDED_SG_IN_FIRST_IU;
	DBG_FUNC(" IN ");
	/**
	calculate embedded sgl count using num_elem_alloted for IO
	**/
	if(elem_alloted - 1)
		embedded_sgl_count += ((elem_alloted - 1) * MAX_EMBEDDED_SG_IN_IU);
	DBG_IO("embedded_sgl_count :%d\n",embedded_sgl_count);

	DBG_FUNC(" OUT ");

	return embedded_sgl_count;
	
}

/* Subroutine to find out contiguous free elem in IU */
static inline
uint32_t pqisrc_contiguous_free_elem(uint32_t pi, uint32_t ci, uint32_t elem_in_q)
{
	uint32_t contiguous_free_elem = 0;

	DBG_FUNC(" IN ");

	if(pi >= ci) {
		contiguous_free_elem = (elem_in_q - pi); 
		if(ci == 0)
			contiguous_free_elem -= 1;
	} else {
		contiguous_free_elem = (ci - pi - 1);
	}

	DBG_FUNC(" OUT ");

	return contiguous_free_elem;
}

/* Subroutine to find out num of elements need for the request */
static uint32_t
pqisrc_num_elem_needed(pqisrc_softstate_t *softs, uint32_t SG_Count)
{
	uint32_t num_sg;
	uint32_t num_elem_required = 1;
	DBG_FUNC(" IN ");
	DBG_IO("SGL_Count :%d",SG_Count);
	/********
	If SG_Count greater than max sg per IU i.e 4 or 68 
	(4 is with out spanning or 68 is with spanning) chaining is required.
	OR, If SG_Count <= MAX_EMBEDDED_SG_IN_FIRST_IU then,
	on these two cases one element is enough.
	********/
	if(SG_Count > softs->max_sg_per_iu || SG_Count <= MAX_EMBEDDED_SG_IN_FIRST_IU)
		return num_elem_required;
	/*
	SGL Count Other Than First IU
	 */
	num_sg = SG_Count - MAX_EMBEDDED_SG_IN_FIRST_IU;
	num_elem_required += PQISRC_DIV_ROUND_UP(num_sg, MAX_EMBEDDED_SG_IN_IU);
	DBG_FUNC(" OUT ");
	return num_elem_required;
}

/* Subroutine to build SG list for the IU submission*/
static
boolean_t pqisrc_build_sgl(sgt_t *sg_array, rcb_t *rcb, iu_header_t *iu_hdr,
			uint32_t num_elem_alloted)
{
	uint32_t i;
	uint32_t num_sg = OS_GET_IO_SG_COUNT(rcb);
	sgt_t *sgt = sg_array; 
	sgt_t *sg_chain = NULL;
	boolean_t partial = false;

	DBG_FUNC(" IN ");

	DBG_IO("SGL_Count :%d",num_sg);
	if (0 == num_sg) {
		goto out;
	}

	if (num_sg <= pqisrc_embedded_sgl_count(num_elem_alloted)) {
		for (i = 0; i < num_sg; i++, sgt++) {
                        sgt->addr= OS_GET_IO_SG_ADDR(rcb,i);
                        sgt->len= OS_GET_IO_SG_LEN(rcb,i);
                        sgt->flags= 0;
                }
		
		sg_array[num_sg - 1].flags = SG_FLAG_LAST;
	} else {
	/**
	SGL Chaining
	**/
		sg_chain = rcb->sg_chain_virt;
		sgt->addr = rcb->sg_chain_dma;
		sgt->len = num_sg * sizeof(sgt_t);
		sgt->flags = SG_FLAG_CHAIN;
		
		sgt = sg_chain;
		for (i = 0; i < num_sg; i++, sgt++) {
			sgt->addr = OS_GET_IO_SG_ADDR(rcb,i);
			sgt->len = OS_GET_IO_SG_LEN(rcb,i);
			sgt->flags = 0;
		}
		
		sg_chain[num_sg - 1].flags = SG_FLAG_LAST; 
		num_sg = 1;
		partial = true;

	}
out:
	iu_hdr->iu_length = num_sg * sizeof(sgt_t);
	DBG_FUNC(" OUT ");
	return partial;
	
}

/*Subroutine used to Build the RAID request */
static void 
pqisrc_build_raid_io(pqisrc_softstate_t *softs, rcb_t *rcb, 
 	pqisrc_raid_req_t *raid_req, uint32_t num_elem_alloted)
{
	DBG_FUNC(" IN ");
	
	raid_req->header.iu_type = PQI_IU_TYPE_RAID_PATH_IO_REQUEST;
	raid_req->header.comp_feature = 0;
	raid_req->response_queue_id = OS_GET_IO_RESP_QID(softs, rcb);
	raid_req->work_area[0] = 0;
	raid_req->work_area[1] = 0;
	raid_req->request_id = rcb->tag;
	raid_req->nexus_id = 0;
	raid_req->buffer_length = GET_SCSI_BUFFLEN(rcb);
	memcpy(raid_req->lun_number, rcb->dvp->scsi3addr, 
 		sizeof(raid_req->lun_number)); 
	raid_req->protocol_spec = 0;
	raid_req->data_direction = rcb->data_dir;
	raid_req->reserved1 = 0;
	raid_req->fence = 0;
	raid_req->error_index = raid_req->request_id;
	raid_req->reserved2 = 0;
  	raid_req->task_attribute = OS_GET_TASK_ATTR(rcb);
  	raid_req->command_priority = 0;
	raid_req->reserved3 = 0;
	raid_req->reserved4 = 0;
	raid_req->reserved5 = 0;

	/* As cdb and additional_cdb_bytes are contiguous, 
	   update them in a single statement */
	memcpy(raid_req->cdb, rcb->cdbp, rcb->cmdlen);
#if 0
	DBG_IO("CDB :");
	for(i = 0; i < rcb->cmdlen ; i++)
		DBG_IO(" 0x%x \n ",raid_req->cdb[i]);
#endif
	
	switch (rcb->cmdlen) {
		case 6:
		case 10:
		case 12:
		case 16:
			raid_req->additional_cdb_bytes_usage =
				PQI_ADDITIONAL_CDB_BYTES_0;
			break;
		case 20:
			raid_req->additional_cdb_bytes_usage =
				PQI_ADDITIONAL_CDB_BYTES_4;
			break;
		case 24:
			raid_req->additional_cdb_bytes_usage =
				PQI_ADDITIONAL_CDB_BYTES_8;
			break;
		case 28:
			raid_req->additional_cdb_bytes_usage =
				PQI_ADDITIONAL_CDB_BYTES_12;
			break;
		case 32:
		default: /* todo:review again */
			raid_req->additional_cdb_bytes_usage =
				PQI_ADDITIONAL_CDB_BYTES_16;
			break;
	}
	
	/* Frame SGL Descriptor */
	raid_req->partial = pqisrc_build_sgl(&raid_req->sg_descriptors[0], rcb,
		&raid_req->header, num_elem_alloted);							

	raid_req->header.iu_length += 
			offsetof(pqisrc_raid_req_t, sg_descriptors) - sizeof(iu_header_t);
	
#if 0
	DBG_IO("raid_req->header.iu_type : 0x%x", raid_req->header.iu_type);
	DBG_IO("raid_req->response_queue_id :%d\n"raid_req->response_queue_id);
	DBG_IO("raid_req->request_id : 0x%x", raid_req->request_id);
	DBG_IO("raid_req->buffer_length : 0x%x", raid_req->buffer_length);
	DBG_IO("raid_req->task_attribute : 0x%x", raid_req->task_attribute);
	DBG_IO("raid_req->lun_number  : 0x%x", raid_req->lun_number);
	DBG_IO("raid_req->error_index : 0x%x", raid_req->error_index);
	DBG_IO("raid_req->sg_descriptors[0].addr : %p", (void*)raid_req->sg_descriptors[0].addr);
	DBG_IO("raid_req->sg_descriptors[0].len : 0x%x", raid_req->sg_descriptors[0].len);
	DBG_IO("raid_req->sg_descriptors[0].flags : 0%x", raid_req->sg_descriptors[0].flags);
#endif	
	rcb->success_cmp_callback = pqisrc_process_io_response_success; 
	rcb->error_cmp_callback = pqisrc_process_raid_response_error; 
	rcb->resp_qid = raid_req->response_queue_id;
	
 	DBG_FUNC(" OUT ");
	
}

/*Subroutine used to Build the AIO request */
static void
pqisrc_build_aio_io(pqisrc_softstate_t *softs, rcb_t *rcb, 
 				pqi_aio_req_t *aio_req, uint32_t num_elem_alloted)
{
	DBG_FUNC(" IN ");

	aio_req->header.iu_type = PQI_IU_TYPE_AIO_PATH_IO_REQUEST;
	aio_req->header.comp_feature = 0;
	aio_req->response_queue_id = OS_GET_IO_RESP_QID(softs, rcb);
	aio_req->work_area[0] = 0;
	aio_req->work_area[1] = 0;
	aio_req->req_id = rcb->tag;
	aio_req->res1[0] = 0;
	aio_req->res1[1] = 0;
	aio_req->nexus = rcb->ioaccel_handle;
	aio_req->buf_len = GET_SCSI_BUFFLEN(rcb);
	aio_req->data_dir = rcb->data_dir;
	aio_req->mem_type = 0;
	aio_req->fence = 0;
	aio_req->res2 = 0;
	aio_req->task_attr = OS_GET_TASK_ATTR(rcb); 
	aio_req->cmd_prio = 0;
	aio_req->res3 = 0;
	aio_req->err_idx = aio_req->req_id;
	aio_req->cdb_len = rcb->cmdlen;
	if(rcb->cmdlen > sizeof(aio_req->cdb))
		rcb->cmdlen = sizeof(aio_req->cdb);
	memcpy(aio_req->cdb, rcb->cdbp, rcb->cmdlen);
#if 0
	DBG_IO("CDB : \n");
	for(int i = 0; i < rcb->cmdlen ; i++)
		 DBG_IO(" 0x%x \n",aio_req->cdb[i]);
#endif
	memset(aio_req->lun,0,sizeof(aio_req->lun));
	memset(aio_req->res4,0,sizeof(aio_req->res4));
	
	if(rcb->encrypt_enable == true) {
		aio_req->encrypt_enable = true;
		aio_req->encrypt_key_index = LE_16(rcb->enc_info.data_enc_key_index);
		aio_req->encrypt_twk_low = LE_32(rcb->enc_info.encrypt_tweak_lower);
		aio_req->encrypt_twk_high = LE_32(rcb->enc_info.encrypt_tweak_upper);
	} else {
		aio_req->encrypt_enable = 0;
		aio_req->encrypt_key_index = 0;
		aio_req->encrypt_twk_high = 0;
		aio_req->encrypt_twk_low = 0;
	}	
	
	/* Frame SGL Descriptor */
	aio_req->partial = pqisrc_build_sgl(&aio_req->sg_desc[0], rcb,
 		&aio_req->header, num_elem_alloted);

	aio_req->num_sg = aio_req->header.iu_length / sizeof(sgt_t);

	DBG_INFO("aio_req->num_sg :%d",aio_req->num_sg);
	
	aio_req->header.iu_length += offsetof(pqi_aio_req_t, sg_desc) - 
		sizeof(iu_header_t);
#if 0
	DBG_IO("aio_req->header.iu_type : 0x%x \n",aio_req->header.iu_type);
	DBG_IO("aio_req->resp_qid :0x%x",aio_req->resp_qid);
	DBG_IO("aio_req->req_id : 0x%x \n",aio_req->req_id);
	DBG_IO("aio_req->nexus : 0x%x  \n",aio_req->nexus);
	DBG_IO("aio_req->buf_len : 0x%x \n",aio_req->buf_len);
	DBG_IO("aio_req->data_dir : 0x%x \n",aio_req->data_dir);
	DBG_IO("aio_req->task_attr : 0x%x \n",aio_req->task_attr);
	DBG_IO("aio_req->err_idx : 0x%x \n",aio_req->err_idx);
	DBG_IO("aio_req->num_sg :%d",aio_req->num_sg);
	DBG_IO("aio_req->sg_desc[0].addr : %p \n", (void*)aio_req->sg_desc[0].addr);
	DBG_IO("aio_req->sg_desc[0].len : 0%x \n", aio_req->sg_desc[0].len);
	DBG_IO("aio_req->sg_desc[0].flags : 0%x \n", aio_req->sg_desc[0].flags);
#endif
	
	rcb->success_cmp_callback = pqisrc_process_io_response_success; 
	rcb->error_cmp_callback = pqisrc_process_aio_response_error; 
	rcb->resp_qid = aio_req->response_queue_id;

	DBG_FUNC(" OUT ");

}

/*Function used to build and send RAID/AIO */
int pqisrc_build_send_io(pqisrc_softstate_t *softs,rcb_t *rcb)
{
	ib_queue_t *ib_q_array = softs->op_aio_ib_q;
	ib_queue_t *ib_q = NULL;
	char *ib_iu = NULL;	
	IO_PATH_T io_path = AIO_PATH;
	uint32_t TraverseCount = 0; 
	int first_qindex = OS_GET_IO_REQ_QINDEX(softs, rcb); 
	int qindex = first_qindex;
	uint32_t num_op_ib_q = softs->num_op_aio_ibq;
	uint32_t num_elem_needed;
	uint32_t num_elem_alloted = 0;
	pqi_scsi_dev_t *devp = rcb->dvp;
	uint8_t raidbypass_cdb[16];
	
	DBG_FUNC(" IN ");


	rcb->cdbp = OS_GET_CDBP(rcb);
	
	if(IS_AIO_PATH(devp)) {
		/**  IO for Physical Drive  **/
		/** Send in AIO PATH**/
		rcb->ioaccel_handle = devp->ioaccel_handle;
	} else {
		int ret = PQI_STATUS_FAILURE;
		/** IO for RAID Volume **/
		if (devp->offload_enabled) {
			/** ByPass IO ,Send in AIO PATH **/
			ret = pqisrc_send_scsi_cmd_raidbypass(softs, 
				devp, rcb, raidbypass_cdb);
		}
		
		if (PQI_STATUS_FAILURE == ret) {
			/** Send in RAID PATH **/
			io_path = RAID_PATH;
			num_op_ib_q = softs->num_op_raid_ibq;
			ib_q_array = softs->op_raid_ib_q;
		} else {
			rcb->cdbp = raidbypass_cdb;
		}
	}
	
	num_elem_needed = pqisrc_num_elem_needed(softs, OS_GET_IO_SG_COUNT(rcb));
	DBG_IO("num_elem_needed :%d",num_elem_needed);
	
	do {
		uint32_t num_elem_available;
		ib_q = (ib_q_array + qindex);
		PQI_LOCK(&ib_q->lock);	
		num_elem_available = pqisrc_contiguous_free_elem(ib_q->pi_local,
					*(ib_q->ci_virt_addr), ib_q->num_elem);
		
		DBG_IO("num_elem_avialable :%d\n",num_elem_available);
		if(num_elem_available >= num_elem_needed) {
			num_elem_alloted = num_elem_needed;
			break;
		}
		DBG_IO("Current queue is busy! Hop to next queue\n");

		PQI_UNLOCK(&ib_q->lock);	
		qindex = (qindex + 1) % num_op_ib_q;
		if(qindex == first_qindex) {
			if (num_elem_needed == 1)
				break;
			TraverseCount += 1;
			num_elem_needed = 1;
		}
	}while(TraverseCount < 2);
	
	DBG_IO("num_elem_alloted :%d",num_elem_alloted);
	if (num_elem_alloted == 0) {
		DBG_WARN("OUT: IB Queues were full\n");
		return PQI_STATUS_QFULL;
	}	
	
	/* Get IB Queue Slot address to build IU */
	ib_iu = ib_q->array_virt_addr + (ib_q->pi_local * ib_q->elem_size);
	
	if(io_path == AIO_PATH) {
		/** Build AIO structure **/
 		pqisrc_build_aio_io(softs, rcb, (pqi_aio_req_t*)ib_iu,
 			num_elem_alloted);
	} else {
		/** Build RAID structure **/
		pqisrc_build_raid_io(softs, rcb, (pqisrc_raid_req_t*)ib_iu,
			num_elem_alloted);
	}
	
	rcb->req_pending = true;
	
	/* Update the local PI */
	ib_q->pi_local = (ib_q->pi_local + num_elem_alloted) % ib_q->num_elem;

	DBG_INFO("ib_q->pi_local : %x\n", ib_q->pi_local);
	DBG_INFO("*ib_q->ci_virt_addr: %x\n",*(ib_q->ci_virt_addr));

	/* Inform the fw about the new IU */
	PCI_MEM_PUT32(softs, ib_q->pi_register_abs, ib_q->pi_register_offset, ib_q->pi_local);
	
	PQI_UNLOCK(&ib_q->lock);	
	DBG_FUNC(" OUT ");
	return PQI_STATUS_SUCCESS;
}

/* Subroutine used to set encryption info as part of RAID bypass IO*/
static inline void pqisrc_set_enc_info(
	struct pqi_enc_info *enc_info, struct raid_map *raid_map,
	uint64_t first_block)
{
	uint32_t volume_blk_size;

	/*
	 * Set the encryption tweak values based on logical block address.
	 * If the block size is 512, the tweak value is equal to the LBA.
	 * For other block sizes, tweak value is (LBA * block size) / 512.
	 */
	volume_blk_size = GET_LE32((uint8_t *)&raid_map->volume_blk_size);
	if (volume_blk_size != 512)
		first_block = (first_block * volume_blk_size) / 512;

	enc_info->data_enc_key_index =
		GET_LE16((uint8_t *)&raid_map->data_encryption_key_index);
	enc_info->encrypt_tweak_upper = ((uint32_t)(((first_block) >> 16) >> 16));
	enc_info->encrypt_tweak_lower = ((uint32_t)(first_block));
}


/*
 * Attempt to perform offload RAID mapping for a logical volume I/O.
 */

#define HPSA_RAID_0		0
#define HPSA_RAID_4		1
#define HPSA_RAID_1		2	/* also used for RAID 10 */
#define HPSA_RAID_5		3	/* also used for RAID 50 */
#define HPSA_RAID_51		4
#define HPSA_RAID_6		5	/* also used for RAID 60 */
#define HPSA_RAID_ADM		6	/* also used for RAID 1+0 ADM */
#define HPSA_RAID_MAX		HPSA_RAID_ADM
#define HPSA_RAID_UNKNOWN	0xff

/* Subroutine used to parse the scsi opcode and build the CDB for RAID bypass*/
int check_for_scsi_opcode(uint8_t *cdb, boolean_t *is_write, uint64_t *fst_blk,
				uint32_t *blk_cnt) {
	
	switch (cdb[0]) {
	case SCMD_WRITE_6:
		*is_write = true;
	case SCMD_READ_6:
		*fst_blk = (uint64_t)(((cdb[1] & 0x1F) << 16) |
				(cdb[2] << 8) | cdb[3]);
		*blk_cnt = (uint32_t)cdb[4];
		if (*blk_cnt == 0)
			*blk_cnt = 256;
		break;
	case SCMD_WRITE_10:
		*is_write = true;
	case SCMD_READ_10:
		*fst_blk = (uint64_t)GET_BE32(&cdb[2]);
		*blk_cnt = (uint32_t)GET_BE16(&cdb[7]);
		break;
	case SCMD_WRITE_12:
		*is_write = true;
	case SCMD_READ_12:
		*fst_blk = (uint64_t)GET_BE32(&cdb[2]);
		*blk_cnt = GET_BE32(&cdb[6]);
		break;
	case SCMD_WRITE_16:
		*is_write = true;
	case SCMD_READ_16:
		*fst_blk = GET_BE64(&cdb[2]);
		*blk_cnt = GET_BE32(&cdb[10]);
		break;
	default:
		/* Process via normal I/O path. */
		return PQI_STATUS_FAILURE;
	}
	return PQI_STATUS_SUCCESS;
}

/*
 * Function used to build and send RAID bypass request to the adapter
 */
int pqisrc_send_scsi_cmd_raidbypass(pqisrc_softstate_t *softs,
				pqi_scsi_dev_t *device, rcb_t *rcb, uint8_t *cdb)
{
	struct raid_map *raid_map;
	boolean_t is_write = false;
	uint32_t map_idx;
	uint64_t fst_blk, lst_blk;
	uint32_t blk_cnt, blks_per_row;
	uint64_t fst_row, lst_row;
	uint32_t fst_row_offset, lst_row_offset;
	uint32_t fst_col, lst_col;
	uint32_t r5or6_blks_per_row;
	uint64_t r5or6_fst_row, r5or6_lst_row;
	uint32_t r5or6_fst_row_offset, r5or6_lst_row_offset;
	uint32_t r5or6_fst_col, r5or6_lst_col;
	uint16_t data_disks_per_row, total_disks_per_row;
	uint16_t layout_map_count;
	uint32_t stripesz;
	uint16_t strip_sz;
	uint32_t fst_grp, lst_grp, cur_grp;
	uint32_t map_row;
	uint64_t disk_block;
	uint32_t disk_blk_cnt;
	uint8_t cdb_length;
	int offload_to_mirror;
	int i;
	DBG_FUNC(" IN \n");
	DBG_IO("!!!!!\n");

	/* Check for eligible opcode, get LBA and block count. */
	memcpy(cdb, OS_GET_CDBP(rcb), rcb->cmdlen);
	
	for(i = 0; i < rcb->cmdlen ; i++)
		DBG_IO(" CDB [ %d ] : %x\n",i,cdb[i]);
	if(check_for_scsi_opcode(cdb, &is_write, 
		&fst_blk, &blk_cnt) == PQI_STATUS_FAILURE)
			return PQI_STATUS_FAILURE;
	/* Check for write to non-RAID-0. */
	if (is_write && device->raid_level != SA_RAID_0)
		return PQI_STATUS_FAILURE;;

	if(blk_cnt == 0) 
		return PQI_STATUS_FAILURE;

	lst_blk = fst_blk + blk_cnt - 1;
	raid_map = device->raid_map;

	/* Check for invalid block or wraparound. */
	if (lst_blk >= GET_LE64((uint8_t *)&raid_map->volume_blk_cnt) ||
		lst_blk < fst_blk)
		return PQI_STATUS_FAILURE;

	data_disks_per_row = GET_LE16((uint8_t *)&raid_map->data_disks_per_row);
	strip_sz = GET_LE16((uint8_t *)(&raid_map->strip_size));
	layout_map_count = GET_LE16((uint8_t *)(&raid_map->layout_map_count));

	/* Calculate stripe information for the request. */
	blks_per_row = data_disks_per_row * strip_sz;
	if (!blks_per_row)
		return PQI_STATUS_FAILURE;
	/* use __udivdi3 ? */
	fst_row = fst_blk / blks_per_row;
	lst_row = lst_blk / blks_per_row;
	fst_row_offset = (uint32_t)(fst_blk - (fst_row * blks_per_row));
	lst_row_offset = (uint32_t)(lst_blk - (lst_row * blks_per_row));
	fst_col = fst_row_offset / strip_sz;
	lst_col = lst_row_offset / strip_sz;

	/* If this isn't a single row/column then give to the controller. */
	if (fst_row != lst_row || fst_col != lst_col)
		return PQI_STATUS_FAILURE;

	/* Proceeding with driver mapping. */
	total_disks_per_row = data_disks_per_row +
		GET_LE16((uint8_t *)(&raid_map->metadata_disks_per_row));
	map_row = ((uint32_t)(fst_row >> raid_map->parity_rotation_shift)) %
		GET_LE16((uint8_t *)(&raid_map->row_cnt));
	map_idx = (map_row * total_disks_per_row) + fst_col;

	/* RAID 1 */
	if (device->raid_level == SA_RAID_1) {
		if (device->offload_to_mirror)
			map_idx += data_disks_per_row;
		device->offload_to_mirror = !device->offload_to_mirror;
	} else if (device->raid_level == SA_RAID_ADM) {
		/* RAID ADM */
		/*
		 * Handles N-way mirrors  (R1-ADM) and R10 with # of drives
		 * divisible by 3.
		 */
		offload_to_mirror = device->offload_to_mirror;
		if (offload_to_mirror == 0)  {
			/* use physical disk in the first mirrored group. */
			map_idx %= data_disks_per_row;
		} else {
			do {
				/*
				 * Determine mirror group that map_idx
				 * indicates.
				 */
				cur_grp = map_idx / data_disks_per_row;

				if (offload_to_mirror != cur_grp) {
					if (cur_grp <
						layout_map_count - 1) {
						/*
						 * Select raid index from
						 * next group.
						 */
						map_idx += data_disks_per_row;
						cur_grp++;
					} else {
						/*
						 * Select raid index from first
						 * group.
						 */
						map_idx %= data_disks_per_row;
						cur_grp = 0;
					}
				}
			} while (offload_to_mirror != cur_grp);
		}

		/* Set mirror group to use next time. */
		offload_to_mirror =
			(offload_to_mirror >= layout_map_count - 1) ?
				0 : offload_to_mirror + 1;
		if(offload_to_mirror >= layout_map_count)
			return PQI_STATUS_FAILURE;

		device->offload_to_mirror = offload_to_mirror;
		/*
		 * Avoid direct use of device->offload_to_mirror within this
		 * function since multiple threads might simultaneously
		 * increment it beyond the range of device->layout_map_count -1.
		 */
	} else if ((device->raid_level == SA_RAID_5 ||
		device->raid_level == SA_RAID_6) && layout_map_count > 1) {
		/* RAID 50/60 */
		/* Verify first and last block are in same RAID group */
		r5or6_blks_per_row = strip_sz * data_disks_per_row;
		stripesz = r5or6_blks_per_row * layout_map_count;

		fst_grp = (fst_blk % stripesz) / r5or6_blks_per_row;
		lst_grp = (lst_blk % stripesz) / r5or6_blks_per_row;

		if (fst_grp != lst_grp)
			return PQI_STATUS_FAILURE;

		/* Verify request is in a single row of RAID 5/6 */
		fst_row = r5or6_fst_row =
			fst_blk / stripesz;
		r5or6_lst_row = lst_blk / stripesz;

		if (r5or6_fst_row != r5or6_lst_row)
			return PQI_STATUS_FAILURE;

		/* Verify request is in a single column */
		fst_row_offset = r5or6_fst_row_offset =
			(uint32_t)((fst_blk % stripesz) %
			r5or6_blks_per_row);

		r5or6_lst_row_offset =
			(uint32_t)((lst_blk % stripesz) %
			r5or6_blks_per_row);

		fst_col = r5or6_fst_row_offset / strip_sz;
		r5or6_fst_col = fst_col;
		r5or6_lst_col = r5or6_lst_row_offset / strip_sz;

		if (r5or6_fst_col != r5or6_lst_col)
			return PQI_STATUS_FAILURE;

		/* Request is eligible */
		map_row =
			((uint32_t)(fst_row >> raid_map->parity_rotation_shift)) %
			GET_LE16((uint8_t *)(&raid_map->row_cnt));

		map_idx = (fst_grp *
			(GET_LE16((uint8_t *)(&raid_map->row_cnt)) *
			total_disks_per_row)) +
			(map_row * total_disks_per_row) + fst_col;
	}

	if (map_idx >= RAID_MAP_MAX_ENTRIES)
		return PQI_STATUS_FAILURE;

	rcb->ioaccel_handle = raid_map->dev_data[map_idx].ioaccel_handle;
	disk_block = GET_LE64((uint8_t *)(&raid_map->disk_starting_blk)) +
		fst_row * strip_sz +
		(fst_row_offset - fst_col * strip_sz);
	disk_blk_cnt = blk_cnt;

	/* Handle differing logical/physical block sizes. */
	if (raid_map->phys_blk_shift) {
		disk_block <<= raid_map->phys_blk_shift;
		disk_blk_cnt <<= raid_map->phys_blk_shift;
	}

	if (disk_blk_cnt > 0xffff)
		return PQI_STATUS_FAILURE;

	/* Build the new CDB for the physical disk I/O. */
	if (disk_block > 0xffffffff) {
		cdb[0] = is_write ? SCMD_WRITE_16 : SCMD_READ_16;
		cdb[1] = 0;
		PUT_BE64(disk_block, &cdb[2]);
		PUT_BE32(disk_blk_cnt, &cdb[10]);
		cdb[14] = 0;
		cdb[15] = 0;
		cdb_length = 16;
	} else {
		cdb[0] = is_write ? SCMD_WRITE_10 : SCMD_READ_10;
		cdb[1] = 0;
		PUT_BE32(disk_block, &cdb[2]);
		cdb[6] = 0;
		PUT_BE16(disk_blk_cnt, &cdb[7]);
		cdb[9] = 0;
		cdb_length = 10;
	}

	if (GET_LE16((uint8_t *)(&raid_map->flags)) &
		RAID_MAP_ENCRYPTION_ENABLED) {
		pqisrc_set_enc_info(&rcb->enc_info, raid_map,
			fst_blk);
		rcb->encrypt_enable = true;
	} else {
		rcb->encrypt_enable = false;
	}

	rcb->cmdlen = cdb_length;
	
		
	DBG_FUNC("OUT");
	
	return PQI_STATUS_SUCCESS;
}

/* Function used to submit a TMF to the adater */
int pqisrc_send_tmf(pqisrc_softstate_t *softs, pqi_scsi_dev_t *devp,
                    rcb_t *rcb, int req_id, int tmf_type)
{
	int rval = PQI_STATUS_SUCCESS;
	pqi_tmf_req_t tmf_req;

	memset(&tmf_req, 0, sizeof(pqi_tmf_req_t));

	DBG_FUNC("IN");

	tmf_req.header.iu_type = PQI_REQUEST_IU_TASK_MANAGEMENT;
	tmf_req.header.iu_length = sizeof(tmf_req) - sizeof(iu_header_t);
	tmf_req.req_id = rcb->tag;

	memcpy(tmf_req.lun, devp->scsi3addr, sizeof(tmf_req.lun));
	tmf_req.tmf = tmf_type;
	tmf_req.req_id_to_manage = req_id;
	tmf_req.resp_qid = OS_GET_TMF_RESP_QID(softs, rcb);
	tmf_req.obq_id_to_manage = rcb->resp_qid;

	rcb->req_pending = true;

	rval = pqisrc_submit_cmnd(softs,
	&softs->op_raid_ib_q[OS_GET_TMF_REQ_QINDEX(softs, rcb)], &tmf_req);
	if (rval != PQI_STATUS_SUCCESS) {
		DBG_ERR("Unable to submit command rval=%d\n", rval);
		return rval;
	}

	rval = pqisrc_wait_on_condition(softs, rcb);
	if (rval != PQI_STATUS_SUCCESS){
		DBG_ERR("Task Management tmf_type : %d timeout\n", tmf_type);
		rcb->status = REQUEST_FAILED;
	}

	if (rcb->status  != REQUEST_SUCCESS) {
		DBG_ERR_BTL(devp, "Task Management failed tmf_type:%d "
				"stat:0x%x\n", tmf_type, rcb->status);
		rval = PQI_STATUS_FAILURE;
	}

	DBG_FUNC("OUT");
	return rval;
}
