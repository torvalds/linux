/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

#if defined(_KERNEL) && defined(HAVE_QAT)
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/completion.h>
#include <sys/zfs_context.h>
#include "qat_compress.h"

/*
 * Timeout - no response from hardware after 0.5 seconds
 */
#define	TIMEOUT_MS		500

/*
 * Max instances in QAT device, each instance is a channel to submit
 * jobs to QAT hardware, this is only for pre-allocating instance,
 * and session arrays, the actual number of instances are defined in
 * the QAT driver's configure file.
 */
#define	MAX_INSTANCES		48

/*
 * ZLIB head and foot size
 */
#define	ZLIB_HEAD_SZ		2
#define	ZLIB_FOOT_SZ		4

/*
 * The minimal and maximal buffer size, which are not restricted
 * in the QAT hardware, but with the input buffer size between 4KB
 * and 128KB, the hardware can provide the optimal performance.
 */
#define	QAT_MIN_BUF_SIZE	(4*1024)
#define	QAT_MAX_BUF_SIZE	(128*1024)

/*
 * Used for qat kstat.
 */
typedef struct qat_stats {
	/*
	 * Number of jobs submitted to qat compression engine.
	 */
	kstat_named_t comp_requests;
	/*
	 * Total bytes sent to qat compression engine.
	 */
	kstat_named_t comp_total_in_bytes;
	/*
	 * Total bytes output from qat compression engine.
	 */
	kstat_named_t comp_total_out_bytes;
	/*
	 * Number of jobs submitted to qat de-compression engine.
	 */
	kstat_named_t decomp_requests;
	/*
	 * Total bytes sent to qat de-compression engine.
	 */
	kstat_named_t decomp_total_in_bytes;
	/*
	 * Total bytes output from qat de-compression engine.
	 */
	kstat_named_t decomp_total_out_bytes;
	/*
	 * Number of fails in qat engine.
	 * Note: when qat fail happens, it doesn't mean a critical hardware
	 * issue, sometimes it is because the output buffer is not big enough,
	 * and the compression job will be transfered to gzip software again,
	 * so the functionality of ZFS is not impacted.
	 */
	kstat_named_t dc_fails;
} qat_stats_t;

qat_stats_t qat_stats = {
	{ "comp_reqests",			KSTAT_DATA_UINT64 },
	{ "comp_total_in_bytes",		KSTAT_DATA_UINT64 },
	{ "comp_total_out_bytes",		KSTAT_DATA_UINT64 },
	{ "decomp_reqests",			KSTAT_DATA_UINT64 },
	{ "decomp_total_in_bytes",		KSTAT_DATA_UINT64 },
	{ "decomp_total_out_bytes",		KSTAT_DATA_UINT64 },
	{ "dc_fails",				KSTAT_DATA_UINT64 },
};

static kstat_t *qat_ksp;
static CpaInstanceHandle dc_inst_handles[MAX_INSTANCES];
static CpaDcSessionHandle session_handles[MAX_INSTANCES];
static CpaBufferList **buffer_array[MAX_INSTANCES];
static Cpa16U num_inst = 0;
static Cpa32U inst_num = 0;
static boolean_t qat_init_done = B_FALSE;
int zfs_qat_disable = 0;

#define	QAT_STAT_INCR(stat, val) \
	atomic_add_64(&qat_stats.stat.value.ui64, (val));
#define	QAT_STAT_BUMP(stat) \
	QAT_STAT_INCR(stat, 1);

#define	PHYS_CONTIG_ALLOC(pp_mem_addr, size_bytes)	\
	mem_alloc_contig((void *)(pp_mem_addr), (size_bytes))

#define	PHYS_CONTIG_FREE(p_mem_addr)	\
	mem_free_contig((void *)&(p_mem_addr))

static inline struct page *
mem_to_page(void *addr)
{
	if (!is_vmalloc_addr(addr))
		return (virt_to_page(addr));

	return (vmalloc_to_page(addr));
}

static void
qat_dc_callback(void *p_callback, CpaStatus status)
{
	if (p_callback != NULL)
		complete((struct completion *)p_callback);
}

