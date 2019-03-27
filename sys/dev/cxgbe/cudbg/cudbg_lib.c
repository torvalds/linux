/*-
 * Copyright (c) 2017 Chelsio Communications, Inc.
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

#include <sys/types.h>
#include <sys/param.h>

#include "common/common.h"
#include "common/t4_regs.h"
#include "cudbg.h"
#include "cudbg_lib_common.h"
#include "cudbg_lib.h"
#include "cudbg_entity.h"
#define  BUFFER_WARN_LIMIT 10000000

struct large_entity large_entity_list[] = {
	{CUDBG_EDC0, 0, 0},
	{CUDBG_EDC1, 0 , 0},
	{CUDBG_MC0, 0, 0},
	{CUDBG_MC1, 0, 0}
};

static int is_fw_attached(struct cudbg_init *pdbg_init)
{

	return (pdbg_init->adap->flags & FW_OK);
}

/* This function will add additional padding bytes into debug_buffer to make it
 * 4 byte aligned.*/
static void align_debug_buffer(struct cudbg_buffer *dbg_buff,
			struct cudbg_entity_hdr *entity_hdr)
{
	u8 zero_buf[4] = {0};
	u8 padding, remain;

	remain = (dbg_buff->offset - entity_hdr->start_offset) % 4;
	padding = 4 - remain;
	if (remain) {
		memcpy(((u8 *) dbg_buff->data) + dbg_buff->offset, &zero_buf,
		       padding);
		dbg_buff->offset += padding;
		entity_hdr->num_pad = padding;
	}

	entity_hdr->size = dbg_buff->offset - entity_hdr->start_offset;
}

static void read_sge_ctxt(struct cudbg_init *pdbg_init, u32 cid,
			  enum ctxt_type ctype, u32 *data)
{
	struct adapter *padap = pdbg_init->adap;
	int rc = -1;

	if (is_fw_attached(pdbg_init)) {
		rc = begin_synchronized_op(padap, NULL, SLEEP_OK | INTR_OK,
		    "t4cudf");
		if (rc != 0)
			goto out;
		rc = t4_sge_ctxt_rd(padap, padap->mbox, cid, ctype,
				    data);
		end_synchronized_op(padap, 0);
	}

out:
	if (rc)
		t4_sge_ctxt_rd_bd(padap, cid, ctype, data);
}

static int get_next_ext_entity_hdr(void *outbuf, u32 *ext_size,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_entity_hdr **entity_hdr)
{
	struct cudbg_hdr *cudbg_hdr = (struct cudbg_hdr *)outbuf;
	int rc = 0;
	u32 ext_offset = cudbg_hdr->data_len;
	*ext_size = 0;

	if (dbg_buff->size - dbg_buff->offset <=
		 sizeof(struct cudbg_entity_hdr)) {
		rc = CUDBG_STATUS_BUFFER_SHORT;
		goto err;
	}

	*entity_hdr = (struct cudbg_entity_hdr *)
		       ((char *)outbuf + cudbg_hdr->data_len);

	/* Find the last extended entity header */
	while ((*entity_hdr)->size) {

		ext_offset += sizeof(struct cudbg_entity_hdr) +
				     (*entity_hdr)->size;

		*ext_size += (*entity_hdr)->size +
			      sizeof(struct cudbg_entity_hdr);

		if (dbg_buff->size - dbg_buff->offset + *ext_size  <=
			sizeof(struct cudbg_entity_hdr)) {
			rc = CUDBG_STATUS_BUFFER_SHORT;
			goto err;
		}

		if (ext_offset != (*entity_hdr)->next_ext_offset) {
			ext_offset -= sizeof(struct cudbg_entity_hdr) +
				     (*entity_hdr)->size;
			break;
		}

		(*entity_hdr)->next_ext_offset = *ext_size;

		*entity_hdr = (struct cudbg_entity_hdr *)
					   ((char *)outbuf +
					   ext_offset);
	}

	/* update the data offset */
	dbg_buff->offset = ext_offset;
err:
	return rc;
}

static int wr_entity_to_flash(void *handle, struct cudbg_buffer *dbg_buff,
		       u32 cur_entity_data_offset,
		       u32 cur_entity_size,
		       int entity_nu, u32 ext_size)
{
	struct cudbg_private *priv = handle;
	struct cudbg_init *cudbg_init = &priv->dbg_init;
	struct cudbg_flash_sec_info *sec_info = &priv->sec_info;
	u64 timestamp;
	u32 cur_entity_hdr_offset = sizeof(struct cudbg_hdr);
	u32 remain_flash_size;
	u32 flash_data_offset;
	u32 data_hdr_size;
	int rc = -1;

	data_hdr_size = CUDBG_MAX_ENTITY * sizeof(struct cudbg_entity_hdr) +
			sizeof(struct cudbg_hdr);

	flash_data_offset = (FLASH_CUDBG_NSECS *
			     (sizeof(struct cudbg_flash_hdr) +
			      data_hdr_size)) +
			    (cur_entity_data_offset - data_hdr_size);

	if (flash_data_offset > CUDBG_FLASH_SIZE) {
		update_skip_size(sec_info, cur_entity_size);
		if (cudbg_init->verbose)
			cudbg_init->print("Large entity skipping...\n");
		return rc;
	}

	remain_flash_size = CUDBG_FLASH_SIZE - flash_data_offset;

	if (cur_entity_size > remain_flash_size) {
		update_skip_size(sec_info, cur_entity_size);
		if (cudbg_init->verbose)
			cudbg_init->print("Large entity skipping...\n");
	} else {
		timestamp = 0;

		cur_entity_hdr_offset +=
			(sizeof(struct cudbg_entity_hdr) *
			(entity_nu - 1));

		rc = cudbg_write_flash(handle, timestamp, dbg_buff,
				       cur_entity_data_offset,
				       cur_entity_hdr_offset,
				       cur_entity_size,
				       ext_size);
		if (rc == CUDBG_STATUS_FLASH_FULL && cudbg_init->verbose)
			cudbg_init->print("\n\tFLASH is full... "
				"can not write in flash more\n\n");
	}

	return rc;
}

int cudbg_collect(void *handle, void *outbuf, u32 *outbuf_size)
{
	struct cudbg_entity_hdr *entity_hdr = NULL;
	struct cudbg_entity_hdr *ext_entity_hdr = NULL;
	struct cudbg_hdr *cudbg_hdr;
	struct cudbg_buffer dbg_buff;
	struct cudbg_error cudbg_err = {0};
	int large_entity_code;

	u8 *dbg_bitmap = ((struct cudbg_private *)handle)->dbg_init.dbg_bitmap;
	struct cudbg_init *cudbg_init =
		&(((struct cudbg_private *)handle)->dbg_init);
	struct adapter *padap = cudbg_init->adap;
	u32 total_size, remaining_buf_size;
	u32 ext_size = 0;
	int index, bit, i, rc = -1;
	int all;
	bool flag_ext = 0;

	reset_skip_entity();

	dbg_buff.data = outbuf;
	dbg_buff.size = *outbuf_size;
	dbg_buff.offset = 0;

	cudbg_hdr = (struct cudbg_hdr *)dbg_buff.data;
	cudbg_hdr->signature = CUDBG_SIGNATURE;
	cudbg_hdr->hdr_len = sizeof(struct cudbg_hdr);
	cudbg_hdr->major_ver = CUDBG_MAJOR_VERSION;
	cudbg_hdr->minor_ver = CUDBG_MINOR_VERSION;
	cudbg_hdr->max_entities = CUDBG_MAX_ENTITY;
	cudbg_hdr->chip_ver = padap->params.chipid;

	if (cudbg_hdr->data_len)
		flag_ext = 1;

	if (cudbg_init->use_flash) {
#ifndef notyet
		rc = t4_get_flash_params(padap);
		if (rc) {
			if (cudbg_init->verbose)
				cudbg_init->print("\nGet flash params failed.\n\n");
			cudbg_init->use_flash = 0;
		}
#endif

#ifdef notyet
		/* Timestamp is mandatory. If it is not passed then disable
		 * flash support
		 */
		if (!cudbg_init->dbg_params[CUDBG_TIMESTAMP_PARAM].u.time) {
			if (cudbg_init->verbose)
				cudbg_init->print("\nTimestamp param missing,"
					  "so ignoring flash write request\n\n");
			cudbg_init->use_flash = 0;
		}
#endif
	}

	if (sizeof(struct cudbg_entity_hdr) * CUDBG_MAX_ENTITY >
	    dbg_buff.size) {
		rc = CUDBG_STATUS_SMALL_BUFF;
		total_size = cudbg_hdr->hdr_len;
		goto err;
	}

	/* If ext flag is set then move the offset to the end of the buf
	 * so that we can add ext entities
	 */
	if (flag_ext) {
		ext_entity_hdr = (struct cudbg_entity_hdr *)
			      ((char *)outbuf + cudbg_hdr->hdr_len +
			      (sizeof(struct cudbg_entity_hdr) *
			      (CUDBG_EXT_ENTITY - 1)));
		ext_entity_hdr->start_offset = cudbg_hdr->data_len;
		ext_entity_hdr->entity_type = CUDBG_EXT_ENTITY;
		ext_entity_hdr->size = 0;
		dbg_buff.offset = cudbg_hdr->data_len;
	} else {
		dbg_buff.offset += cudbg_hdr->hdr_len; /* move 24 bytes*/
		dbg_buff.offset += CUDBG_MAX_ENTITY *
					sizeof(struct cudbg_entity_hdr);
	}

	total_size = dbg_buff.offset;
	all = dbg_bitmap[0] & (1 << CUDBG_ALL);

	/*sort(large_entity_list);*/

	for (i = 1; i < CUDBG_MAX_ENTITY; i++) {
		index = i / 8;
		bit = i % 8;

		if (entity_list[i].bit == CUDBG_EXT_ENTITY)
			continue;

		if (all || (dbg_bitmap[index] & (1 << bit))) {

			if (!flag_ext) {
				rc = get_entity_hdr(outbuf, i, dbg_buff.size,
						    &entity_hdr);
				if (rc)
					cudbg_hdr->hdr_flags = rc;
			} else {
				rc = get_next_ext_entity_hdr(outbuf, &ext_size,
							     &dbg_buff,
							     &entity_hdr);
				if (rc)
					goto err;

				/* move the offset after the ext header */
				dbg_buff.offset +=
					sizeof(struct cudbg_entity_hdr);
			}

			entity_hdr->entity_type = i;
			entity_hdr->start_offset = dbg_buff.offset;
			/* process each entity by calling process_entity fp */
			remaining_buf_size = dbg_buff.size - dbg_buff.offset;

			if ((remaining_buf_size <= BUFFER_WARN_LIMIT) &&
			    is_large_entity(i)) {
				if (cudbg_init->verbose)
					cudbg_init->print("Skipping %s\n",
					    entity_list[i].name);
				skip_entity(i);
				continue;
			} else {

				/* If fw_attach is 0, then skip entities which
				 * communicates with firmware
				 */

				if (!is_fw_attached(cudbg_init) &&
				    (entity_list[i].flag &
				    (1 << ENTITY_FLAG_FW_NO_ATTACH))) {
					if (cudbg_init->verbose)
						cudbg_init->print("Skipping %s entity,"\
							  "because fw_attach "\
							  "is 0\n",
							  entity_list[i].name);
					continue;
				}

				if (cudbg_init->verbose)
					cudbg_init->print("collecting debug entity: "\
						  "%s\n", entity_list[i].name);
				memset(&cudbg_err, 0,
				       sizeof(struct cudbg_error));
				rc = process_entity[i-1](cudbg_init, &dbg_buff,
							 &cudbg_err);
			}

			if (rc) {
				entity_hdr->size = 0;
				dbg_buff.offset = entity_hdr->start_offset;
			} else
				align_debug_buffer(&dbg_buff, entity_hdr);

			if (cudbg_err.sys_err)
				rc = CUDBG_SYSTEM_ERROR;

			entity_hdr->hdr_flags =  rc;
			entity_hdr->sys_err = cudbg_err.sys_err;
			entity_hdr->sys_warn =	cudbg_err.sys_warn;

			/* We don't want to include ext entity size in global
			 * header
			 */
			if (!flag_ext)
				total_size += entity_hdr->size;

			cudbg_hdr->data_len = total_size;
			*outbuf_size = total_size;

			/* consider the size of the ext entity header and data
			 * also
			 */
			if (flag_ext) {
				ext_size += (sizeof(struct cudbg_entity_hdr) +
					     entity_hdr->size);
				entity_hdr->start_offset -= cudbg_hdr->data_len;
				ext_entity_hdr->size = ext_size;
				entity_hdr->next_ext_offset = ext_size;
				entity_hdr->flag |= CUDBG_EXT_DATA_VALID;
			}

			if (cudbg_init->use_flash) {
				if (flag_ext) {
					wr_entity_to_flash(handle,
							   &dbg_buff,
							   ext_entity_hdr->
							   start_offset,
							   entity_hdr->
							   size,
							   CUDBG_EXT_ENTITY,
							   ext_size);
				}
				else
					wr_entity_to_flash(handle,
							   &dbg_buff,
							   entity_hdr->\
							   start_offset,
							   entity_hdr->size,
							   i, ext_size);
			}
		}
	}

	for (i = 0; i < sizeof(large_entity_list) / sizeof(struct large_entity);
	     i++) {
		large_entity_code = large_entity_list[i].entity_code;
		if (large_entity_list[i].skip_flag) {
			if (!flag_ext) {
				rc = get_entity_hdr(outbuf, large_entity_code,
						    dbg_buff.size, &entity_hdr);
				if (rc)
					cudbg_hdr->hdr_flags = rc;
			} else {
				rc = get_next_ext_entity_hdr(outbuf, &ext_size,
							     &dbg_buff,
							     &entity_hdr);
				if (rc)
					goto err;

				dbg_buff.offset +=
					sizeof(struct cudbg_entity_hdr);
			}

			/* If fw_attach is 0, then skip entities which
			 * communicates with firmware
			 */
			if (!is_fw_attached(cudbg_init) &&
			    (entity_list[large_entity_code].flag &
			    (1 << ENTITY_FLAG_FW_NO_ATTACH))) {
				if (cudbg_init->verbose)
					cudbg_init->print("Skipping %s entity,"\
						  "because fw_attach "\
						  "is 0\n",
						  entity_list[large_entity_code]
						  .name);
				continue;
			}

			entity_hdr->entity_type = large_entity_code;
			entity_hdr->start_offset = dbg_buff.offset;
			if (cudbg_init->verbose)
				cudbg_init->print("Re-trying debug entity: %s\n",
					  entity_list[large_entity_code].name);

			memset(&cudbg_err, 0, sizeof(struct cudbg_error));
			rc = process_entity[large_entity_code - 1](cudbg_init,
								   &dbg_buff,
								   &cudbg_err);
			if (rc) {
				entity_hdr->size = 0;
				dbg_buff.offset = entity_hdr->start_offset;
			} else
				align_debug_buffer(&dbg_buff, entity_hdr);

			if (cudbg_err.sys_err)
				rc = CUDBG_SYSTEM_ERROR;

			entity_hdr->hdr_flags = rc;
			entity_hdr->sys_err = cudbg_err.sys_err;
			entity_hdr->sys_warn =	cudbg_err.sys_warn;

			/* We don't want to include ext entity size in global
			 * header
			 */
			if (!flag_ext)
				total_size += entity_hdr->size;

			cudbg_hdr->data_len = total_size;
			*outbuf_size = total_size;

			/* consider the size of the ext entity header and
			 * data also
			 */
			if (flag_ext) {
				ext_size += (sizeof(struct cudbg_entity_hdr) +
						   entity_hdr->size);
				entity_hdr->start_offset -=
							cudbg_hdr->data_len;
				ext_entity_hdr->size = ext_size;
				entity_hdr->flag |= CUDBG_EXT_DATA_VALID;
			}

			if (cudbg_init->use_flash) {
				if (flag_ext)
					wr_entity_to_flash(handle,
							   &dbg_buff,
							   ext_entity_hdr->
							   start_offset,
							   entity_hdr->size,
							   CUDBG_EXT_ENTITY,
							   ext_size);
				else
					wr_entity_to_flash(handle,
							   &dbg_buff,
							   entity_hdr->
							   start_offset,
							   entity_hdr->
							   size,
							   large_entity_list[i].
							   entity_code,
							   ext_size);
			}
		}
	}

	cudbg_hdr->data_len = total_size;
	*outbuf_size = total_size;

	if (flag_ext)
		*outbuf_size += ext_size;

	return 0;
err:
	return rc;
}