static inline CpaStatus
mem_alloc_contig(void **pp_mem_addr, Cpa32U size_bytes)
{
	*pp_mem_addr = kmalloc(size_bytes, GFP_KERNEL);
	if (*pp_mem_addr == NULL)
		return (CPA_STATUS_RESOURCE);
	return (CPA_STATUS_SUCCESS);
}

static inline void
mem_free_contig(void **pp_mem_addr)
{
	if (*pp_mem_addr != NULL) {
		kfree(*pp_mem_addr);
		*pp_mem_addr = NULL;
	}
}

static void
qat_clean(void)
{
	Cpa16U buff_num = 0;
	Cpa16U num_inter_buff_lists = 0;
	Cpa16U i = 0;

	for (i = 0; i < num_inst; i++) {
		cpaDcStopInstance(dc_inst_handles[i]);
		PHYS_CONTIG_FREE(session_handles[i]);
		/* free intermediate buffers  */
		if (buffer_array[i] != NULL) {
			cpaDcGetNumIntermediateBuffers(
			    dc_inst_handles[i], &num_inter_buff_lists);
			for (buff_num = 0; buff_num < num_inter_buff_lists;
			    buff_num++) {
				CpaBufferList *buffer_inter =
				    buffer_array[i][buff_num];
				if (buffer_inter->pBuffers) {
					PHYS_CONTIG_FREE(
					    buffer_inter->pBuffers->pData);
					PHYS_CONTIG_FREE(
					    buffer_inter->pBuffers);
				}
				PHYS_CONTIG_FREE(
				    buffer_inter->pPrivateMetaData);
				PHYS_CONTIG_FREE(buffer_inter);
			}
		}
	}

	num_inst = 0;
	qat_init_done = B_FALSE;
}

int
qat_init(void)
{
	CpaStatus status = CPA_STATUS_SUCCESS;
	Cpa32U sess_size = 0;
	Cpa32U ctx_size = 0;
	Cpa16U num_inter_buff_lists = 0;
	Cpa16U buff_num = 0;
	Cpa32U buff_meta_size = 0;
	CpaDcSessionSetupData sd = {0};
	Cpa16U i;

	status = cpaDcGetNumInstances(&num_inst);
	if (status != CPA_STATUS_SUCCESS || num_inst == 0)
		return (-1);

	if (num_inst > MAX_INSTANCES)
		num_inst = MAX_INSTANCES;

	status = cpaDcGetInstances(num_inst, &dc_inst_handles[0]);
	if (status != CPA_STATUS_SUCCESS)
		return (-1);

	for (i = 0; i < num_inst; i++) {
		cpaDcSetAddressTranslation(dc_inst_handles[i],
		    (void*)virt_to_phys);

		status = cpaDcBufferListGetMetaSize(dc_inst_handles[i],
		    1, &buff_meta_size);

		if (status == CPA_STATUS_SUCCESS)
			status = cpaDcGetNumIntermediateBuffers(
			    dc_inst_handles[i], &num_inter_buff_lists);

		if (status == CPA_STATUS_SUCCESS && num_inter_buff_lists != 0)
			status = PHYS_CONTIG_ALLOC(&buffer_array[i],
			    num_inter_buff_lists *
			    sizeof (CpaBufferList *));

		for (buff_num = 0; buff_num < num_inter_buff_lists;
		    buff_num++) {
			if (status == CPA_STATUS_SUCCESS)
				status = PHYS_CONTIG_ALLOC(
				    &buffer_array[i][buff_num],
				    sizeof (CpaBufferList));

			if (status == CPA_STATUS_SUCCESS)
				status = PHYS_CONTIG_ALLOC(
				    &buffer_array[i][buff_num]->
				    pPrivateMetaData,
				    buff_meta_size);

			if (status == CPA_STATUS_SUCCESS)
				status = PHYS_CONTIG_ALLOC(
				    &buffer_array[i][buff_num]->pBuffers,
				    sizeof (CpaFlatBuffer));

			if (status == CPA_STATUS_SUCCESS) {
				/*
				 *  implementation requires an intermediate
				 *  buffer approximately twice the size of
				 *  output buffer, which is 2x max buffer
				 *  size here.
				 */
				status = PHYS_CONTIG_ALLOC(
				    &buffer_array[i][buff_num]->pBuffers->
				    pData, 2 * QAT_MAX_BUF_SIZE);
				if (status != CPA_STATUS_SUCCESS)
					goto fail;

				buffer_array[i][buff_num]->numBuffers = 1;
				buffer_array[i][buff_num]->pBuffers->
				    dataLenInBytes = 2 * QAT_MAX_BUF_SIZE;
			}
		}

		status = cpaDcStartInstance(dc_inst_handles[i],
		    num_inter_buff_lists, buffer_array[i]);
		if (status != CPA_STATUS_SUCCESS)
			goto fail;

		sd.compLevel = CPA_DC_L1;
		sd.compType = CPA_DC_DEFLATE;
		sd.huffType = CPA_DC_HT_FULL_DYNAMIC;
		sd.sessDirection = CPA_DC_DIR_COMBINED;
		sd.sessState = CPA_DC_STATELESS;
		sd.deflateWindowSize = 7;
		sd.checksum = CPA_DC_ADLER32;
		status = cpaDcGetSessionSize(dc_inst_handles[i],
		    &sd, &sess_size, &ctx_size);
		if (status != CPA_STATUS_SUCCESS)
			goto fail;

		PHYS_CONTIG_ALLOC(&session_handles[i], sess_size);
		if (session_handles[i] == NULL)
			goto fail;

		status = cpaDcInitSession(dc_inst_handles[i],
		    session_handles[i],
		    &sd, NULL, qat_dc_callback);
		if (status != CPA_STATUS_SUCCESS)
			goto fail;
	}

	qat_ksp = kstat_create("zfs", 0, "qat", "misc",
	    KSTAT_TYPE_NAMED, sizeof (qat_stats) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);
	if (qat_ksp != NULL) {
		qat_ksp->ks_data = &qat_stats;
		kstat_install(qat_ksp);
	}

	qat_init_done = B_TRUE;
	return (0);
fail:
	qat_clean();
	return (-1);
}

void
qat_fini(void)
{
	qat_clean();

	if (qat_ksp != NULL) {
		kstat_delete(qat_ksp);
		qat_ksp = NULL;
	}
}

boolean_t
qat_use_accel(size_t s_len)
{
	return (!zfs_qat_disable &&
	    qat_init_done &&
	    s_len >= QAT_MIN_BUF_SIZE &&
	    s_len <= QAT_MAX_BUF_SIZE);
}

int
qat_compress(qat_compress_dir_t dir, char *src, int src_len,
    char *dst, int dst_len, size_t *c_len)
{
	CpaInstanceHandle dc_inst_handle;
	CpaDcSessionHandle session_handle;
	CpaBufferList *buf_list_src = NULL;
	CpaBufferList *buf_list_dst = NULL;
	CpaFlatBuffer *flat_buf_src = NULL;
	CpaFlatBuffer *flat_buf_dst = NULL;
	Cpa8U *buffer_meta_src = NULL;
	Cpa8U *buffer_meta_dst = NULL;
	Cpa32U buffer_meta_size = 0;
	CpaDcRqResults dc_results;
	CpaStatus status = CPA_STATUS_SUCCESS;
	Cpa32U hdr_sz = 0;
	Cpa32U compressed_sz;
	Cpa32U num_src_buf = (src_len >> PAGE_SHIFT) + 1;
	Cpa32U num_dst_buf = (dst_len >> PAGE_SHIFT) + 1;
	Cpa32U bytes_left;
	char *data;
	struct page *in_page, *out_page;
	struct page **in_pages = NULL;
	struct page **out_pages = NULL;
	struct completion complete;
	size_t ret = -1;
	Cpa16U page_num = 0;
	Cpa16U i;

	Cpa32U src_buffer_list_mem_size = sizeof (CpaBufferList) +
	    (num_src_buf * sizeof (CpaFlatBuffer));
	Cpa32U dst_buffer_list_mem_size = sizeof (CpaBufferList) +
	    (num_dst_buf * sizeof (CpaFlatBuffer));

	if (PHYS_CONTIG_ALLOC(&in_pages,
	    num_src_buf * sizeof (struct page *)) != CPA_STATUS_SUCCESS)
		goto fail;

	if (PHYS_CONTIG_ALLOC(&out_pages,
	    num_dst_buf * sizeof (struct page *)) != CPA_STATUS_SUCCESS)
		goto fail;

	i = atomic_inc_32_nv(&inst_num) % num_inst;
	dc_inst_handle = dc_inst_handles[i];
	session_handle = session_handles[i];

	cpaDcBufferListGetMetaSize(dc_inst_handle, num_src_buf,
	    &buffer_meta_size);
	if (PHYS_CONTIG_ALLOC(&buffer_meta_src, buffer_meta_size) !=
	    CPA_STATUS_SUCCESS)
		goto fail;

	cpaDcBufferListGetMetaSize(dc_inst_handle, num_dst_buf,
	    &buffer_meta_size);
	if (PHYS_CONTIG_ALLOC(&buffer_meta_dst, buffer_meta_size) !=
	    CPA_STATUS_SUCCESS)
		goto fail;

	/* build source buffer list */
	if (PHYS_CONTIG_ALLOC(&buf_list_src, src_buffer_list_mem_size) !=
	    CPA_STATUS_SUCCESS)
		goto fail;

	flat_buf_src = (CpaFlatBuffer *)(buf_list_src + 1);

	buf_list_src->pBuffers = flat_buf_src; /* always point to first one */

	/* build destination buffer list */
	if (PHYS_CONTIG_ALLOC(&buf_list_dst, dst_buffer_list_mem_size) !=
	    CPA_STATUS_SUCCESS)
		goto fail;

	flat_buf_dst = (CpaFlatBuffer *)(buf_list_dst + 1);

	buf_list_dst->pBuffers = flat_buf_dst; /* always point to first one */

	buf_list_src->numBuffers = 0;
	buf_list_src->pPrivateMetaData = buffer_meta_src;
	bytes_left = src_len;
	data = src;
	page_num = 0;
	while (bytes_left > 0) {
		in_page = mem_to_page(data);
		in_pages[page_num] = in_page;
		flat_buf_src->pData = kmap(in_page);
		flat_buf_src->dataLenInBytes =
		    min((long)bytes_left, (long)PAGE_SIZE);

		bytes_left -= flat_buf_src->dataLenInBytes;
		data += flat_buf_src->dataLenInBytes;
		flat_buf_src++;
		buf_list_src->numBuffers++;
		page_num++;
	}

	buf_list_dst->numBuffers = 0;
	buf_list_dst->pPrivateMetaData = buffer_meta_dst;
	bytes_left = dst_len;
	data = dst;
	page_num = 0;
	while (bytes_left > 0) {
		out_page = mem_to_page(data);
		flat_buf_dst->pData = kmap(out_page);
		out_pages[page_num] = out_page;
		flat_buf_dst->dataLenInBytes =
		    min((long)bytes_left, (long)PAGE_SIZE);

		bytes_left -= flat_buf_dst->dataLenInBytes;
		data += flat_buf_dst->dataLenInBytes;
		flat_buf_dst++;
		buf_list_dst->numBuffers++;
		page_num++;
	}

	init_completion(&complete);

	if (dir == QAT_COMPRESS) {
		QAT_STAT_BUMP(comp_requests);
		QAT_STAT_INCR(comp_total_in_bytes, src_len);

		cpaDcGenerateHeader(session_handle,
		    buf_list_dst->pBuffers, &hdr_sz);
		buf_list_dst->pBuffers->pData += hdr_sz;
		buf_list_dst->pBuffers->dataLenInBytes -= hdr_sz;
		status = cpaDcCompressData(
		    dc_inst_handle, session_handle,
		    buf_list_src, buf_list_dst,
		    &dc_results, CPA_DC_FLUSH_FINAL,
		    &complete);
		if (status != CPA_STATUS_SUCCESS) {
			goto fail;
		}

		/* we now wait until the completion of the operation. */
		if (!wait_for_completion_interruptible_timeout(&complete,
		    TIMEOUT_MS)) {
			status = CPA_STATUS_FAIL;
			goto fail;
		}

		if (dc_results.status != CPA_STATUS_SUCCESS) {
			status = CPA_STATUS_FAIL;
			goto fail;
		}

		compressed_sz = dc_results.produced;
		if (compressed_sz + hdr_sz + ZLIB_FOOT_SZ > dst_len) {
			goto fail;
		}

		flat_buf_dst = (CpaFlatBuffer *)(buf_list_dst + 1);
		/* move to the last page */
		flat_buf_dst += (compressed_sz + hdr_sz) >> PAGE_SHIFT;

		/* no space for gzip foot in the last page */
		if (((compressed_sz + hdr_sz) % PAGE_SIZE)
		    + ZLIB_FOOT_SZ > PAGE_SIZE)
			goto fail;

		/* jump to the end of the buffer and append footer */
		flat_buf_dst->pData =
		    (char *)((unsigned long)flat_buf_dst->pData & PAGE_MASK)
		    + ((compressed_sz + hdr_sz) % PAGE_SIZE);
		flat_buf_dst->dataLenInBytes = ZLIB_FOOT_SZ;

		dc_results.produced = 0;
		status = cpaDcGenerateFooter(session_handle,
		    flat_buf_dst, &dc_results);
		if (status != CPA_STATUS_SUCCESS) {
			goto fail;
		}

		*c_len = compressed_sz + dc_results.produced + hdr_sz;

		QAT_STAT_INCR(comp_total_out_bytes, *c_len);

		ret = 0;

	} else if (dir == QAT_DECOMPRESS) {
		QAT_STAT_BUMP(decomp_requests);
		QAT_STAT_INCR(decomp_total_in_bytes, src_len);

		buf_list_src->pBuffers->pData += ZLIB_HEAD_SZ;
		buf_list_src->pBuffers->dataLenInBytes -= ZLIB_HEAD_SZ;
		status = cpaDcDecompressData(dc_inst_handle,
		    session_handle,
		    buf_list_src,
		    buf_list_dst,
		    &dc_results,
		    CPA_DC_FLUSH_FINAL,
		    &complete);

		if (CPA_STATUS_SUCCESS != status) {
			status = CPA_STATUS_FAIL;
			goto fail;
		}

		/* we now wait until the completion of the operation. */
		if (!wait_for_completion_interruptible_timeout(&complete,
		    TIMEOUT_MS)) {
			status = CPA_STATUS_FAIL;
			goto fail;
		}

		if (dc_results.status != CPA_STATUS_SUCCESS) {
			status = CPA_STATUS_FAIL;
			goto fail;
		}

		*c_len = dc_results.produced;

		QAT_STAT_INCR(decomp_total_out_bytes, *c_len);

		ret = 0;
	}

fail:
	if (status != CPA_STATUS_SUCCESS) {
		QAT_STAT_BUMP(dc_fails);
	}

	if (in_pages) {
		for (page_num = 0;
		    page_num < buf_list_src->numBuffers;
		    page_num++) {
			kunmap(in_pages[page_num]);
		}
		PHYS_CONTIG_FREE(in_pages);
	}

	if (out_pages) {
		for (page_num = 0;
		    page_num < buf_list_dst->numBuffers;
		    page_num++) {
			kunmap(out_pages[page_num]);
		}
		PHYS_CONTIG_FREE(out_pages);
	}

	PHYS_CONTIG_FREE(buffer_meta_src);
	PHYS_CONTIG_FREE(buffer_meta_dst);
	PHYS_CONTIG_FREE(buf_list_src);
	PHYS_CONTIG_FREE(buf_list_dst);

	return (ret);
}

module_param(zfs_qat_disable, int, 0644);
MODULE_PARM_DESC(zfs_qat_disable, "Disable QAT compression");

#endif