void reset_skip_entity(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(large_entity_list); i++)
		large_entity_list[i].skip_flag = 0;
}

void skip_entity(int entity_code)
{
	int i;
	for (i = 0; i < sizeof(large_entity_list) / sizeof(struct large_entity);
	     i++) {
		if (large_entity_list[i].entity_code == entity_code)
			large_entity_list[i].skip_flag = 1;
	}
}

int is_large_entity(int entity_code)
{
	int i;

	for (i = 0; i < sizeof(large_entity_list) / sizeof(struct large_entity);
	     i++) {
		if (large_entity_list[i].entity_code == entity_code)
			return 1;
	}
	return 0;
}

int get_entity_hdr(void *outbuf, int i, u32 size,
		   struct cudbg_entity_hdr **entity_hdr)
{
	int rc = 0;
	struct cudbg_hdr *cudbg_hdr = (struct cudbg_hdr *)outbuf;

	if (cudbg_hdr->hdr_len + (sizeof(struct cudbg_entity_hdr)*i) > size)
		return CUDBG_STATUS_SMALL_BUFF;

	*entity_hdr = (struct cudbg_entity_hdr *)
		      ((char *)outbuf+cudbg_hdr->hdr_len +
		       (sizeof(struct cudbg_entity_hdr)*(i-1)));
	return rc;
}

static int collect_rss(struct cudbg_init *pdbg_init,
		       struct cudbg_buffer *dbg_buff,
		       struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	u32 size;
	int rc = 0;

	size = RSS_NENTRIES  * sizeof(u16);
	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	rc = t4_read_rss(padap, (u16 *)scratch_buff.data);
	if (rc) {
		if (pdbg_init->verbose)
			pdbg_init->print("%s(), t4_read_rss failed!, rc: %d\n",
				 __func__, rc);
		cudbg_err->sys_err = rc;
		goto err1;
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_sw_state(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct sw_state *swstate;
	u32 size;
	int rc = 0;

	size = sizeof(struct sw_state);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	swstate = (struct sw_state *) scratch_buff.data;

	swstate->fw_state = t4_read_reg(padap, A_PCIE_FW);
	snprintf(swstate->caller_string, sizeof(swstate->caller_string), "%s",
	    "FreeBSD");
	swstate->os_type = 0;

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_ddp_stats(struct cudbg_init *pdbg_init,
			     struct cudbg_buffer *dbg_buff,
			     struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct tp_usm_stats  *tp_usm_stats_buff;
	u32 size;
	int rc = 0;

	size = sizeof(struct tp_usm_stats);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	tp_usm_stats_buff = (struct tp_usm_stats *) scratch_buff.data;

	/* spin_lock(&padap->stats_lock);	TODO*/
	t4_get_usm_stats(padap, tp_usm_stats_buff, 1);
	/* spin_unlock(&padap->stats_lock);	TODO*/

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_ulptx_la(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct struct_ulptx_la *ulptx_la_buff;
	u32 size, i, j;
	int rc = 0;

	size = sizeof(struct struct_ulptx_la);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	ulptx_la_buff = (struct struct_ulptx_la *) scratch_buff.data;

	for (i = 0; i < CUDBG_NUM_ULPTX; i++) {
		ulptx_la_buff->rdptr[i] = t4_read_reg(padap,
						      A_ULP_TX_LA_RDPTR_0 +
						      0x10 * i);
		ulptx_la_buff->wrptr[i] = t4_read_reg(padap,
						      A_ULP_TX_LA_WRPTR_0 +
						      0x10 * i);
		ulptx_la_buff->rddata[i] = t4_read_reg(padap,
						       A_ULP_TX_LA_RDDATA_0 +
						       0x10 * i);
		for (j = 0; j < CUDBG_NUM_ULPTX_READ; j++) {
			ulptx_la_buff->rd_data[i][j] =
				t4_read_reg(padap,
					    A_ULP_TX_LA_RDDATA_0 + 0x10 * i);
		}
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;

}

static int collect_ulprx_la(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct struct_ulprx_la *ulprx_la_buff;
	u32 size;
	int rc = 0;

	size = sizeof(struct struct_ulprx_la);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	ulprx_la_buff = (struct struct_ulprx_la *) scratch_buff.data;
	t4_ulprx_read_la(padap, (u32 *)ulprx_la_buff->data);
	ulprx_la_buff->size = ULPRX_LA_SIZE;

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_cpl_stats(struct cudbg_init *pdbg_init,
			     struct cudbg_buffer *dbg_buff,
			     struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct struct_tp_cpl_stats *tp_cpl_stats_buff;
	u32 size;
	int rc = 0;

	size = sizeof(struct struct_tp_cpl_stats);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	tp_cpl_stats_buff = (struct struct_tp_cpl_stats *) scratch_buff.data;
	tp_cpl_stats_buff->nchan = padap->chip_params->nchan;

	/* spin_lock(&padap->stats_lock);	TODO*/
	t4_tp_get_cpl_stats(padap, &tp_cpl_stats_buff->stats, 1);
	/* spin_unlock(&padap->stats_lock);	TODO*/

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_wc_stats(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct struct_wc_stats *wc_stats_buff;
	u32 val1;
	u32 val2;
	u32 size;

	int rc = 0;

	size = sizeof(struct struct_wc_stats);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	wc_stats_buff = (struct struct_wc_stats *) scratch_buff.data;

	if (!is_t4(padap)) {
		val1 = t4_read_reg(padap, A_SGE_STAT_TOTAL);
		val2 = t4_read_reg(padap, A_SGE_STAT_MATCH);
		wc_stats_buff->wr_cl_success = val1 - val2;
		wc_stats_buff->wr_cl_fail = val2;
	} else {
		wc_stats_buff->wr_cl_success = 0;
		wc_stats_buff->wr_cl_fail = 0;
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int mem_desc_cmp(const void *a, const void *b)
{
	return ((const struct struct_mem_desc *)a)->base -
		((const struct struct_mem_desc *)b)->base;
}

static int fill_meminfo(struct adapter *padap,
			struct struct_meminfo *meminfo_buff)
{
	struct struct_mem_desc *md;
	u32 size, lo, hi;
	u32 used, alloc;
	int n, i, rc = 0;

	size = sizeof(struct struct_meminfo);

	memset(meminfo_buff->avail, 0,
	       ARRAY_SIZE(meminfo_buff->avail) *
	       sizeof(struct struct_mem_desc));
	memset(meminfo_buff->mem, 0,
	       (ARRAY_SIZE(region) + 3) * sizeof(struct struct_mem_desc));
	md  = meminfo_buff->mem;

	for (i = 0; i < ARRAY_SIZE(meminfo_buff->mem); i++) {
		meminfo_buff->mem[i].limit = 0;
		meminfo_buff->mem[i].idx = i;
	}

	i = 0;

	lo = t4_read_reg(padap, A_MA_TARGET_MEM_ENABLE);

	if (lo & F_EDRAM0_ENABLE) {
		hi = t4_read_reg(padap, A_MA_EDRAM0_BAR);
		meminfo_buff->avail[i].base = G_EDRAM0_BASE(hi) << 20;
		meminfo_buff->avail[i].limit = meminfo_buff->avail[i].base +
					       (G_EDRAM0_SIZE(hi) << 20);
		meminfo_buff->avail[i].idx = 0;
		i++;
	}

	if (lo & F_EDRAM1_ENABLE) {
		hi =  t4_read_reg(padap, A_MA_EDRAM1_BAR);
		meminfo_buff->avail[i].base = G_EDRAM1_BASE(hi) << 20;
		meminfo_buff->avail[i].limit = meminfo_buff->avail[i].base +
					       (G_EDRAM1_SIZE(hi) << 20);
		meminfo_buff->avail[i].idx = 1;
		i++;
	}

	if (is_t5(padap)) {
		if (lo & F_EXT_MEM0_ENABLE) {
			hi = t4_read_reg(padap, A_MA_EXT_MEMORY0_BAR);
			meminfo_buff->avail[i].base = G_EXT_MEM_BASE(hi) << 20;
			meminfo_buff->avail[i].limit =
				meminfo_buff->avail[i].base +
				(G_EXT_MEM_SIZE(hi) << 20);
			meminfo_buff->avail[i].idx = 3;
			i++;
		}

		if (lo & F_EXT_MEM1_ENABLE) {
			hi = t4_read_reg(padap, A_MA_EXT_MEMORY1_BAR);
			meminfo_buff->avail[i].base = G_EXT_MEM1_BASE(hi) << 20;
			meminfo_buff->avail[i].limit =
				meminfo_buff->avail[i].base +
				(G_EXT_MEM1_SIZE(hi) << 20);
			meminfo_buff->avail[i].idx = 4;
			i++;
		}
	} else if (is_t6(padap)) {
		if (lo & F_EXT_MEM_ENABLE) {
			hi = t4_read_reg(padap, A_MA_EXT_MEMORY_BAR);
			meminfo_buff->avail[i].base = G_EXT_MEM_BASE(hi) << 20;
			meminfo_buff->avail[i].limit =
				meminfo_buff->avail[i].base +
				(G_EXT_MEM_SIZE(hi) << 20);
			meminfo_buff->avail[i].idx = 2;
			i++;
		}
	}

	if (!i) {				   /* no memory available */
		rc = CUDBG_STATUS_ENTITY_NOT_FOUND;
		goto err;
	}

	meminfo_buff->avail_c = i;
	qsort(meminfo_buff->avail, i, sizeof(struct struct_mem_desc),
	    mem_desc_cmp);
	(md++)->base = t4_read_reg(padap, A_SGE_DBQ_CTXT_BADDR);
	(md++)->base = t4_read_reg(padap, A_SGE_IMSG_CTXT_BADDR);
	(md++)->base = t4_read_reg(padap, A_SGE_FLM_CACHE_BADDR);
	(md++)->base = t4_read_reg(padap, A_TP_CMM_TCB_BASE);
	(md++)->base = t4_read_reg(padap, A_TP_CMM_MM_BASE);
	(md++)->base = t4_read_reg(padap, A_TP_CMM_TIMER_BASE);
	(md++)->base = t4_read_reg(padap, A_TP_CMM_MM_RX_FLST_BASE);
	(md++)->base = t4_read_reg(padap, A_TP_CMM_MM_TX_FLST_BASE);
	(md++)->base = t4_read_reg(padap, A_TP_CMM_MM_PS_FLST_BASE);

	/* the next few have explicit upper bounds */
	md->base = t4_read_reg(padap, A_TP_PMM_TX_BASE);
	md->limit = md->base - 1 +
		    t4_read_reg(padap,
				A_TP_PMM_TX_PAGE_SIZE) *
				G_PMTXMAXPAGE(t4_read_reg(padap,
							  A_TP_PMM_TX_MAX_PAGE)
					     );
	md++;

	md->base = t4_read_reg(padap, A_TP_PMM_RX_BASE);
	md->limit = md->base - 1 +
		    t4_read_reg(padap,
				A_TP_PMM_RX_PAGE_SIZE) *
				G_PMRXMAXPAGE(t4_read_reg(padap,
							  A_TP_PMM_RX_MAX_PAGE)
					      );
	md++;
	if (t4_read_reg(padap, A_LE_DB_CONFIG) & F_HASHEN) {
		if (chip_id(padap) <= CHELSIO_T5) {
			hi = t4_read_reg(padap, A_LE_DB_TID_HASHBASE) / 4;
			md->base = t4_read_reg(padap, A_LE_DB_HASH_TID_BASE);
		} else {
			hi = t4_read_reg(padap, A_LE_DB_HASH_TID_BASE);
			md->base = t4_read_reg(padap,
					       A_LE_DB_HASH_TBL_BASE_ADDR);
		}
		md->limit = 0;
	} else {
		md->base = 0;
		md->idx = ARRAY_SIZE(region);  /* hide it */
	}
	md++;
#define ulp_region(reg) \
	{\
		md->base = t4_read_reg(padap, A_ULP_ ## reg ## _LLIMIT);\
		(md++)->limit = t4_read_reg(padap, A_ULP_ ## reg ## _ULIMIT);\
	}

	ulp_region(RX_ISCSI);
	ulp_region(RX_TDDP);
	ulp_region(TX_TPT);
	ulp_region(RX_STAG);
	ulp_region(RX_RQ);
	ulp_region(RX_RQUDP);
	ulp_region(RX_PBL);
	ulp_region(TX_PBL);
#undef ulp_region
	md->base = 0;
	md->idx = ARRAY_SIZE(region);
	if (!is_t4(padap)) {
		u32 sge_ctrl = t4_read_reg(padap, A_SGE_CONTROL2);
		u32 fifo_size = t4_read_reg(padap, A_SGE_DBVFIFO_SIZE);
		if (is_t5(padap)) {
			if (sge_ctrl & F_VFIFO_ENABLE)
				size = G_DBVFIFO_SIZE(fifo_size);
		} else
			size = G_T6_DBVFIFO_SIZE(fifo_size);

		if (size) {
			md->base = G_BASEADDR(t4_read_reg(padap,
							  A_SGE_DBVFIFO_BADDR));
			md->limit = md->base + (size << 2) - 1;
		}
	}

	md++;

	md->base = t4_read_reg(padap, A_ULP_RX_CTX_BASE);
	md->limit = 0;
	md++;
	md->base = t4_read_reg(padap, A_ULP_TX_ERR_TABLE_BASE);
	md->limit = 0;
	md++;
#ifndef __NO_DRIVER_OCQ_SUPPORT__
	/*md->base = padap->vres.ocq.start;*/
	/*if (adap->vres.ocq.size)*/
	/*	  md->limit = md->base + adap->vres.ocq.size - 1;*/
	/*else*/
	md->idx = ARRAY_SIZE(region);  /* hide it */
	md++;
#endif

	/* add any address-space holes, there can be up to 3 */
	for (n = 0; n < i - 1; n++)
		if (meminfo_buff->avail[n].limit <
		    meminfo_buff->avail[n + 1].base)
			(md++)->base = meminfo_buff->avail[n].limit;

	if (meminfo_buff->avail[n].limit)
		(md++)->base = meminfo_buff->avail[n].limit;

	n = (int) (md - meminfo_buff->mem);
	meminfo_buff->mem_c = n;

	qsort(meminfo_buff->mem, n, sizeof(struct struct_mem_desc),
	    mem_desc_cmp);

	lo = t4_read_reg(padap, A_CIM_SDRAM_BASE_ADDR);
	hi = t4_read_reg(padap, A_CIM_SDRAM_ADDR_SIZE) + lo - 1;
	meminfo_buff->up_ram_lo = lo;
	meminfo_buff->up_ram_hi = hi;

	lo = t4_read_reg(padap, A_CIM_EXTMEM2_BASE_ADDR);
	hi = t4_read_reg(padap, A_CIM_EXTMEM2_ADDR_SIZE) + lo - 1;
	meminfo_buff->up_extmem2_lo = lo;
	meminfo_buff->up_extmem2_hi = hi;

	lo = t4_read_reg(padap, A_TP_PMM_RX_MAX_PAGE);
	meminfo_buff->rx_pages_data[0] =  G_PMRXMAXPAGE(lo);
	meminfo_buff->rx_pages_data[1] =
		t4_read_reg(padap, A_TP_PMM_RX_PAGE_SIZE) >> 10;
	meminfo_buff->rx_pages_data[2] = (lo & F_PMRXNUMCHN) ? 2 : 1 ;

	lo = t4_read_reg(padap, A_TP_PMM_TX_MAX_PAGE);
	hi = t4_read_reg(padap, A_TP_PMM_TX_PAGE_SIZE);
	meminfo_buff->tx_pages_data[0] = G_PMTXMAXPAGE(lo);
	meminfo_buff->tx_pages_data[1] =
		hi >= (1 << 20) ? (hi >> 20) : (hi >> 10);
	meminfo_buff->tx_pages_data[2] =
		hi >= (1 << 20) ? 'M' : 'K';
	meminfo_buff->tx_pages_data[3] = 1 << G_PMTXNUMCHN(lo);

	for (i = 0; i < 4; i++) {
		if (chip_id(padap) > CHELSIO_T5)
			lo = t4_read_reg(padap,
					 A_MPS_RX_MAC_BG_PG_CNT0 + i * 4);
		else
			lo = t4_read_reg(padap, A_MPS_RX_PG_RSV0 + i * 4);
		if (is_t5(padap)) {
			used = G_T5_USED(lo);
			alloc = G_T5_ALLOC(lo);
		} else {
			used = G_USED(lo);
			alloc = G_ALLOC(lo);
		}
		meminfo_buff->port_used[i] = used;
		meminfo_buff->port_alloc[i] = alloc;
	}

	for (i = 0; i < padap->chip_params->nchan; i++) {
		if (chip_id(padap) > CHELSIO_T5)
			lo = t4_read_reg(padap,
					 A_MPS_RX_LPBK_BG_PG_CNT0 + i * 4);
		else
			lo = t4_read_reg(padap, A_MPS_RX_PG_RSV4 + i * 4);
		if (is_t5(padap)) {
			used = G_T5_USED(lo);
			alloc = G_T5_ALLOC(lo);
		} else {
			used = G_USED(lo);
			alloc = G_ALLOC(lo);
		}
		meminfo_buff->loopback_used[i] = used;
		meminfo_buff->loopback_alloc[i] = alloc;
	}
err:
	return rc;
}

static int collect_meminfo(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct struct_meminfo *meminfo_buff;
	int rc = 0;
	u32 size;

	size = sizeof(struct struct_meminfo);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	meminfo_buff = (struct struct_meminfo *)scratch_buff.data;

	rc = fill_meminfo(padap, meminfo_buff);
	if (rc)
		goto err;

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_lb_stats(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct lb_port_stats *tmp_stats;
	struct struct_lb_stats *lb_stats_buff;
	u32 i, n, size;
	int rc = 0;

	rc = padap->params.nports;
	if (rc < 0)
		goto err;

	n = rc;
	size = sizeof(struct struct_lb_stats) +
	       n * sizeof(struct lb_port_stats);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	lb_stats_buff = (struct struct_lb_stats *) scratch_buff.data;

	lb_stats_buff->nchan = n;
	tmp_stats = lb_stats_buff->s;

	for (i = 0; i < n; i += 2, tmp_stats += 2) {
		t4_get_lb_stats(padap, i, tmp_stats);
		t4_get_lb_stats(padap, i + 1, tmp_stats+1);
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_rdma_stats(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_er)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct tp_rdma_stats *rdma_stats_buff;
	u32 size;
	int rc = 0;

	size = sizeof(struct tp_rdma_stats);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	rdma_stats_buff = (struct tp_rdma_stats *) scratch_buff.data;

	/* spin_lock(&padap->stats_lock);	TODO*/
	t4_tp_get_rdma_stats(padap, rdma_stats_buff, 1);
	/* spin_unlock(&padap->stats_lock);	TODO*/

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_clk_info(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	struct struct_clk_info *clk_info_buff;
	u64 tp_tick_us;
	int size;
	int rc = 0;

	if (!padap->params.vpd.cclk) {
		rc =  CUDBG_STATUS_CCLK_NOT_DEFINED;
		goto err;
	}

	size = sizeof(struct struct_clk_info);
	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	clk_info_buff = (struct struct_clk_info *) scratch_buff.data;

	clk_info_buff->cclk_ps = 1000000000 / padap->params.vpd.cclk;  /* in ps
	*/
	clk_info_buff->res = t4_read_reg(padap, A_TP_TIMER_RESOLUTION);
	clk_info_buff->tre = G_TIMERRESOLUTION(clk_info_buff->res);
	clk_info_buff->dack_re = G_DELAYEDACKRESOLUTION(clk_info_buff->res);
	tp_tick_us = (clk_info_buff->cclk_ps << clk_info_buff->tre) / 1000000;
	/* in us */
	clk_info_buff->dack_timer = ((clk_info_buff->cclk_ps <<
				      clk_info_buff->dack_re) / 1000000) *
				     t4_read_reg(padap, A_TP_DACK_TIMER);

	clk_info_buff->retransmit_min =
		tp_tick_us * t4_read_reg(padap, A_TP_RXT_MIN);
	clk_info_buff->retransmit_max =
		tp_tick_us * t4_read_reg(padap, A_TP_RXT_MAX);

	clk_info_buff->persist_timer_min =
		tp_tick_us * t4_read_reg(padap, A_TP_PERS_MIN);
	clk_info_buff->persist_timer_max =
		tp_tick_us * t4_read_reg(padap, A_TP_PERS_MAX);

	clk_info_buff->keepalive_idle_timer =
		tp_tick_us * t4_read_reg(padap, A_TP_KEEP_IDLE);
	clk_info_buff->keepalive_interval =
		tp_tick_us * t4_read_reg(padap, A_TP_KEEP_INTVL);

	clk_info_buff->initial_srtt =
		tp_tick_us * G_INITSRTT(t4_read_reg(padap, A_TP_INIT_SRTT));
	clk_info_buff->finwait2_timer =
		tp_tick_us * t4_read_reg(padap, A_TP_FINWAIT2_TIMER);

	rc = write_compression_hdr(&scratch_buff, dbg_buff);

	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;

}

static int collect_macstats(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct struct_mac_stats_rev1 *mac_stats_buff;
	u32 i, n, size;
	int rc = 0;

	rc = padap->params.nports;
	if (rc < 0)
		goto err;

	n = rc;
	size = sizeof(struct struct_mac_stats_rev1);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	mac_stats_buff = (struct struct_mac_stats_rev1 *) scratch_buff.data;

	mac_stats_buff->ver_hdr.signature = CUDBG_ENTITY_SIGNATURE;
	mac_stats_buff->ver_hdr.revision = CUDBG_MAC_STATS_REV;
	mac_stats_buff->ver_hdr.size = sizeof(struct struct_mac_stats_rev1) -
				       sizeof(struct cudbg_ver_hdr);

	mac_stats_buff->port_count = n;
	for (i = 0; i <  mac_stats_buff->port_count; i++)
		t4_get_port_stats(padap, i, &mac_stats_buff->stats[i]);

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_cim_pif_la(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct cim_pif_la *cim_pif_la_buff;
	u32 size;
	int rc = 0;

	size = sizeof(struct cim_pif_la) +
	       2 * CIM_PIFLA_SIZE * 6 * sizeof(u32);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	cim_pif_la_buff = (struct cim_pif_la *) scratch_buff.data;
	cim_pif_la_buff->size = CIM_PIFLA_SIZE;

	t4_cim_read_pif_la(padap, (u32 *)cim_pif_la_buff->data,
			   (u32 *)cim_pif_la_buff->data + 6 * CIM_PIFLA_SIZE,
			   NULL, NULL);

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_tp_la(struct cudbg_init *pdbg_init,
			 struct cudbg_buffer *dbg_buff,
			 struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct struct_tp_la *tp_la_buff;
	u32 size;
	int rc = 0;

	size = sizeof(struct struct_tp_la) + TPLA_SIZE *  sizeof(u64);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	tp_la_buff = (struct struct_tp_la *) scratch_buff.data;

	tp_la_buff->mode = G_DBGLAMODE(t4_read_reg(padap, A_TP_DBG_LA_CONFIG));
	t4_tp_read_la(padap, (u64 *)tp_la_buff->data, NULL);

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_fcoe_stats(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct struct_tp_fcoe_stats  *tp_fcoe_stats_buff;
	u32 size;
	int rc = 0;

	size = sizeof(struct struct_tp_fcoe_stats);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	tp_fcoe_stats_buff = (struct struct_tp_fcoe_stats *) scratch_buff.data;

	/* spin_lock(&padap->stats_lock);	TODO*/
	t4_get_fcoe_stats(padap, 0, &tp_fcoe_stats_buff->stats[0], 1);
	t4_get_fcoe_stats(padap, 1, &tp_fcoe_stats_buff->stats[1], 1);
	if (padap->chip_params->nchan == NCHAN) {
		t4_get_fcoe_stats(padap, 2, &tp_fcoe_stats_buff->stats[2], 1);
		t4_get_fcoe_stats(padap, 3, &tp_fcoe_stats_buff->stats[3], 1);
	}
	/* spin_unlock(&padap->stats_lock);	TODO*/

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_tp_err_stats(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct struct_tp_err_stats *tp_err_stats_buff;
	u32 size;
	int rc = 0;

	size = sizeof(struct struct_tp_err_stats);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	tp_err_stats_buff = (struct struct_tp_err_stats *) scratch_buff.data;

	/* spin_lock(&padap->stats_lock);	TODO*/
	t4_tp_get_err_stats(padap, &tp_err_stats_buff->stats, 1);
	/* spin_unlock(&padap->stats_lock);	TODO*/
	tp_err_stats_buff->nchan = padap->chip_params->nchan;

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_tcp_stats(struct cudbg_init *pdbg_init,
			     struct cudbg_buffer *dbg_buff,
			     struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct struct_tcp_stats *tcp_stats_buff;
	u32 size;
	int rc = 0;

	size = sizeof(struct struct_tcp_stats);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	tcp_stats_buff = (struct struct_tcp_stats *) scratch_buff.data;

	/* spin_lock(&padap->stats_lock);	TODO*/
	t4_tp_get_tcp_stats(padap, &tcp_stats_buff->v4, &tcp_stats_buff->v6, 1);
	/* spin_unlock(&padap->stats_lock);	TODO*/

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_hw_sched(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct struct_hw_sched *hw_sched_buff;
	u32 size;
	int i, rc = 0;

	if (!padap->params.vpd.cclk) {
		rc =  CUDBG_STATUS_CCLK_NOT_DEFINED;
		goto err;
	}

	size = sizeof(struct struct_hw_sched);
	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	hw_sched_buff = (struct struct_hw_sched *) scratch_buff.data;

	hw_sched_buff->map = t4_read_reg(padap, A_TP_TX_MOD_QUEUE_REQ_MAP);
	hw_sched_buff->mode = G_TIMERMODE(t4_read_reg(padap, A_TP_MOD_CONFIG));
	t4_read_pace_tbl(padap, hw_sched_buff->pace_tab);

	for (i = 0; i < NTX_SCHED; ++i) {
		t4_get_tx_sched(padap, i, &hw_sched_buff->kbps[i],
		    &hw_sched_buff->ipg[i], 1);
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_pm_stats(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct struct_pm_stats *pm_stats_buff;
	u32 size;
	int rc = 0;

	size = sizeof(struct struct_pm_stats);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	pm_stats_buff = (struct struct_pm_stats *) scratch_buff.data;

	t4_pmtx_get_stats(padap, pm_stats_buff->tx_cnt, pm_stats_buff->tx_cyc);
	t4_pmrx_get_stats(padap, pm_stats_buff->rx_cnt, pm_stats_buff->rx_cyc);

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_path_mtu(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	u32 size;
	int rc = 0;

	size = NMTUS  * sizeof(u16);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	t4_read_mtu_tbl(padap, (u16 *)scratch_buff.data, NULL);

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_rss_key(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	u32 size;

	int rc = 0;

	size = 10  * sizeof(u32);
	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	t4_read_rss_key(padap, (u32 *)scratch_buff.data, 1);

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_rss_config(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct rss_config *rss_conf;
	int rc;
	u32 size;

	size = sizeof(struct rss_config);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	rss_conf =  (struct rss_config *)scratch_buff.data;

	rss_conf->tp_rssconf = t4_read_reg(padap, A_TP_RSS_CONFIG);
	rss_conf->tp_rssconf_tnl = t4_read_reg(padap, A_TP_RSS_CONFIG_TNL);
	rss_conf->tp_rssconf_ofd = t4_read_reg(padap, A_TP_RSS_CONFIG_OFD);
	rss_conf->tp_rssconf_syn = t4_read_reg(padap, A_TP_RSS_CONFIG_SYN);
	rss_conf->tp_rssconf_vrt = t4_read_reg(padap, A_TP_RSS_CONFIG_VRT);
	rss_conf->tp_rssconf_cng = t4_read_reg(padap, A_TP_RSS_CONFIG_CNG);
	rss_conf->chip = padap->params.chipid;

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_rss_vf_config(struct cudbg_init *pdbg_init,
				 struct cudbg_buffer *dbg_buff,
				 struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	struct rss_vf_conf *vfconf;
	int vf, rc, vf_count;
	u32 size;

	vf_count = padap->chip_params->vfcount;
	size = vf_count * sizeof(*vfconf);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	vfconf =  (struct rss_vf_conf *)scratch_buff.data;

	for (vf = 0; vf < vf_count; vf++) {
		t4_read_rss_vf_config(padap, vf, &vfconf[vf].rss_vf_vfl,
				      &vfconf[vf].rss_vf_vfh, 1);
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_rss_pf_config(struct cudbg_init *pdbg_init,
				 struct cudbg_buffer *dbg_buff,
				 struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct rss_pf_conf *pfconf;
	struct adapter *padap = pdbg_init->adap;
	u32 rss_pf_map, rss_pf_mask, size;
	int pf, rc;

	size = 8  * sizeof(*pfconf);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	pfconf =  (struct rss_pf_conf *)scratch_buff.data;

	rss_pf_map = t4_read_rss_pf_map(padap, 1);
	rss_pf_mask = t4_read_rss_pf_mask(padap, 1);

	for (pf = 0; pf < 8; pf++) {
		pfconf[pf].rss_pf_map = rss_pf_map;
		pfconf[pf].rss_pf_mask = rss_pf_mask;
		/* no return val */
		t4_read_rss_pf_config(padap, pf, &pfconf[pf].rss_pf_config, 1);
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int check_valid(u32 *buf, int type)
{
	int index;
	int bit;
	int bit_pos = 0;

	switch (type) {
	case CTXT_EGRESS:
		bit_pos = 176;
		break;
	case CTXT_INGRESS:
		bit_pos = 141;
		break;
	case CTXT_FLM:
		bit_pos = 89;
		break;
	}
	index = bit_pos / 32;
	bit =  bit_pos % 32;

	return buf[index] & (1U << bit);
}

/**
 * Get EGRESS, INGRESS, FLM, and CNM max qid.
 *
 * For EGRESS and INGRESS, do the following calculation.
 * max_qid = (DBQ/IMSG context region size in bytes) /
 *	     (size of context in bytes).
 *
 * For FLM, do the following calculation.
 * max_qid = (FLM cache region size in bytes) /
 *	     ((number of pointers cached in EDRAM) * 8 (bytes per pointer)).
 *
 * There's a 1-to-1 mapping between FLM and CNM if there's no header splitting
 * enabled; i.e., max CNM qid is equal to max FLM qid. However, if header
 * splitting is enabled, then max CNM qid is half of max FLM qid.
 */
static int get_max_ctxt_qid(struct adapter *padap,
			    struct struct_meminfo *meminfo,
			    u32 *max_ctx_qid, u8 nelem)
{
	u32 i, idx, found = 0;

	if (nelem != (CTXT_CNM + 1))
		return -EINVAL;

	for (i = 0; i < meminfo->mem_c; i++) {
		if (meminfo->mem[i].idx >= ARRAY_SIZE(region))
			continue;                        /* skip holes */

		idx = meminfo->mem[i].idx;
		/* Get DBQ, IMSG, and FLM context region size */
		if (idx <= CTXT_FLM) {
			if (!(meminfo->mem[i].limit))
				meminfo->mem[i].limit =
					i < meminfo->mem_c - 1 ?
					meminfo->mem[i + 1].base - 1 : ~0;

			if (idx < CTXT_FLM) {
				/* Get EGRESS and INGRESS max qid. */
				max_ctx_qid[idx] = (meminfo->mem[i].limit -
						    meminfo->mem[i].base + 1) /
						   CUDBG_CTXT_SIZE_BYTES;
				found++;
			} else {
				/* Get FLM and CNM max qid. */
				u32 value, edram_ptr_count;
				u8 bytes_per_ptr = 8;
				u8 nohdr;

				value = t4_read_reg(padap, A_SGE_FLM_CFG);

				/* Check if header splitting is enabled. */
				nohdr = (value >> S_NOHDR) & 1U;

				/* Get the number of pointers in EDRAM per
				 * qid in units of 32.
				 */
				edram_ptr_count = 32 *
						  (1U << G_EDRAMPTRCNT(value));

				/* EDRAMPTRCNT value of 3 is reserved.
				 * So don't exceed 128.
				 */
				if (edram_ptr_count > 128)
					edram_ptr_count = 128;

				max_ctx_qid[idx] = (meminfo->mem[i].limit -
						    meminfo->mem[i].base + 1) /
						   (edram_ptr_count *
						    bytes_per_ptr);
				found++;

				/* CNM has 1-to-1 mapping with FLM.
				 * However, if header splitting is enabled,
				 * then max CNM qid is half of max FLM qid.
				 */
				max_ctx_qid[CTXT_CNM] = nohdr ?
							max_ctx_qid[idx] :
							max_ctx_qid[idx] >> 1;

				/* One more increment for CNM */
				found++;
			}
		}
		if (found == nelem)
			break;
	}

	/* Sanity check. Ensure the values are within known max. */
	max_ctx_qid[CTXT_EGRESS] = min_t(u32, max_ctx_qid[CTXT_EGRESS],
					 M_CTXTQID);
	max_ctx_qid[CTXT_INGRESS] = min_t(u32, max_ctx_qid[CTXT_INGRESS],
					  CUDBG_MAX_INGRESS_QIDS);
	max_ctx_qid[CTXT_FLM] = min_t(u32, max_ctx_qid[CTXT_FLM],
				      CUDBG_MAX_FL_QIDS);
	max_ctx_qid[CTXT_CNM] = min_t(u32, max_ctx_qid[CTXT_CNM],
				      CUDBG_MAX_CNM_QIDS);
	return 0;
}

static int collect_dump_context(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct cudbg_buffer temp_buff;
	struct adapter *padap = pdbg_init->adap;
	u32 size = 0, next_offset = 0, total_size = 0;
	struct cudbg_ch_cntxt *buff = NULL;
	struct struct_meminfo meminfo;
	int bytes = 0;
	int rc = 0;
	u32 i, j;
	u32 max_ctx_qid[CTXT_CNM + 1];
	bool limit_qid = false;
	u32 qid_count = 0;

	rc = fill_meminfo(padap, &meminfo);
	if (rc)
		goto err;

	/* Get max valid qid for each type of queue */
	rc = get_max_ctxt_qid(padap, &meminfo, max_ctx_qid, CTXT_CNM + 1);
	if (rc)
		goto err;

	/* There are four types of queues. Collect context upto max
	 * qid of each type of queue.
	 */
	for (i = CTXT_EGRESS; i <= CTXT_CNM; i++)
		size += sizeof(struct cudbg_ch_cntxt) * max_ctx_qid[i];

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc == CUDBG_STATUS_NO_SCRATCH_MEM) {
		/* Not enough scratch Memory available.
		 * Collect context of at least CUDBG_LOWMEM_MAX_CTXT_QIDS
		 * for each queue type.
		 */
		size = 0;
		for (i = CTXT_EGRESS; i <= CTXT_CNM; i++)
			size += sizeof(struct cudbg_ch_cntxt) *
				CUDBG_LOWMEM_MAX_CTXT_QIDS;

		limit_qid = true;
		rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
		if (rc)
			goto err;
	}

	buff = (struct cudbg_ch_cntxt *)scratch_buff.data;

	/* Collect context data */
	for (i = CTXT_EGRESS; i <= CTXT_FLM; i++) {
		qid_count = 0;
		for (j = 0; j < max_ctx_qid[i]; j++) {
			read_sge_ctxt(pdbg_init, j, i, buff->data);

			rc = check_valid(buff->data, i);
			if (rc) {
				buff->cntxt_type = i;
				buff->cntxt_id = j;
				buff++;
				total_size += sizeof(struct cudbg_ch_cntxt);

				if (i == CTXT_FLM) {
					read_sge_ctxt(pdbg_init, j, CTXT_CNM,
						      buff->data);
					buff->cntxt_type = CTXT_CNM;
					buff->cntxt_id = j;
					buff++;
					total_size +=
						sizeof(struct cudbg_ch_cntxt);
				}
				qid_count++;
			}

			/* If there's not enough space to collect more qids,
			 * then bail and move on to next queue type.
			 */
			if (limit_qid &&
			    qid_count >= CUDBG_LOWMEM_MAX_CTXT_QIDS)
				break;
		}
	}

	scratch_buff.size = total_size;
	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	/* Splitting buffer and writing in terms of CUDBG_CHUNK_SIZE */
	while (total_size > 0) {
		bytes = min_t(unsigned long, (unsigned long)total_size,
			      (unsigned long)CUDBG_CHUNK_SIZE);
		temp_buff.size = bytes;
		temp_buff.data = (void *)((char *)scratch_buff.data +
					  next_offset);

		rc = compress_buff(&temp_buff, dbg_buff);
		if (rc)
			goto err1;

		total_size -= bytes;
		next_offset += bytes;
	}

err1:
	scratch_buff.size = size;
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_fw_devlog(struct cudbg_init *pdbg_init,
			     struct cudbg_buffer *dbg_buff,
			     struct cudbg_error *cudbg_err)
{
#ifdef notyet
	struct adapter *padap = pdbg_init->adap;
	struct devlog_params *dparams = &padap->params.devlog;
	struct cudbg_param *params = NULL;
	struct cudbg_buffer scratch_buff;
	u32 offset;
	int rc = 0, i;

	rc = t4_init_devlog_params(padap, 1);

	if (rc < 0) {
		pdbg_init->print("%s(), t4_init_devlog_params failed!, rc: "\
				 "%d\n", __func__, rc);
		for (i = 0; i < pdbg_init->dbg_params_cnt; i++) {
			if (pdbg_init->dbg_params[i].param_type ==
			    CUDBG_DEVLOG_PARAM) {
				params = &pdbg_init->dbg_params[i];
				break;
			}
		}

		if (params) {
			dparams->memtype = params->u.devlog_param.memtype;
			dparams->start = params->u.devlog_param.start;
			dparams->size = params->u.devlog_param.size;
		} else {
			cudbg_err->sys_err = rc;
			goto err;
		}
	}

	rc = get_scratch_buff(dbg_buff, dparams->size, &scratch_buff);

	if (rc)
		goto err;

	/* Collect FW devlog */
	if (dparams->start != 0) {
		offset = scratch_buff.offset;
		rc = t4_memory_rw(padap, padap->params.drv_memwin,
				  dparams->memtype, dparams->start,
				  dparams->size,
				  (__be32 *)((char *)scratch_buff.data +
					     offset), 1);

		if (rc) {
			pdbg_init->print("%s(), t4_memory_rw failed!, rc: "\
					 "%d\n", __func__, rc);
			cudbg_err->sys_err = rc;
			goto err1;
		}
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);

	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
#endif
	return (EDOOFUS);
}
/* CIM OBQ */

static int collect_cim_obq_ulp0(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	int rc = 0, qid = 0;

	rc = read_cim_obq(pdbg_init, dbg_buff, cudbg_err, qid);

	return rc;
}

static int collect_cim_obq_ulp1(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	int rc = 0, qid = 1;

	rc = read_cim_obq(pdbg_init, dbg_buff, cudbg_err, qid);

	return rc;
}

static int collect_cim_obq_ulp2(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	int rc = 0, qid = 2;

	rc = read_cim_obq(pdbg_init, dbg_buff, cudbg_err, qid);

	return rc;
}

static int collect_cim_obq_ulp3(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	int rc = 0, qid = 3;

	rc = read_cim_obq(pdbg_init, dbg_buff, cudbg_err, qid);

	return rc;
}

static int collect_cim_obq_sge(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	int rc = 0, qid = 4;

	rc = read_cim_obq(pdbg_init, dbg_buff, cudbg_err, qid);

	return rc;
}

static int collect_cim_obq_ncsi(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	int rc = 0, qid = 5;

	rc = read_cim_obq(pdbg_init, dbg_buff, cudbg_err, qid);

	return rc;
}

static int collect_obq_sge_rx_q0(struct cudbg_init *pdbg_init,
				 struct cudbg_buffer *dbg_buff,
				 struct cudbg_error *cudbg_err)
{
	int rc = 0, qid = 6;

	rc = read_cim_obq(pdbg_init, dbg_buff, cudbg_err, qid);

	return rc;
}

static int collect_obq_sge_rx_q1(struct cudbg_init *pdbg_init,
				 struct cudbg_buffer *dbg_buff,
				 struct cudbg_error *cudbg_err)
{
	int rc = 0, qid = 7;

	rc = read_cim_obq(pdbg_init, dbg_buff, cudbg_err, qid);

	return rc;
}

static int read_cim_obq(struct cudbg_init *pdbg_init,
			struct cudbg_buffer *dbg_buff,
			struct cudbg_error *cudbg_err, int qid)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	u32 qsize;
	int rc;
	int no_of_read_words;

	/* collect CIM OBQ */
	qsize =  6 * CIM_OBQ_SIZE * 4 *  sizeof(u32);
	rc = get_scratch_buff(dbg_buff, qsize, &scratch_buff);
	if (rc)
		goto err;

	/* t4_read_cim_obq will return no. of read words or error */
	no_of_read_words = t4_read_cim_obq(padap, qid,
					   (u32 *)((u32 *)scratch_buff.data +
					   scratch_buff.offset), qsize);

	/* no_of_read_words is less than or equal to 0 means error */
	if (no_of_read_words <= 0) {
		if (no_of_read_words == 0)
			rc = CUDBG_SYSTEM_ERROR;
		else
			rc = no_of_read_words;
		if (pdbg_init->verbose)
			pdbg_init->print("%s: t4_read_cim_obq failed (%d)\n",
				 __func__, rc);
		cudbg_err->sys_err = rc;
		goto err1;
	}

	scratch_buff.size = no_of_read_words * 4;

	rc = write_compression_hdr(&scratch_buff, dbg_buff);

	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

	if (rc)
		goto err1;

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

/* CIM IBQ */

static int collect_cim_ibq_tp0(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	int rc = 0, qid = 0;

	rc = read_cim_ibq(pdbg_init, dbg_buff, cudbg_err, qid);
	return rc;
}

static int collect_cim_ibq_tp1(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	int rc = 0, qid = 1;

	rc = read_cim_ibq(pdbg_init, dbg_buff, cudbg_err, qid);
	return rc;
}

static int collect_cim_ibq_ulp(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	int rc = 0, qid = 2;

	rc = read_cim_ibq(pdbg_init, dbg_buff, cudbg_err, qid);
	return rc;
}

static int collect_cim_ibq_sge0(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	int rc = 0, qid = 3;

	rc = read_cim_ibq(pdbg_init, dbg_buff, cudbg_err, qid);
	return rc;
}

static int collect_cim_ibq_sge1(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	int rc = 0, qid = 4;

	rc = read_cim_ibq(pdbg_init, dbg_buff, cudbg_err, qid);
	return rc;
}

static int collect_cim_ibq_ncsi(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	int rc, qid = 5;

	rc = read_cim_ibq(pdbg_init, dbg_buff, cudbg_err, qid);
	return rc;
}

static int read_cim_ibq(struct cudbg_init *pdbg_init,
			struct cudbg_buffer *dbg_buff,
			struct cudbg_error *cudbg_err, int qid)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer scratch_buff;
	u32 qsize;
	int rc;
	int no_of_read_words;

	/* collect CIM IBQ */
	qsize = CIM_IBQ_SIZE * 4 *  sizeof(u32);
	rc = get_scratch_buff(dbg_buff, qsize, &scratch_buff);

	if (rc)
		goto err;

	/* t4_read_cim_ibq will return no. of read words or error */
	no_of_read_words = t4_read_cim_ibq(padap, qid,
					   (u32 *)((u32 *)scratch_buff.data +
					   scratch_buff.offset), qsize);
	/* no_of_read_words is less than or equal to 0 means error */
	if (no_of_read_words <= 0) {
		if (no_of_read_words == 0)
			rc = CUDBG_SYSTEM_ERROR;
		else
			rc = no_of_read_words;
		if (pdbg_init->verbose)
			pdbg_init->print("%s: t4_read_cim_ibq failed (%d)\n",
				 __func__, rc);
		cudbg_err->sys_err = rc;
		goto err1;
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);

err:
	return rc;
}

static int collect_cim_ma_la(struct cudbg_init *pdbg_init,
			     struct cudbg_buffer *dbg_buff,
			     struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	u32 rc = 0;

	/* collect CIM MA LA */
	scratch_buff.size =  2 * CIM_MALA_SIZE * 5 * sizeof(u32);
	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	/* no return */
	t4_cim_read_ma_la(padap,
			  (u32 *) ((char *)scratch_buff.data +
				   scratch_buff.offset),
			  (u32 *) ((char *)scratch_buff.data +
				   scratch_buff.offset + 5 * CIM_MALA_SIZE));

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_cim_la(struct cudbg_init *pdbg_init,
			  struct cudbg_buffer *dbg_buff,
			  struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;

	int rc;
	u32 cfg = 0;
	int size;

	/* collect CIM LA */
	if (is_t6(padap)) {
		size = padap->params.cim_la_size / 10 + 1;
		size *= 11 * sizeof(u32);
	} else {
		size = padap->params.cim_la_size / 8;
		size *= 8 * sizeof(u32);
	}

	size += sizeof(cfg);

	rc = get_scratch_buff(dbg_buff, size, &scratch_buff);
	if (rc)
		goto err;

	rc = t4_cim_read(padap, A_UP_UP_DBG_LA_CFG, 1, &cfg);

	if (rc) {
		if (pdbg_init->verbose)
			pdbg_init->print("%s: t4_cim_read failed (%d)\n",
				 __func__, rc);
		cudbg_err->sys_err = rc;
		goto err1;
	}

	memcpy((char *)scratch_buff.data + scratch_buff.offset, &cfg,
	       sizeof(cfg));

	rc = t4_cim_read_la(padap,
			    (u32 *) ((char *)scratch_buff.data +
				     scratch_buff.offset + sizeof(cfg)), NULL);
	if (rc < 0) {
		if (pdbg_init->verbose)
			pdbg_init->print("%s: t4_cim_read_la failed (%d)\n",
				 __func__, rc);
		cudbg_err->sys_err = rc;
		goto err1;
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_cim_qcfg(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	u32 offset;
	int cim_num_obq, rc = 0;

	struct struct_cim_qcfg *cim_qcfg_data = NULL;

	rc = get_scratch_buff(dbg_buff, sizeof(struct struct_cim_qcfg),
			      &scratch_buff);

	if (rc)
		goto err;

	offset = scratch_buff.offset;

	cim_num_obq = is_t4(padap) ? CIM_NUM_OBQ : CIM_NUM_OBQ_T5;

	cim_qcfg_data =
		(struct struct_cim_qcfg *)((u8 *)((char *)scratch_buff.data +
					   offset));

	rc = t4_cim_read(padap, A_UP_IBQ_0_RDADDR,
			 ARRAY_SIZE(cim_qcfg_data->stat), cim_qcfg_data->stat);

	if (rc) {
		if (pdbg_init->verbose)
			pdbg_init->print("%s: t4_cim_read IBQ_0_RDADDR failed (%d)\n",
			    __func__, rc);
		cudbg_err->sys_err = rc;
		goto err1;
	}

	rc = t4_cim_read(padap, A_UP_OBQ_0_REALADDR,
			 ARRAY_SIZE(cim_qcfg_data->obq_wr),
			 cim_qcfg_data->obq_wr);

	if (rc) {
		if (pdbg_init->verbose)
			pdbg_init->print("%s: t4_cim_read OBQ_0_REALADDR failed (%d)\n",
			    __func__, rc);
		cudbg_err->sys_err = rc;
		goto err1;
	}

	/* no return val */
	t4_read_cimq_cfg(padap,
			cim_qcfg_data->base,
			cim_qcfg_data->size,
			cim_qcfg_data->thres);

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

/**
 * Fetch the TX/RX payload regions start and end.
 *
 * @padap (IN): adapter handle.
 * @mem_type (IN): EDC0, EDC1, MC/MC0/MC1.
 * @mem_tot_len (IN): total length of @mem_type memory region to read.
 * @payload_type (IN): TX or RX Payload.
 * @reg_info (OUT): store the payload region info.
 *
 * Fetch the TX/RX payload region information from meminfo.
 * However, reading from the @mem_type region starts at 0 and not
 * from whatever base info is stored in meminfo.  Hence, if the
 * payload region exists, then calculate the payload region
 * start and end wrt 0 and @mem_tot_len, respectively, and set
 * @reg_info->exist to true. Otherwise, set @reg_info->exist to false.
 */
#ifdef notyet
static int get_payload_range(struct adapter *padap, u8 mem_type,
			     unsigned long mem_tot_len, u8 payload_type,
			     struct struct_region_info *reg_info)
{
	struct struct_meminfo meminfo;
	struct struct_mem_desc mem_region;
	struct struct_mem_desc payload;
	u32 i, idx, found = 0;
	u8 mc_type;
	int rc;

	/* Get meminfo of all regions */
	rc = fill_meminfo(padap, &meminfo);
	if (rc)
		return rc;

	/* Extract the specified TX or RX Payload region range */
	memset(&payload, 0, sizeof(struct struct_mem_desc));
	for (i = 0; i < meminfo.mem_c; i++) {
		if (meminfo.mem[i].idx >= ARRAY_SIZE(region))
			continue;                        /* skip holes */

		idx = meminfo.mem[i].idx;
		/* Get TX or RX Payload region start and end */
		if (idx == payload_type) {
			if (!(meminfo.mem[i].limit))
				meminfo.mem[i].limit =
					i < meminfo.mem_c - 1 ?
					meminfo.mem[i + 1].base - 1 : ~0;

			memcpy(&payload, &meminfo.mem[i], sizeof(payload));
			found = 1;
			break;
		}
	}

	/* If TX or RX Payload region is not found return error. */
	if (!found)
		return -EINVAL;

	if (mem_type < MEM_MC) {
		memcpy(&mem_region, &meminfo.avail[mem_type],
		       sizeof(mem_region));
	} else {
		/* Check if both MC0 and MC1 exist by checking if a
		 * base address for the specified @mem_type exists.
		 * If a base address exists, then there is MC1 and
		 * hence use the base address stored at index 3.
		 * Otherwise, use the base address stored at index 2.
		 */
		mc_type = meminfo.avail[mem_type].base ?
			  mem_type : mem_type - 1;
		memcpy(&mem_region, &meminfo.avail[mc_type],
		       sizeof(mem_region));
	}

	/* Check if payload region exists in current memory */
	if (payload.base < mem_region.base && payload.limit < mem_region.base) {
		reg_info->exist = false;
		return 0;
	}

	/* Get Payload region start and end with respect to 0 and
	 * mem_tot_len, respectively.  This is because reading from the
	 * memory region starts at 0 and not at base info stored in meminfo.
	 */
	if (payload.base < mem_region.limit) {
		reg_info->exist = true;
		if (payload.base >= mem_region.base)
			reg_info->start = payload.base - mem_region.base;
		else
			reg_info->start = 0;

		if (payload.limit < mem_region.limit)
			reg_info->end = payload.limit - mem_region.base;
		else
			reg_info->end = mem_tot_len;
	}

	return 0;
}
#endif

static int read_fw_mem(struct cudbg_init *pdbg_init,
			struct cudbg_buffer *dbg_buff, u8 mem_type,
			unsigned long tot_len, struct cudbg_error *cudbg_err)
{
#ifdef notyet
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	unsigned long bytes_read = 0;
	unsigned long bytes_left;
	unsigned long bytes;
	int	      rc;
	struct struct_region_info payload[2]; /* TX and RX Payload Region */
	u16 get_payload_flag;
	u8 i;

	get_payload_flag =
		pdbg_init->dbg_params[CUDBG_GET_PAYLOAD_PARAM].param_type;

	/* If explicitly asked to get TX/RX Payload data,
	 * then don't zero out the payload data. Otherwise,
	 * zero out the payload data.
	 */
	if (!get_payload_flag) {
		u8 region_index[2];
		u8 j = 0;

		/* Find the index of TX and RX Payload regions in meminfo */
		for (i = 0; i < ARRAY_SIZE(region); i++) {
			if (!strcmp(region[i], "Tx payload:") ||
			    !strcmp(region[i], "Rx payload:")) {
				region_index[j] = i;
				j++;
				if (j == 2)
					break;
			}
		}

		/* Get TX/RX Payload region range if they exist */
		memset(payload, 0, ARRAY_SIZE(payload) * sizeof(payload[0]));
		for (i = 0; i < ARRAY_SIZE(payload); i++) {
			rc = get_payload_range(padap, mem_type, tot_len,
					       region_index[i],
					       &payload[i]);
			if (rc)
				goto err;

			if (payload[i].exist) {
				/* Align start and end to avoid wrap around */
				payload[i].start =
					roundup(payload[i].start,
					    CUDBG_CHUNK_SIZE);
				payload[i].end =
					rounddown(payload[i].end,
					    CUDBG_CHUNK_SIZE);
			}
		}
	}

	bytes_left = tot_len;
	scratch_buff.size = tot_len;
	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err;

	while (bytes_left > 0) {
		bytes = min_t(unsigned long, bytes_left, (unsigned long)CUDBG_CHUNK_SIZE);
		rc = get_scratch_buff(dbg_buff, bytes, &scratch_buff);

		if (rc) {
			rc = CUDBG_STATUS_NO_SCRATCH_MEM;
			goto err;
		}

		if (!get_payload_flag) {
			for (i = 0; i < ARRAY_SIZE(payload); i++) {
				if (payload[i].exist &&
				    bytes_read >= payload[i].start &&
				    (bytes_read + bytes) <= payload[i].end) {
					memset(scratch_buff.data, 0, bytes);
					/* TX and RX Payload regions
					 * can't overlap.
					 */
					goto skip_read;
				}
			}
		}

		/* Read from file */
		/*fread(scratch_buff.data, 1, Bytes, in);*/
		rc = t4_memory_rw(padap, MEMWIN_NIC, mem_type, bytes_read,
				  bytes, (__be32 *)(scratch_buff.data), 1);

		if (rc) {
			if (pdbg_init->verbose)
				pdbg_init->print("%s: t4_memory_rw failed (%d)",
				    __func__, rc);
			cudbg_err->sys_err = rc;
			goto err1;
		}

skip_read:
		rc = compress_buff(&scratch_buff, dbg_buff);
		if (rc)
			goto err1;

		bytes_left -= bytes;
		bytes_read += bytes;
		release_scratch_buff(&scratch_buff, dbg_buff);
	}

err1:
	if (rc)
		release_scratch_buff(&scratch_buff, dbg_buff);

err:
	return rc;
#endif
	return (EDOOFUS);
}

static void collect_mem_info(struct cudbg_init *pdbg_init,
			     struct card_mem *mem_info)
{
	struct adapter *padap = pdbg_init->adap;
	u32 value;
	int t4 = 0;

	if (is_t4(padap))
		t4 = 1;

	if (t4) {
		value = t4_read_reg(padap, A_MA_EXT_MEMORY_BAR);
		value = G_EXT_MEM_SIZE(value);
		mem_info->size_mc0 = (u16)value;  /* size in MB */

		value = t4_read_reg(padap, A_MA_TARGET_MEM_ENABLE);
		if (value & F_EXT_MEM_ENABLE)
			mem_info->mem_flag |= (1 << MC0_FLAG); /* set mc0 flag
								  bit */
	} else {
		value = t4_read_reg(padap, A_MA_EXT_MEMORY0_BAR);
		value = G_EXT_MEM0_SIZE(value);
		mem_info->size_mc0 = (u16)value;

		value = t4_read_reg(padap, A_MA_EXT_MEMORY1_BAR);
		value = G_EXT_MEM1_SIZE(value);
		mem_info->size_mc1 = (u16)value;

		value = t4_read_reg(padap, A_MA_TARGET_MEM_ENABLE);
		if (value & F_EXT_MEM0_ENABLE)
			mem_info->mem_flag |= (1 << MC0_FLAG);
		if (value & F_EXT_MEM1_ENABLE)
			mem_info->mem_flag |= (1 << MC1_FLAG);
	}

	value = t4_read_reg(padap, A_MA_EDRAM0_BAR);
	value = G_EDRAM0_SIZE(value);
	mem_info->size_edc0 = (u16)value;

	value = t4_read_reg(padap, A_MA_EDRAM1_BAR);
	value = G_EDRAM1_SIZE(value);
	mem_info->size_edc1 = (u16)value;

	value = t4_read_reg(padap, A_MA_TARGET_MEM_ENABLE);
	if (value & F_EDRAM0_ENABLE)
		mem_info->mem_flag |= (1 << EDC0_FLAG);
	if (value & F_EDRAM1_ENABLE)
		mem_info->mem_flag |= (1 << EDC1_FLAG);

}

static void cudbg_t4_fwcache(struct cudbg_init *pdbg_init,
				struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	int rc;

	if (is_fw_attached(pdbg_init)) {

		/* Flush uP dcache before reading edcX/mcX  */
		rc = begin_synchronized_op(padap, NULL, SLEEP_OK | INTR_OK,
		    "t4cudl");
		if (rc == 0) {
			rc = t4_fwcache(padap, FW_PARAM_DEV_FWCACHE_FLUSH);
			end_synchronized_op(padap, 0);
		}

		if (rc) {
			if (pdbg_init->verbose)
				pdbg_init->print("%s: t4_fwcache failed (%d)\n",
				 __func__, rc);
			cudbg_err->sys_warn = rc;
		}
	}
}

static int collect_edc0_meminfo(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	struct card_mem mem_info = {0};
	unsigned long edc0_size;
	int rc;

	cudbg_t4_fwcache(pdbg_init, cudbg_err);

	collect_mem_info(pdbg_init, &mem_info);

	if (mem_info.mem_flag & (1 << EDC0_FLAG)) {
		edc0_size = (((unsigned long)mem_info.size_edc0) * 1024 * 1024);
		rc = read_fw_mem(pdbg_init, dbg_buff, MEM_EDC0,
				 edc0_size, cudbg_err);
		if (rc)
			goto err;

	} else {
		rc = CUDBG_STATUS_ENTITY_NOT_FOUND;
		if (pdbg_init->verbose)
			pdbg_init->print("%s(), collect_mem_info failed!, %s\n",
				 __func__, err_msg[-rc]);
		goto err;

	}
err:
	return rc;
}

static int collect_edc1_meminfo(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	struct card_mem mem_info = {0};
	unsigned long edc1_size;
	int rc;

	cudbg_t4_fwcache(pdbg_init, cudbg_err);

	collect_mem_info(pdbg_init, &mem_info);

	if (mem_info.mem_flag & (1 << EDC1_FLAG)) {
		edc1_size = (((unsigned long)mem_info.size_edc1) * 1024 * 1024);
		rc = read_fw_mem(pdbg_init, dbg_buff, MEM_EDC1,
				 edc1_size, cudbg_err);
		if (rc)
			goto err;
	} else {
		rc = CUDBG_STATUS_ENTITY_NOT_FOUND;
		if (pdbg_init->verbose)
			pdbg_init->print("%s(), collect_mem_info failed!, %s\n",
				 __func__, err_msg[-rc]);
		goto err;
	}

err:

	return rc;
}

static int collect_mc0_meminfo(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	struct card_mem mem_info = {0};
	unsigned long mc0_size;
	int rc;

	cudbg_t4_fwcache(pdbg_init, cudbg_err);

	collect_mem_info(pdbg_init, &mem_info);

	if (mem_info.mem_flag & (1 << MC0_FLAG)) {
		mc0_size = (((unsigned long)mem_info.size_mc0) * 1024 * 1024);
		rc = read_fw_mem(pdbg_init, dbg_buff, MEM_MC0,
				 mc0_size, cudbg_err);
		if (rc)
			goto err;
	} else {
		rc = CUDBG_STATUS_ENTITY_NOT_FOUND;
		if (pdbg_init->verbose)
			pdbg_init->print("%s(), collect_mem_info failed!, %s\n",
				 __func__, err_msg[-rc]);
		goto err;
	}

err:
	return rc;
}

static int collect_mc1_meminfo(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	struct card_mem mem_info = {0};
	unsigned long mc1_size;
	int rc;

	cudbg_t4_fwcache(pdbg_init, cudbg_err);

	collect_mem_info(pdbg_init, &mem_info);

	if (mem_info.mem_flag & (1 << MC1_FLAG)) {
		mc1_size = (((unsigned long)mem_info.size_mc1) * 1024 * 1024);
		rc = read_fw_mem(pdbg_init, dbg_buff, MEM_MC1,
				 mc1_size, cudbg_err);
		if (rc)
			goto err;
	} else {
		rc = CUDBG_STATUS_ENTITY_NOT_FOUND;

		if (pdbg_init->verbose)
			pdbg_init->print("%s(), collect_mem_info failed!, %s\n",
				 __func__, err_msg[-rc]);
		goto err;
	}
err:
	return rc;
}

static int collect_reg_dump(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct cudbg_buffer tmp_scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	unsigned long	     bytes_read = 0;
	unsigned long	     bytes_left;
	u32		     buf_size = 0, bytes = 0;
	int		     rc = 0;

	if (is_t4(padap))
		buf_size = T4_REGMAP_SIZE ;/*+ sizeof(unsigned int);*/
	else if (is_t5(padap) || is_t6(padap))
		buf_size = T5_REGMAP_SIZE;

	scratch_buff.size = buf_size;

	tmp_scratch_buff = scratch_buff;

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	/* no return */
	t4_get_regs(padap, (void *)scratch_buff.data, scratch_buff.size);
	bytes_left =   scratch_buff.size;

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	while (bytes_left > 0) {
		tmp_scratch_buff.data =
			((char *)scratch_buff.data) + bytes_read;
		bytes = min_t(unsigned long, bytes_left, (unsigned long)CUDBG_CHUNK_SIZE);
		tmp_scratch_buff.size = bytes;
		compress_buff(&tmp_scratch_buff, dbg_buff);
		bytes_left -= bytes;
		bytes_read += bytes;
	}

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_cctrl(struct cudbg_init *pdbg_init,
			 struct cudbg_buffer *dbg_buff,
			 struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	u32 size;
	int rc;

	size = sizeof(u16) * NMTUS * NCCTRL_WIN;
	scratch_buff.size = size;

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	t4_read_cong_tbl(padap, (void *)scratch_buff.data);

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int check_busy_bit(struct adapter *padap)
{
	u32 val;
	u32 busy = 1;
	int i = 0;
	int retry = 10;
	int status = 0;

	while (busy & (1 < retry)) {
		val = t4_read_reg(padap, A_CIM_HOST_ACC_CTRL);
		busy = (0 != (val & CUDBG_CIM_BUSY_BIT));
		i++;
	}

	if (busy)
		status = -1;

	return status;
}

static int cim_ha_rreg(struct adapter *padap, u32 addr, u32 *val)
{
	int rc = 0;

	/* write register address into the A_CIM_HOST_ACC_CTRL */
	t4_write_reg(padap, A_CIM_HOST_ACC_CTRL, addr);

	/* Poll HOSTBUSY */
	rc = check_busy_bit(padap);
	if (rc)
		goto err;

	/* Read value from A_CIM_HOST_ACC_DATA */
	*val = t4_read_reg(padap, A_CIM_HOST_ACC_DATA);

err:
	return rc;
}

static int dump_up_cim(struct adapter *padap, struct cudbg_init *pdbg_init,
		       struct ireg_field *up_cim_reg, u32 *buff)
{
	u32 i;
	int rc = 0;

	for (i = 0; i < up_cim_reg->ireg_offset_range; i++) {
		rc = cim_ha_rreg(padap,
				 up_cim_reg->ireg_local_offset + (i * 4),
				buff);
		if (rc) {
			if (pdbg_init->verbose)
				pdbg_init->print("BUSY timeout reading"
					 "CIM_HOST_ACC_CTRL\n");
			goto err;
		}

		buff++;
	}

err:
	return rc;
}

static int collect_up_cim_indirect(struct cudbg_init *pdbg_init,
				   struct cudbg_buffer *dbg_buff,
				   struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	struct ireg_buf *up_cim;
	u32 size;
	int i, rc, n;

	n = sizeof(t5_up_cim_reg_array) / (4 * sizeof(u32));
	size = sizeof(struct ireg_buf) * n;
	scratch_buff.size = size;

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	up_cim = (struct ireg_buf *)scratch_buff.data;

	for (i = 0; i < n; i++) {
		struct ireg_field *up_cim_reg = &up_cim->tp_pio;
		u32 *buff = up_cim->outbuf;

		if (is_t5(padap)) {
			up_cim_reg->ireg_addr = t5_up_cim_reg_array[i][0];
			up_cim_reg->ireg_data = t5_up_cim_reg_array[i][1];
			up_cim_reg->ireg_local_offset =
						t5_up_cim_reg_array[i][2];
			up_cim_reg->ireg_offset_range =
						t5_up_cim_reg_array[i][3];
		} else if (is_t6(padap)) {
			up_cim_reg->ireg_addr = t6_up_cim_reg_array[i][0];
			up_cim_reg->ireg_data = t6_up_cim_reg_array[i][1];
			up_cim_reg->ireg_local_offset =
						t6_up_cim_reg_array[i][2];
			up_cim_reg->ireg_offset_range =
						t6_up_cim_reg_array[i][3];
		}

		rc = dump_up_cim(padap, pdbg_init, up_cim_reg, buff);

		up_cim++;
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_mbox_log(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
#ifdef notyet
	struct cudbg_buffer scratch_buff;
	struct cudbg_mbox_log *mboxlog = NULL;
	struct mbox_cmd_log *log = NULL;
	struct mbox_cmd *entry;
	u64 flit;
	u32 size;
	unsigned int entry_idx;
	int i, k, rc;
	u16 mbox_cmds;

	if (pdbg_init->dbg_params[CUDBG_MBOX_LOG_PARAM].u.mboxlog_param.log) {
		log = pdbg_init->dbg_params[CUDBG_MBOX_LOG_PARAM].u.
			mboxlog_param.log;
		mbox_cmds = pdbg_init->dbg_params[CUDBG_MBOX_LOG_PARAM].u.
				mboxlog_param.mbox_cmds;
	} else {
		if (pdbg_init->verbose)
			pdbg_init->print("Mbox log is not requested\n");
		return CUDBG_STATUS_ENTITY_NOT_REQUESTED;
	}

	size = sizeof(struct cudbg_mbox_log) * mbox_cmds;
	scratch_buff.size = size;
	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	mboxlog = (struct cudbg_mbox_log *)scratch_buff.data;

	for (k = 0; k < mbox_cmds; k++) {
		entry_idx = log->cursor + k;
		if (entry_idx >= log->size)
			entry_idx -= log->size;
		entry = mbox_cmd_log_entry(log, entry_idx);

		/* skip over unused entries */
		if (entry->timestamp == 0)
			continue;

		memcpy(&mboxlog->entry, entry, sizeof(struct mbox_cmd));

		for (i = 0; i < MBOX_LEN / 8; i++) {
			flit = entry->cmd[i];
			mboxlog->hi[i] = (u32)(flit >> 32);
			mboxlog->lo[i] = (u32)flit;
		}

		mboxlog++;
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
#endif
	return (EDOOFUS);
}

static int collect_pbt_tables(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_pbt_tables *pbt = NULL;
	u32 size;
	u32 addr;
	int i, rc;

	size = sizeof(struct cudbg_pbt_tables);
	scratch_buff.size = size;

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	pbt = (struct cudbg_pbt_tables *)scratch_buff.data;

	/* PBT dynamic entries */
	addr = CUDBG_CHAC_PBT_ADDR;
	for (i = 0; i < CUDBG_PBT_DYNAMIC_ENTRIES; i++) {
		rc = cim_ha_rreg(padap, addr + (i * 4), &pbt->pbt_dynamic[i]);
		if (rc) {
			if (pdbg_init->verbose)
				pdbg_init->print("BUSY timeout reading"
					 "CIM_HOST_ACC_CTRL\n");
			goto err1;
		}
	}

	/* PBT static entries */

	/* static entries start when bit 6 is set */
	addr = CUDBG_CHAC_PBT_ADDR + (1 << 6);
	for (i = 0; i < CUDBG_PBT_STATIC_ENTRIES; i++) {
		rc = cim_ha_rreg(padap, addr + (i * 4), &pbt->pbt_static[i]);
		if (rc) {
			if (pdbg_init->verbose)
				pdbg_init->print("BUSY timeout reading"
					 "CIM_HOST_ACC_CTRL\n");
			goto err1;
		}
	}

	/* LRF entries */
	addr = CUDBG_CHAC_PBT_LRF;
	for (i = 0; i < CUDBG_LRF_ENTRIES; i++) {
		rc = cim_ha_rreg(padap, addr + (i * 4), &pbt->lrf_table[i]);
		if (rc) {
			if (pdbg_init->verbose)
				pdbg_init->print("BUSY timeout reading"
					 "CIM_HOST_ACC_CTRL\n");
			goto err1;
		}
	}

	/* PBT data entries */
	addr = CUDBG_CHAC_PBT_DATA;
	for (i = 0; i < CUDBG_PBT_DATA_ENTRIES; i++) {
		rc = cim_ha_rreg(padap, addr + (i * 4), &pbt->pbt_data[i]);
		if (rc) {
			if (pdbg_init->verbose)
				pdbg_init->print("BUSY timeout reading"
					 "CIM_HOST_ACC_CTRL\n");
			goto err1;
		}
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_pm_indirect(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	struct ireg_buf *ch_pm;
	u32 size;
	int i, rc, n;

	n = sizeof(t5_pm_rx_array) / (4 * sizeof(u32));
	size = sizeof(struct ireg_buf) * n * 2;
	scratch_buff.size = size;

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	ch_pm = (struct ireg_buf *)scratch_buff.data;

	/*PM_RX*/
	for (i = 0; i < n; i++) {
		struct ireg_field *pm_pio = &ch_pm->tp_pio;
		u32 *buff = ch_pm->outbuf;

		pm_pio->ireg_addr = t5_pm_rx_array[i][0];
		pm_pio->ireg_data = t5_pm_rx_array[i][1];
		pm_pio->ireg_local_offset = t5_pm_rx_array[i][2];
		pm_pio->ireg_offset_range = t5_pm_rx_array[i][3];

		t4_read_indirect(padap,
				pm_pio->ireg_addr,
				pm_pio->ireg_data,
				buff,
				pm_pio->ireg_offset_range,
				pm_pio->ireg_local_offset);

		ch_pm++;
	}

	/*PM_Tx*/
	n = sizeof(t5_pm_tx_array) / (4 * sizeof(u32));
	for (i = 0; i < n; i++) {
		struct ireg_field *pm_pio = &ch_pm->tp_pio;
		u32 *buff = ch_pm->outbuf;

		pm_pio->ireg_addr = t5_pm_tx_array[i][0];
		pm_pio->ireg_data = t5_pm_tx_array[i][1];
		pm_pio->ireg_local_offset = t5_pm_tx_array[i][2];
		pm_pio->ireg_offset_range = t5_pm_tx_array[i][3];

		t4_read_indirect(padap,
				pm_pio->ireg_addr,
				pm_pio->ireg_data,
				buff,
				pm_pio->ireg_offset_range,
				pm_pio->ireg_local_offset);

		ch_pm++;
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;

}

static int collect_tid(struct cudbg_init *pdbg_init,
		       struct cudbg_buffer *dbg_buff,
		       struct cudbg_error *cudbg_err)
{

	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	struct tid_info_region *tid;
	struct tid_info_region_rev1 *tid1;
	u32 para[7], val[7];
	u32 mbox, pf;
	int rc;

	scratch_buff.size = sizeof(struct tid_info_region_rev1);

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

#define FW_PARAM_DEV_A(param) \
	(V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_DEV) | \
	 V_FW_PARAMS_PARAM_X(FW_PARAMS_PARAM_DEV_##param))
#define FW_PARAM_PFVF_A(param) \
	(V_FW_PARAMS_MNEM(FW_PARAMS_MNEM_PFVF) | \
	 V_FW_PARAMS_PARAM_X(FW_PARAMS_PARAM_PFVF_##param)|  \
	 V_FW_PARAMS_PARAM_Y(0) | \
	 V_FW_PARAMS_PARAM_Z(0))
#define MAX_ATIDS_A 8192U

	tid1 = (struct tid_info_region_rev1 *)scratch_buff.data;
	tid = &(tid1->tid);
	tid1->ver_hdr.signature = CUDBG_ENTITY_SIGNATURE;
	tid1->ver_hdr.revision = CUDBG_TID_INFO_REV;
	tid1->ver_hdr.size = sizeof(struct tid_info_region_rev1) -
			     sizeof(struct cudbg_ver_hdr);

	if (is_t5(padap)) {
		tid->hash_base = t4_read_reg(padap, A_LE_DB_TID_HASHBASE);
		tid1->tid_start = 0;
	} else if (is_t6(padap)) {
		tid->hash_base = t4_read_reg(padap, A_T6_LE_DB_HASH_TID_BASE);
		tid1->tid_start = t4_read_reg(padap, A_LE_DB_ACTIVE_TABLE_START_INDEX);
	}

	tid->le_db_conf = t4_read_reg(padap, A_LE_DB_CONFIG);

	para[0] = FW_PARAM_PFVF_A(FILTER_START);
	para[1] = FW_PARAM_PFVF_A(FILTER_END);
	para[2] = FW_PARAM_PFVF_A(ACTIVE_FILTER_START);
	para[3] = FW_PARAM_PFVF_A(ACTIVE_FILTER_END);
	para[4] = FW_PARAM_DEV_A(NTID);
	para[5] = FW_PARAM_PFVF_A(SERVER_START);
	para[6] = FW_PARAM_PFVF_A(SERVER_END);

	rc = begin_synchronized_op(padap, NULL, SLEEP_OK | INTR_OK, "t4cudq");
	if (rc)
		goto err;
	mbox = padap->mbox;
	pf = padap->pf;
	rc = t4_query_params(padap, mbox, pf, 0, 7, para, val);
	if (rc <  0) {
		if (rc == -FW_EPERM) {
			/* It looks like we don't have permission to use
			 * padap->mbox.
			 *
			 * Try mbox 4.  If it works, we'll continue to
			 * collect the rest of tid info from mbox 4.
			 * Else, quit trying to collect tid info.
			 */
			mbox = 4;
			pf = 4;
			rc = t4_query_params(padap, mbox, pf, 0, 7, para, val);
			if (rc < 0) {
				cudbg_err->sys_err = rc;
				goto err1;
			}
		} else {
			cudbg_err->sys_err = rc;
			goto err1;
		}
	}

	tid->ftid_base = val[0];
	tid->nftids = val[1] - val[0] + 1;
	/*active filter region*/
	if (val[2] != val[3]) {
#ifdef notyet
		tid->flags |= FW_OFLD_CONN;
#endif
		tid->aftid_base = val[2];
		tid->aftid_end = val[3];
	}
	tid->ntids = val[4];
	tid->natids = min_t(u32, tid->ntids / 2, MAX_ATIDS_A);
	tid->stid_base = val[5];
	tid->nstids = val[6] - val[5] + 1;

	if (chip_id(padap) >= CHELSIO_T6) {
		para[0] = FW_PARAM_PFVF_A(HPFILTER_START);
		para[1] = FW_PARAM_PFVF_A(HPFILTER_END);
		rc = t4_query_params(padap, mbox, pf, 0, 2, para, val);
		if (rc < 0) {
			cudbg_err->sys_err = rc;
			goto err1;
		}

		tid->hpftid_base = val[0];
		tid->nhpftids = val[1] - val[0] + 1;
	}

	if (chip_id(padap) <= CHELSIO_T5) {
		tid->sb = t4_read_reg(padap, A_LE_DB_SERVER_INDEX) / 4;
		tid->hash_base /= 4;
	} else
		tid->sb = t4_read_reg(padap, A_LE_DB_SRVR_START_INDEX);

	/*UO context range*/
	para[0] = FW_PARAM_PFVF_A(ETHOFLD_START);
	para[1] = FW_PARAM_PFVF_A(ETHOFLD_END);

	rc = t4_query_params(padap, mbox, pf, 0, 2, para, val);
	if (rc <  0) {
		cudbg_err->sys_err = rc;
		goto err1;
	}

	if (val[0] != val[1]) {
		tid->uotid_base = val[0];
		tid->nuotids = val[1] - val[0] + 1;
	}
	tid->IP_users = t4_read_reg(padap, A_LE_DB_ACT_CNT_IPV4);
	tid->IPv6_users = t4_read_reg(padap, A_LE_DB_ACT_CNT_IPV6);

#undef FW_PARAM_PFVF_A
#undef FW_PARAM_DEV_A
#undef MAX_ATIDS_A

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;
	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	end_synchronized_op(padap, 0);
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_tx_rate(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	struct tx_rate *tx_rate;
	u32 size;
	int rc;

	size = sizeof(struct tx_rate);
	scratch_buff.size = size;

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	tx_rate = (struct tx_rate *)scratch_buff.data;
	t4_get_chan_txrate(padap, tx_rate->nrate, tx_rate->orate);
	tx_rate->nchan = padap->chip_params->nchan;

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static inline void cudbg_tcamxy2valmask(u64 x, u64 y, u8 *addr, u64 *mask)
{
	*mask = x | y;
	y = (__force u64)cpu_to_be64(y);
	memcpy(addr, (char *)&y + 2, ETH_ALEN);
}

static void mps_rpl_backdoor(struct adapter *padap, struct fw_ldst_mps_rplc *mps_rplc)
{
	if (is_t5(padap)) {
		mps_rplc->rplc255_224 = htonl(t4_read_reg(padap,
							  A_MPS_VF_RPLCT_MAP3));
		mps_rplc->rplc223_192 = htonl(t4_read_reg(padap,
							  A_MPS_VF_RPLCT_MAP2));
		mps_rplc->rplc191_160 = htonl(t4_read_reg(padap,
							  A_MPS_VF_RPLCT_MAP1));
		mps_rplc->rplc159_128 = htonl(t4_read_reg(padap,
							  A_MPS_VF_RPLCT_MAP0));
	} else {
		mps_rplc->rplc255_224 = htonl(t4_read_reg(padap,
							  A_MPS_VF_RPLCT_MAP7));
		mps_rplc->rplc223_192 = htonl(t4_read_reg(padap,
							  A_MPS_VF_RPLCT_MAP6));
		mps_rplc->rplc191_160 = htonl(t4_read_reg(padap,
							  A_MPS_VF_RPLCT_MAP5));
		mps_rplc->rplc159_128 = htonl(t4_read_reg(padap,
							  A_MPS_VF_RPLCT_MAP4));
	}
	mps_rplc->rplc127_96 = htonl(t4_read_reg(padap, A_MPS_VF_RPLCT_MAP3));
	mps_rplc->rplc95_64 = htonl(t4_read_reg(padap, A_MPS_VF_RPLCT_MAP2));
	mps_rplc->rplc63_32 = htonl(t4_read_reg(padap, A_MPS_VF_RPLCT_MAP1));
	mps_rplc->rplc31_0 = htonl(t4_read_reg(padap, A_MPS_VF_RPLCT_MAP0));
}

static int collect_mps_tcam(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_mps_tcam *tcam = NULL;
	u32 size = 0, i, n, total_size = 0;
	u32 ctl, data2;
	u64 tcamy, tcamx, val;
	int rc;

	n = padap->chip_params->mps_tcam_size;
	size = sizeof(struct cudbg_mps_tcam) * n;
	scratch_buff.size = size;

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;
	memset(scratch_buff.data, 0, size);

	tcam = (struct cudbg_mps_tcam *)scratch_buff.data;
	for (i = 0; i < n; i++) {
		if (chip_id(padap) >= CHELSIO_T6) {
			/* CtlReqID   - 1: use Host Driver Requester ID
			 * CtlCmdType - 0: Read, 1: Write
			 * CtlTcamSel - 0: TCAM0, 1: TCAM1
			 * CtlXYBitSel- 0: Y bit, 1: X bit
			 */

			/* Read tcamy */
			ctl = (V_CTLREQID(1) |
			       V_CTLCMDTYPE(0) | V_CTLXYBITSEL(0));
			if (i < 256)
				ctl |= V_CTLTCAMINDEX(i) | V_CTLTCAMSEL(0);
			else
				ctl |= V_CTLTCAMINDEX(i - 256) |
				       V_CTLTCAMSEL(1);

			t4_write_reg(padap, A_MPS_CLS_TCAM_DATA2_CTL, ctl);
			val = t4_read_reg(padap, A_MPS_CLS_TCAM_RDATA1_REQ_ID1);
			tcamy = G_DMACH(val) << 32;
			tcamy |= t4_read_reg(padap, A_MPS_CLS_TCAM_RDATA0_REQ_ID1);
			data2 = t4_read_reg(padap, A_MPS_CLS_TCAM_RDATA2_REQ_ID1);
			tcam->lookup_type = G_DATALKPTYPE(data2);

			/* 0 - Outer header, 1 - Inner header
			 * [71:48] bit locations are overloaded for
			 * outer vs. inner lookup types.
			 */

			if (tcam->lookup_type &&
			    (tcam->lookup_type != M_DATALKPTYPE)) {
				/* Inner header VNI */
				tcam->vniy = ((data2 & F_DATAVIDH2) << 23) |
					     (G_DATAVIDH1(data2) << 16) |
					     G_VIDL(val);
				tcam->dip_hit = data2 & F_DATADIPHIT;
			} else {
				tcam->vlan_vld = data2 & F_DATAVIDH2;
				tcam->ivlan = G_VIDL(val);
			}

			tcam->port_num = G_DATAPORTNUM(data2);

			/* Read tcamx. Change the control param */
			ctl |= V_CTLXYBITSEL(1);
			t4_write_reg(padap, A_MPS_CLS_TCAM_DATA2_CTL, ctl);
			val = t4_read_reg(padap, A_MPS_CLS_TCAM_RDATA1_REQ_ID1);
			tcamx = G_DMACH(val) << 32;
			tcamx |= t4_read_reg(padap, A_MPS_CLS_TCAM_RDATA0_REQ_ID1);
			data2 = t4_read_reg(padap, A_MPS_CLS_TCAM_RDATA2_REQ_ID1);
			if (tcam->lookup_type &&
			    (tcam->lookup_type != M_DATALKPTYPE)) {
				/* Inner header VNI mask */
				tcam->vnix = ((data2 & F_DATAVIDH2) << 23) |
					     (G_DATAVIDH1(data2) << 16) |
					     G_VIDL(val);
			}
		} else {
			tcamy = t4_read_reg64(padap, MPS_CLS_TCAM_Y_L(i));
			tcamx = t4_read_reg64(padap, MPS_CLS_TCAM_X_L(i));
		}

		if (tcamx & tcamy)
			continue;

		tcam->cls_lo = t4_read_reg(padap, MPS_CLS_SRAM_L(i));
		tcam->cls_hi = t4_read_reg(padap, MPS_CLS_SRAM_H(i));

		if (is_t5(padap))
			tcam->repli = (tcam->cls_lo & F_REPLICATE);
		else if (is_t6(padap))
			tcam->repli = (tcam->cls_lo & F_T6_REPLICATE);

		if (tcam->repli) {
			struct fw_ldst_cmd ldst_cmd;
			struct fw_ldst_mps_rplc mps_rplc;

			memset(&ldst_cmd, 0, sizeof(ldst_cmd));
			ldst_cmd.op_to_addrspace =
				htonl(V_FW_CMD_OP(FW_LDST_CMD) |
				      F_FW_CMD_REQUEST |
				      F_FW_CMD_READ |
				      V_FW_LDST_CMD_ADDRSPACE(
					      FW_LDST_ADDRSPC_MPS));

			ldst_cmd.cycles_to_len16 = htonl(FW_LEN16(ldst_cmd));

			ldst_cmd.u.mps.rplc.fid_idx =
				htons(V_FW_LDST_CMD_FID(FW_LDST_MPS_RPLC) |
				      V_FW_LDST_CMD_IDX(i));

			rc = begin_synchronized_op(padap, NULL,
			    SLEEP_OK | INTR_OK, "t4cudm");
			if (rc == 0) {
				rc = t4_wr_mbox(padap, padap->mbox, &ldst_cmd,
						sizeof(ldst_cmd), &ldst_cmd);
				end_synchronized_op(padap, 0);
			}

			if (rc)
				mps_rpl_backdoor(padap, &mps_rplc);
			else
				mps_rplc = ldst_cmd.u.mps.rplc;

			tcam->rplc[0] = ntohl(mps_rplc.rplc31_0);
			tcam->rplc[1] = ntohl(mps_rplc.rplc63_32);
			tcam->rplc[2] = ntohl(mps_rplc.rplc95_64);
			tcam->rplc[3] = ntohl(mps_rplc.rplc127_96);
			if (padap->chip_params->mps_rplc_size >
					CUDBG_MAX_RPLC_SIZE) {
				tcam->rplc[4] = ntohl(mps_rplc.rplc159_128);
				tcam->rplc[5] = ntohl(mps_rplc.rplc191_160);
				tcam->rplc[6] = ntohl(mps_rplc.rplc223_192);
				tcam->rplc[7] = ntohl(mps_rplc.rplc255_224);
			}
		}
		cudbg_tcamxy2valmask(tcamx, tcamy, tcam->addr, &tcam->mask);

		tcam->idx = i;
		tcam->rplc_size = padap->chip_params->mps_rplc_size;

		total_size += sizeof(struct cudbg_mps_tcam);

		tcam++;
	}

	if (total_size == 0) {
		rc = CUDBG_SYSTEM_ERROR;
		goto err1;
	}

	scratch_buff.size = total_size;
	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	scratch_buff.size = size;
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_pcie_config(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	u32 size, *value, j;
	int i, rc, n;

	size = sizeof(u32) * NUM_PCIE_CONFIG_REGS;
	n = sizeof(t5_pcie_config_array) / (2 * sizeof(u32));
	scratch_buff.size = size;

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	value = (u32 *)scratch_buff.data;
	for (i = 0; i < n; i++) {
		for (j = t5_pcie_config_array[i][0];
		     j <= t5_pcie_config_array[i][1]; j += 4) {
			*value++ = t4_hw_pci_read_cfg4(padap, j);
		}
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int cudbg_read_tid(struct cudbg_init *pdbg_init, u32 tid,
			  struct cudbg_tid_data *tid_data)
{
	int i, cmd_retry = 8;
	struct adapter *padap = pdbg_init->adap;
	u32 val;

	/* Fill REQ_DATA regs with 0's */
	for (i = 0; i < CUDBG_NUM_REQ_REGS; i++)
		t4_write_reg(padap, A_LE_DB_DBGI_REQ_DATA + (i << 2), 0);

	/* Write DBIG command */
	val = (0x4 << S_DBGICMD) | tid;
	t4_write_reg(padap, A_LE_DB_DBGI_REQ_TCAM_CMD, val);
	tid_data->dbig_cmd = val;

	val = 0;
	val |= 1 << S_DBGICMDSTRT;
	val |= 1;  /* LE mode */
	t4_write_reg(padap, A_LE_DB_DBGI_CONFIG, val);
	tid_data->dbig_conf = val;

	/* Poll the DBGICMDBUSY bit */
	val = 1;
	while (val) {
		val = t4_read_reg(padap, A_LE_DB_DBGI_CONFIG);
		val = (val >> S_DBGICMDBUSY) & 1;
		cmd_retry--;
		if (!cmd_retry) {
			if (pdbg_init->verbose)
				pdbg_init->print("%s(): Timeout waiting for non-busy\n",
					 __func__);
			return CUDBG_SYSTEM_ERROR;
		}
	}

	/* Check RESP status */
	val = 0;
	val = t4_read_reg(padap, A_LE_DB_DBGI_RSP_STATUS);
	tid_data->dbig_rsp_stat = val;
	if (!(val & 1)) {
		if (pdbg_init->verbose)
			pdbg_init->print("%s(): DBGI command failed\n", __func__);
		return CUDBG_SYSTEM_ERROR;
	}

	/* Read RESP data */
	for (i = 0; i < CUDBG_NUM_REQ_REGS; i++)
		tid_data->data[i] = t4_read_reg(padap,
						A_LE_DB_DBGI_RSP_DATA +
						(i << 2));

	tid_data->tid = tid;

	return 0;
}

static int collect_le_tcam(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_tcam tcam_region = {0};
	struct cudbg_tid_data *tid_data = NULL;
	u32 value, bytes = 0, bytes_left  = 0;
	u32 i;
	int rc, size;

	/* Get the LE regions */
	value = t4_read_reg(padap, A_LE_DB_TID_HASHBASE); /* Get hash base
							     index */
	tcam_region.tid_hash_base = value;

	/* Get routing table index */
	value = t4_read_reg(padap, A_LE_DB_ROUTING_TABLE_INDEX);
	tcam_region.routing_start = value;

	/*Get clip table index */
	value = t4_read_reg(padap, A_LE_DB_CLIP_TABLE_INDEX);
	tcam_region.clip_start = value;

	/* Get filter table index */
	value = t4_read_reg(padap, A_LE_DB_FILTER_TABLE_INDEX);
	tcam_region.filter_start = value;

	/* Get server table index */
	value = t4_read_reg(padap, A_LE_DB_SERVER_INDEX);
	tcam_region.server_start = value;

	/* Check whether hash is enabled and calculate the max tids */
	value = t4_read_reg(padap, A_LE_DB_CONFIG);
	if ((value >> S_HASHEN) & 1) {
		value = t4_read_reg(padap, A_LE_DB_HASH_CONFIG);
		if (chip_id(padap) > CHELSIO_T5)
			tcam_region.max_tid = (value & 0xFFFFF) +
					      tcam_region.tid_hash_base;
		else {	    /* for T5 */
			value = G_HASHTIDSIZE(value);
			value = 1 << value;
			tcam_region.max_tid = value +
				tcam_region.tid_hash_base;
		}
	} else	 /* hash not enabled */
		tcam_region.max_tid = CUDBG_MAX_TCAM_TID;

	size = sizeof(struct cudbg_tid_data) * tcam_region.max_tid;
	size += sizeof(struct cudbg_tcam);
	scratch_buff.size = size;

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err;

	rc = get_scratch_buff(dbg_buff, CUDBG_CHUNK_SIZE, &scratch_buff);
	if (rc)
		goto err;

	memcpy(scratch_buff.data, &tcam_region, sizeof(struct cudbg_tcam));

	tid_data = (struct cudbg_tid_data *)(((struct cudbg_tcam *)
					     scratch_buff.data) + 1);
	bytes_left = CUDBG_CHUNK_SIZE - sizeof(struct cudbg_tcam);
	bytes = sizeof(struct cudbg_tcam);

	/* read all tid */
	for (i = 0; i < tcam_region.max_tid; i++) {
		if (bytes_left < sizeof(struct cudbg_tid_data)) {
			scratch_buff.size = bytes;
			rc = compress_buff(&scratch_buff, dbg_buff);
			if (rc)
				goto err1;
			scratch_buff.size = CUDBG_CHUNK_SIZE;
			release_scratch_buff(&scratch_buff, dbg_buff);

			/* new alloc */
			rc = get_scratch_buff(dbg_buff, CUDBG_CHUNK_SIZE,
					      &scratch_buff);
			if (rc)
				goto err;

			tid_data = (struct cudbg_tid_data *)(scratch_buff.data);
			bytes_left = CUDBG_CHUNK_SIZE;
			bytes = 0;
		}

		rc = cudbg_read_tid(pdbg_init, i, tid_data);

		if (rc) {
			cudbg_err->sys_err = rc;
			goto err1;
		}

		tid_data++;
		bytes_left -= sizeof(struct cudbg_tid_data);
		bytes += sizeof(struct cudbg_tid_data);
	}

	if (bytes) {
		scratch_buff.size = bytes;
		rc = compress_buff(&scratch_buff, dbg_buff);
	}

err1:
	scratch_buff.size = CUDBG_CHUNK_SIZE;
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_ma_indirect(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	struct ireg_buf *ma_indr = NULL;
	u32 size, j;
	int i, rc, n;

	if (chip_id(padap) < CHELSIO_T6) {
		if (pdbg_init->verbose)
			pdbg_init->print("MA indirect available only in T6\n");
		rc = CUDBG_STATUS_ENTITY_NOT_FOUND;
		goto err;
	}

	n = sizeof(t6_ma_ireg_array) / (4 * sizeof(u32));
	size = sizeof(struct ireg_buf) * n * 2;
	scratch_buff.size = size;

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	ma_indr = (struct ireg_buf *)scratch_buff.data;

	for (i = 0; i < n; i++) {
		struct ireg_field *ma_fli = &ma_indr->tp_pio;
		u32 *buff = ma_indr->outbuf;

		ma_fli->ireg_addr = t6_ma_ireg_array[i][0];
		ma_fli->ireg_data = t6_ma_ireg_array[i][1];
		ma_fli->ireg_local_offset = t6_ma_ireg_array[i][2];
		ma_fli->ireg_offset_range = t6_ma_ireg_array[i][3];

		t4_read_indirect(padap, ma_fli->ireg_addr, ma_fli->ireg_data,
				 buff, ma_fli->ireg_offset_range,
				 ma_fli->ireg_local_offset);

		ma_indr++;

	}

	n = sizeof(t6_ma_ireg_array2) / (4 * sizeof(u32));

	for (i = 0; i < n; i++) {
		struct ireg_field *ma_fli = &ma_indr->tp_pio;
		u32 *buff = ma_indr->outbuf;

		ma_fli->ireg_addr = t6_ma_ireg_array2[i][0];
		ma_fli->ireg_data = t6_ma_ireg_array2[i][1];
		ma_fli->ireg_local_offset = t6_ma_ireg_array2[i][2];

		for (j = 0; j < t6_ma_ireg_array2[i][3]; j++) {
			t4_read_indirect(padap, ma_fli->ireg_addr,
					 ma_fli->ireg_data, buff, 1,
					 ma_fli->ireg_local_offset);
			buff++;
			ma_fli->ireg_local_offset += 0x20;
		}
		ma_indr++;
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_hma_indirect(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	struct ireg_buf *hma_indr = NULL;
	u32 size;
	int i, rc, n;

	if (chip_id(padap) < CHELSIO_T6) {
		if (pdbg_init->verbose)
			pdbg_init->print("HMA indirect available only in T6\n");
		rc = CUDBG_STATUS_ENTITY_NOT_FOUND;
		goto err;
	}

	n = sizeof(t6_hma_ireg_array) / (4 * sizeof(u32));
	size = sizeof(struct ireg_buf) * n;
	scratch_buff.size = size;

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	hma_indr = (struct ireg_buf *)scratch_buff.data;

	for (i = 0; i < n; i++) {
		struct ireg_field *hma_fli = &hma_indr->tp_pio;
		u32 *buff = hma_indr->outbuf;

		hma_fli->ireg_addr = t6_hma_ireg_array[i][0];
		hma_fli->ireg_data = t6_hma_ireg_array[i][1];
		hma_fli->ireg_local_offset = t6_hma_ireg_array[i][2];
		hma_fli->ireg_offset_range = t6_hma_ireg_array[i][3];

		t4_read_indirect(padap, hma_fli->ireg_addr, hma_fli->ireg_data,
				 buff, hma_fli->ireg_offset_range,
				 hma_fli->ireg_local_offset);

		hma_indr++;

	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_pcie_indirect(struct cudbg_init *pdbg_init,
				 struct cudbg_buffer *dbg_buff,
				 struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	struct ireg_buf *ch_pcie;
	u32 size;
	int i, rc, n;

	n = sizeof(t5_pcie_pdbg_array) / (4 * sizeof(u32));
	size = sizeof(struct ireg_buf) * n * 2;
	scratch_buff.size = size;

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	ch_pcie = (struct ireg_buf *)scratch_buff.data;

	/*PCIE_PDBG*/
	for (i = 0; i < n; i++) {
		struct ireg_field *pcie_pio = &ch_pcie->tp_pio;
		u32 *buff = ch_pcie->outbuf;

		pcie_pio->ireg_addr = t5_pcie_pdbg_array[i][0];
		pcie_pio->ireg_data = t5_pcie_pdbg_array[i][1];
		pcie_pio->ireg_local_offset = t5_pcie_pdbg_array[i][2];
		pcie_pio->ireg_offset_range = t5_pcie_pdbg_array[i][3];

		t4_read_indirect(padap,
				pcie_pio->ireg_addr,
				pcie_pio->ireg_data,
				buff,
				pcie_pio->ireg_offset_range,
				pcie_pio->ireg_local_offset);

		ch_pcie++;
	}

	/*PCIE_CDBG*/
	n = sizeof(t5_pcie_cdbg_array) / (4 * sizeof(u32));
	for (i = 0; i < n; i++) {
		struct ireg_field *pcie_pio = &ch_pcie->tp_pio;
		u32 *buff = ch_pcie->outbuf;

		pcie_pio->ireg_addr = t5_pcie_cdbg_array[i][0];
		pcie_pio->ireg_data = t5_pcie_cdbg_array[i][1];
		pcie_pio->ireg_local_offset = t5_pcie_cdbg_array[i][2];
		pcie_pio->ireg_offset_range = t5_pcie_cdbg_array[i][3];

		t4_read_indirect(padap,
				pcie_pio->ireg_addr,
				pcie_pio->ireg_data,
				buff,
				pcie_pio->ireg_offset_range,
				pcie_pio->ireg_local_offset);

		ch_pcie++;
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;

}

static int collect_tp_indirect(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	struct ireg_buf *ch_tp_pio;
	u32 size;
	int i, rc, n = 0;

	if (is_t5(padap))
		n = sizeof(t5_tp_pio_array) / (4 * sizeof(u32));
	else if (is_t6(padap))
		n = sizeof(t6_tp_pio_array) / (4 * sizeof(u32));

	size = sizeof(struct ireg_buf) * n * 3;
	scratch_buff.size = size;

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	ch_tp_pio = (struct ireg_buf *)scratch_buff.data;

	/* TP_PIO*/
	for (i = 0; i < n; i++) {
		struct ireg_field *tp_pio = &ch_tp_pio->tp_pio;
		u32 *buff = ch_tp_pio->outbuf;

		if (is_t5(padap)) {
			tp_pio->ireg_addr = t5_tp_pio_array[i][0];
			tp_pio->ireg_data = t5_tp_pio_array[i][1];
			tp_pio->ireg_local_offset = t5_tp_pio_array[i][2];
			tp_pio->ireg_offset_range = t5_tp_pio_array[i][3];
		} else if (is_t6(padap)) {
			tp_pio->ireg_addr = t6_tp_pio_array[i][0];
			tp_pio->ireg_data = t6_tp_pio_array[i][1];
			tp_pio->ireg_local_offset = t6_tp_pio_array[i][2];
			tp_pio->ireg_offset_range = t6_tp_pio_array[i][3];
		}

		t4_tp_pio_read(padap, buff, tp_pio->ireg_offset_range,
			       tp_pio->ireg_local_offset, true);

		ch_tp_pio++;
	}

	/* TP_TM_PIO*/
	if (is_t5(padap))
		n = sizeof(t5_tp_tm_pio_array) / (4 * sizeof(u32));
	else if (is_t6(padap))
		n = sizeof(t6_tp_tm_pio_array) / (4 * sizeof(u32));

	for (i = 0; i < n; i++) {
		struct ireg_field *tp_pio = &ch_tp_pio->tp_pio;
		u32 *buff = ch_tp_pio->outbuf;

		if (is_t5(padap)) {
			tp_pio->ireg_addr = t5_tp_tm_pio_array[i][0];
			tp_pio->ireg_data = t5_tp_tm_pio_array[i][1];
			tp_pio->ireg_local_offset = t5_tp_tm_pio_array[i][2];
			tp_pio->ireg_offset_range = t5_tp_tm_pio_array[i][3];
		} else if (is_t6(padap)) {
			tp_pio->ireg_addr = t6_tp_tm_pio_array[i][0];
			tp_pio->ireg_data = t6_tp_tm_pio_array[i][1];
			tp_pio->ireg_local_offset = t6_tp_tm_pio_array[i][2];
			tp_pio->ireg_offset_range = t6_tp_tm_pio_array[i][3];
		}

		t4_tp_tm_pio_read(padap, buff, tp_pio->ireg_offset_range,
				  tp_pio->ireg_local_offset, true);

		ch_tp_pio++;
	}

	/* TP_MIB_INDEX*/
	if (is_t5(padap))
		n = sizeof(t5_tp_mib_index_array) / (4 * sizeof(u32));
	else if (is_t6(padap))
		n = sizeof(t6_tp_mib_index_array) / (4 * sizeof(u32));

	for (i = 0; i < n ; i++) {
		struct ireg_field *tp_pio = &ch_tp_pio->tp_pio;
		u32 *buff = ch_tp_pio->outbuf;

		if (is_t5(padap)) {
			tp_pio->ireg_addr = t5_tp_mib_index_array[i][0];
			tp_pio->ireg_data = t5_tp_mib_index_array[i][1];
			tp_pio->ireg_local_offset =
				t5_tp_mib_index_array[i][2];
			tp_pio->ireg_offset_range =
				t5_tp_mib_index_array[i][3];
		} else if (is_t6(padap)) {
			tp_pio->ireg_addr = t6_tp_mib_index_array[i][0];
			tp_pio->ireg_data = t6_tp_mib_index_array[i][1];
			tp_pio->ireg_local_offset =
				t6_tp_mib_index_array[i][2];
			tp_pio->ireg_offset_range =
				t6_tp_mib_index_array[i][3];
		}

		t4_tp_mib_read(padap, buff, tp_pio->ireg_offset_range,
			       tp_pio->ireg_local_offset, true);

		ch_tp_pio++;
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_sge_indirect(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	struct ireg_buf *ch_sge_dbg;
	u32 size;
	int i, rc;

	size = sizeof(struct ireg_buf) * 2;
	scratch_buff.size = size;

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	ch_sge_dbg = (struct ireg_buf *)scratch_buff.data;

	for (i = 0; i < 2; i++) {
		struct ireg_field *sge_pio = &ch_sge_dbg->tp_pio;
		u32 *buff = ch_sge_dbg->outbuf;

		sge_pio->ireg_addr = t5_sge_dbg_index_array[i][0];
		sge_pio->ireg_data = t5_sge_dbg_index_array[i][1];
		sge_pio->ireg_local_offset = t5_sge_dbg_index_array[i][2];
		sge_pio->ireg_offset_range = t5_sge_dbg_index_array[i][3];

		t4_read_indirect(padap,
				sge_pio->ireg_addr,
				sge_pio->ireg_data,
				buff,
				sge_pio->ireg_offset_range,
				sge_pio->ireg_local_offset);

		ch_sge_dbg++;
	}

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_full(struct cudbg_init *pdbg_init,
			struct cudbg_buffer *dbg_buff,
			struct cudbg_error *cudbg_err)
{
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	u32 reg_addr, reg_data, reg_local_offset, reg_offset_range;
	u32 *sp;
	int rc;
	int nreg = 0;

	/* Collect Registers:
	 * TP_DBG_SCHED_TX (0x7e40 + 0x6a),
	 * TP_DBG_SCHED_RX (0x7e40 + 0x6b),
	 * TP_DBG_CSIDE_INT (0x7e40 + 0x23f),
	 * TP_DBG_ESIDE_INT (0x7e40 + 0x148),
	 * PCIE_CDEBUG_INDEX[AppData0] (0x5a10 + 2),
	 * PCIE_CDEBUG_INDEX[AppData1] (0x5a10 + 3)  This is for T6
	 * SGE_DEBUG_DATA_HIGH_INDEX_10 (0x12a8)
	 **/

	if (is_t5(padap))
		nreg = 6;
	else if (is_t6(padap))
		nreg = 7;

	scratch_buff.size = nreg * sizeof(u32);

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	sp = (u32 *)scratch_buff.data;

	/* TP_DBG_SCHED_TX */
	reg_local_offset = t5_tp_pio_array[3][2] + 0xa;
	reg_offset_range = 1;

	t4_tp_pio_read(padap, sp, reg_offset_range, reg_local_offset, true);

	sp++;

	/* TP_DBG_SCHED_RX */
	reg_local_offset = t5_tp_pio_array[3][2] + 0xb;
	reg_offset_range = 1;

	t4_tp_pio_read(padap, sp, reg_offset_range, reg_local_offset, true);

	sp++;

	/* TP_DBG_CSIDE_INT */
	reg_local_offset = t5_tp_pio_array[9][2] + 0xf;
	reg_offset_range = 1;

	t4_tp_pio_read(padap, sp, reg_offset_range, reg_local_offset, true);

	sp++;

	/* TP_DBG_ESIDE_INT */
	reg_local_offset = t5_tp_pio_array[8][2] + 3;
	reg_offset_range = 1;

	t4_tp_pio_read(padap, sp, reg_offset_range, reg_local_offset, true);

	sp++;

	/* PCIE_CDEBUG_INDEX[AppData0] */
	reg_addr = t5_pcie_cdbg_array[0][0];
	reg_data = t5_pcie_cdbg_array[0][1];
	reg_local_offset = t5_pcie_cdbg_array[0][2] + 2;
	reg_offset_range = 1;

	t4_read_indirect(padap, reg_addr, reg_data, sp, reg_offset_range,
			 reg_local_offset);

	sp++;

	if (is_t6(padap)) {
		/* PCIE_CDEBUG_INDEX[AppData1] */
		reg_addr = t5_pcie_cdbg_array[0][0];
		reg_data = t5_pcie_cdbg_array[0][1];
		reg_local_offset = t5_pcie_cdbg_array[0][2] + 3;
		reg_offset_range = 1;

		t4_read_indirect(padap, reg_addr, reg_data, sp,
				 reg_offset_range, reg_local_offset);

		sp++;
	}

	/* SGE_DEBUG_DATA_HIGH_INDEX_10 */
	*sp = t4_read_reg(padap, A_SGE_DEBUG_DATA_HIGH_INDEX_10);

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
}

static int collect_vpd_data(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
#ifdef notyet
	struct cudbg_buffer scratch_buff;
	struct adapter *padap = pdbg_init->adap;
	struct struct_vpd_data *vpd_data;
	char vpd_ver[4];
	u32 fw_vers;
	u32 size;
	int rc;

	size = sizeof(struct struct_vpd_data);
	scratch_buff.size = size;

	rc = get_scratch_buff(dbg_buff, scratch_buff.size, &scratch_buff);
	if (rc)
		goto err;

	vpd_data = (struct struct_vpd_data *)scratch_buff.data;

	if (is_t5(padap)) {
		read_vpd_reg(padap, SN_REG_ADDR, SN_MAX_LEN, vpd_data->sn);
		read_vpd_reg(padap, BN_REG_ADDR, BN_MAX_LEN, vpd_data->bn);
		read_vpd_reg(padap, NA_REG_ADDR, NA_MAX_LEN, vpd_data->na);
		read_vpd_reg(padap, MN_REG_ADDR, MN_MAX_LEN, vpd_data->mn);
	} else if (is_t6(padap)) {
		read_vpd_reg(padap, SN_T6_ADDR, SN_MAX_LEN, vpd_data->sn);
		read_vpd_reg(padap, BN_T6_ADDR, BN_MAX_LEN, vpd_data->bn);
		read_vpd_reg(padap, NA_T6_ADDR, NA_MAX_LEN, vpd_data->na);
		read_vpd_reg(padap, MN_T6_ADDR, MN_MAX_LEN, vpd_data->mn);
	}

	if (is_fw_attached(pdbg_init)) {
	   rc = t4_get_scfg_version(padap, &vpd_data->scfg_vers);
	} else {
		rc = 1;
	}

	if (rc) {
		/* Now trying with backdoor mechanism */
		rc = read_vpd_reg(padap, SCFG_VER_ADDR, SCFG_VER_LEN,
				  (u8 *)&vpd_data->scfg_vers);
		if (rc)
			goto err1;
	}

	if (is_fw_attached(pdbg_init)) {
		rc = t4_get_vpd_version(padap, &vpd_data->vpd_vers);
	} else {
		rc = 1;
	}

	if (rc) {
		/* Now trying with backdoor mechanism */
		rc = read_vpd_reg(padap, VPD_VER_ADDR, VPD_VER_LEN,
				  (u8 *)vpd_ver);
		if (rc)
			goto err1;
		/* read_vpd_reg return string of stored hex
		 * converting hex string to char string
		 * vpd version is 2 bytes only */
		sprintf(vpd_ver, "%c%c\n", vpd_ver[0], vpd_ver[1]);
		vpd_data->vpd_vers = simple_strtoul(vpd_ver, NULL, 16);
	}

	/* Get FW version if it's not already filled in */
	fw_vers = padap->params.fw_vers;
	if (!fw_vers) {
		rc = t4_get_fw_version(padap, &fw_vers);
		if (rc)
			goto err1;
	}

	vpd_data->fw_major = G_FW_HDR_FW_VER_MAJOR(fw_vers);
	vpd_data->fw_minor = G_FW_HDR_FW_VER_MINOR(fw_vers);
	vpd_data->fw_micro = G_FW_HDR_FW_VER_MICRO(fw_vers);
	vpd_data->fw_build = G_FW_HDR_FW_VER_BUILD(fw_vers);

	rc = write_compression_hdr(&scratch_buff, dbg_buff);
	if (rc)
		goto err1;

	rc = compress_buff(&scratch_buff, dbg_buff);

err1:
	release_scratch_buff(&scratch_buff, dbg_buff);
err:
	return rc;
#endif
	return (EDOOFUS);
}
