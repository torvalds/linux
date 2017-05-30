/*
 *  sst.h - Intel SST Driver for audio engine
 *
 *  Copyright (C) 2008-14 Intel Corporation
 *  Authors:	Vinod Koul <vinod.koul@intel.com>
 *		Harsha Priya <priya.harsha@intel.com>
 *		Dharageswari R <dharageswari.r@intel.com>
 *		KP Jeeja <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  Common private declarations for SST
 */
#ifndef __SST_H__
#define __SST_H__

#include <linux/firmware.h>

/* driver names */
#define SST_DRV_NAME "intel_sst_driver"
#define SST_MRFLD_PCI_ID 0x119A
#define SST_BYT_ACPI_ID	0x80860F28
#define SST_CHV_ACPI_ID	0x808622A8

#define SST_SUSPEND_DELAY 2000
#define FW_CONTEXT_MEM (64*1024)
#define SST_ICCM_BOUNDARY 4
#define SST_CONFIG_SSP_SIGN 0x7ffe8001

#define MRFLD_FW_VIRTUAL_BASE 0xC0000000
#define MRFLD_FW_DDR_BASE_OFFSET 0x0
#define MRFLD_FW_FEATURE_BASE_OFFSET 0x4
#define MRFLD_FW_BSS_RESET_BIT 0

extern const struct dev_pm_ops intel_sst_pm;
enum sst_states {
	SST_FW_LOADING = 1,
	SST_FW_RUNNING,
	SST_RESET,
	SST_SHUTDOWN,
};

enum sst_algo_ops {
	SST_SET_ALGO = 0,
	SST_GET_ALGO = 1,
};

#define SST_BLOCK_TIMEOUT	1000

#define FW_SIGNATURE_SIZE	4
#define FW_NAME_SIZE		32

/* stream states */
enum sst_stream_states {
	STREAM_UN_INIT	= 0,	/* Freed/Not used stream */
	STREAM_RUNNING	= 1,	/* Running */
	STREAM_PAUSED	= 2,	/* Paused stream */
	STREAM_DECODE	= 3,	/* stream is in decoding only state */
	STREAM_INIT	= 4,	/* stream init, waiting for data */
	STREAM_RESET	= 5,	/* force reset on recovery */
};

enum sst_ram_type {
	SST_IRAM	= 1,
	SST_DRAM	= 2,
	SST_DDR	= 5,
	SST_CUSTOM_INFO	= 7,	/* consists of FW binary information */
};

/* SST shim registers to structure mapping */
union interrupt_reg {
	struct {
		u64 done_interrupt:1;
		u64 busy_interrupt:1;
		u64 rsvd:62;
	} part;
	u64 full;
};

union sst_pisr_reg {
	struct {
		u32 pssp0:1;
		u32 pssp1:1;
		u32 rsvd0:3;
		u32 dmac:1;
		u32 rsvd1:26;
	} part;
	u32 full;
};

union sst_pimr_reg {
	struct {
		u32 ssp0:1;
		u32 ssp1:1;
		u32 rsvd0:3;
		u32 dmac:1;
		u32 rsvd1:10;
		u32 ssp0_sc:1;
		u32 ssp1_sc:1;
		u32 rsvd2:3;
		u32 dmac_sc:1;
		u32 rsvd3:10;
	} part;
	u32 full;
};

union config_status_reg_mrfld {
	struct {
		u64 lpe_reset:1;
		u64 lpe_reset_vector:1;
		u64 runstall:1;
		u64 pwaitmode:1;
		u64 clk_sel:3;
		u64 rsvd2:1;
		u64 sst_clk:3;
		u64 xt_snoop:1;
		u64 rsvd3:4;
		u64 clk_sel1:6;
		u64 clk_enable:3;
		u64 rsvd4:6;
		u64 slim0baseclk:1;
		u64 rsvd:32;
	} part;
	u64 full;
};

union interrupt_reg_mrfld {
	struct {
		u64 done_interrupt:1;
		u64 busy_interrupt:1;
		u64 rsvd:62;
	} part;
	u64 full;
};

union sst_imr_reg_mrfld {
	struct {
		u64 done_interrupt:1;
		u64 busy_interrupt:1;
		u64 rsvd:62;
	} part;
	u64 full;
};

/**
 * struct sst_block - This structure is used to block a user/fw data call to another
 * fw/user call
 *
 * @condition: condition for blocking check
 * @ret_code: ret code when block is released
 * @data: data ptr
 * @size: size of data
 * @on: block condition
 * @msg_id: msg_id = msgid in mfld/ctp, mrfld = NULL
 * @drv_id: str_id in mfld/ctp, = drv_id in mrfld
 * @node: list head node
 */
struct sst_block {
	bool	condition;
	int	ret_code;
	void	*data;
	u32     size;
	bool	on;
	u32     msg_id;
	u32     drv_id;
	struct list_head node;
};

/**
 * struct stream_info - structure that holds the stream information
 *
 * @status : stream current state
 * @prev : stream prev state
 * @ops : stream operation pb/cp/drm...
 * @bufs: stream buffer list
 * @lock : stream mutex for protecting state
 * @pcm_substream : PCM substream
 * @period_elapsed : PCM period elapsed callback
 * @sfreq : stream sampling freq
 * @str_type : stream type
 * @cumm_bytes : cummulative bytes decoded
 * @str_type : stream type
 * @src : stream source
 */
struct stream_info {
	unsigned int		status;
	unsigned int		prev;
	unsigned int		ops;
	struct mutex		lock;

	void			*pcm_substream;
	void (*period_elapsed)(void *pcm_substream);

	unsigned int		sfreq;
	u32			cumm_bytes;

	void			*compr_cb_param;
	void (*compr_cb)(void *compr_cb_param);

	void			*drain_cb_param;
	void (*drain_notify)(void *drain_cb_param);

	unsigned int		num_ch;
	unsigned int		pipe_id;
	unsigned int		str_id;
	unsigned int		task_id;
};

#define SST_FW_SIGN "$SST"
#define SST_FW_LIB_SIGN "$LIB"

/**
 * struct sst_fw_header - FW file headers
 *
 * @signature : FW signature
 * @file_size: size of fw image
 * @modules : # of modules
 * @file_format : version of header format
 * @reserved : reserved fields
 */
struct sst_fw_header {
	unsigned char signature[FW_SIGNATURE_SIZE];
	u32 file_size;
	u32 modules;
	u32 file_format;
	u32 reserved[4];
};

/**
 * struct fw_module_header - module header in FW
 *
 * @signature: module signature
 * @mod_size: size of module
 * @blocks: block count
 * @type: block type
 * @entry_point: module netry point
 */
struct fw_module_header {
	unsigned char signature[FW_SIGNATURE_SIZE];
	u32 mod_size;
	u32 blocks;
	u32 type;
	u32 entry_point;
};

/**
 * struct fw_block_info - block header for FW
 *
 * @type: block ram type I/D
 * @size: size of block
 * @ram_offset: offset in ram
 */
struct fw_block_info {
	enum sst_ram_type	type;
	u32			size;
	u32			ram_offset;
	u32			rsvd;
};

struct sst_runtime_param {
	struct snd_sst_runtime_params param;
};

struct sst_sg_list {
	struct scatterlist *src;
	struct scatterlist *dst;
	int list_len;
	unsigned int sg_idx;
};

struct sst_memcpy_list {
	struct list_head memcpylist;
	void *dstn;
	const void *src;
	u32 size;
	bool is_io;
};

/*Firmware Module Information*/
enum sst_lib_dwnld_status {
	SST_LIB_NOT_FOUND = 0,
	SST_LIB_FOUND,
	SST_LIB_DOWNLOADED,
};

struct sst_module_info {
	const char *name; /*Library name*/
	u32	id; /*Module ID*/
	u32	entry_pt; /*Module entry point*/
	u8	status; /*module status*/
	u8	rsvd1;
	u16	rsvd2;
};

/*
 * Structure for managing the Library Region(1.5MB)
 * in DDR in Merrifield
 */
struct sst_mem_mgr {
	phys_addr_t current_base;
	int avail;
	unsigned int count;
};

struct sst_ipc_reg {
	int ipcx;
	int ipcd;
};

struct sst_fw_save {
	void *iram;
	void *dram;
	void *sram;
	void *ddr;
};

/**
 * struct intel_sst_drv - driver ops
 *
 * @sst_state : current sst device state
 * @dev_id : device identifier, pci_id for pci devices and acpi_id for acpi
 * 	     devices
 * @shim : SST shim pointer
 * @mailbox : SST mailbox pointer
 * @iram : SST IRAM pointer
 * @dram : SST DRAM pointer
 * @pdata : SST info passed as a part of pci platform data
 * @shim_phy_add : SST shim phy addr
 * @ipc_dispatch_list : ipc messages dispatched
 * @rx_list : to copy the process_reply/process_msg from DSP
 * @ipc_post_msg_wq : wq to post IPC messages context
 * @mad_ops : MAD driver operations registered
 * @mad_wq : MAD driver wq
 * @post_msg_wq : wq to post IPC messages
 * @streams : sst stream contexts
 * @list_lock : sst driver list lock (deprecated)
 * @ipc_spin_lock : spin lock to handle audio shim access and ipc queue
 * @block_lock : spin lock to add block to block_list and assign pvt_id
 * @rx_msg_lock : spin lock to handle the rx messages from the DSP
 * @scard_ops : sst card ops
 * @pci : sst pci device struture
 * @dev : pointer to current device struct
 * @sst_lock : sst device lock
 * @pvt_id : sst private id
 * @stream_cnt : total sst active stream count
 * @pb_streams : total active pb streams
 * @cp_streams : total active cp streams
 * @audio_start : audio status
 * @qos		: PM Qos struct
 * firmware_name : Firmware / Library name
 */
struct intel_sst_drv {
	int			sst_state;
	int			irq_num;
	unsigned int		dev_id;
	void __iomem		*ddr;
	void __iomem		*shim;
	void __iomem		*mailbox;
	void __iomem		*iram;
	void __iomem		*dram;
	unsigned int		mailbox_add;
	unsigned int		iram_base;
	unsigned int		dram_base;
	unsigned int		shim_phy_add;
	unsigned int		iram_end;
	unsigned int		dram_end;
	unsigned int		ddr_end;
	unsigned int		ddr_base;
	unsigned int		mailbox_recv_offset;
	struct list_head        block_list;
	struct list_head	ipc_dispatch_list;
	struct sst_platform_info *pdata;
	struct list_head	rx_list;
	struct work_struct      ipc_post_msg_wq;
	wait_queue_head_t	wait_queue;
	struct workqueue_struct *post_msg_wq;
	unsigned int		tstamp;
	/* str_id 0 is not used */
	struct stream_info	streams[MAX_NUM_STREAMS+1];
	spinlock_t		ipc_spin_lock;
	spinlock_t              block_lock;
	spinlock_t		rx_msg_lock;
	struct pci_dev		*pci;
	struct device		*dev;
	volatile long unsigned 		pvt_id;
	struct mutex            sst_lock;
	unsigned int		stream_cnt;
	unsigned int		csr_value;
	void			*fw_in_mem;
	struct sst_sg_list	fw_sg_list, library_list;
	struct intel_sst_ops	*ops;
	struct sst_info		info;
	struct pm_qos_request	*qos;
	unsigned int		use_dma;
	unsigned int		use_lli;
	atomic_t		fw_clear_context;
	bool			lib_dwnld_reqd;
	struct list_head	memcpy_list;
	struct sst_ipc_reg	ipc_reg;
	struct sst_mem_mgr      lib_mem_mgr;
	/*
	 * Holder for firmware name. Due to async call it needs to be
	 * persistent till worker thread gets called
	 */
	char firmware_name[FW_NAME_SIZE];

	struct snd_sst_fw_version fw_version;
	struct sst_fw_save	*fw_save;
};

/* misc definitions */
#define FW_DWNL_ID 0x01

struct intel_sst_ops {
	irqreturn_t (*interrupt)(int, void *);
	irqreturn_t (*irq_thread)(int, void *);
	void (*clear_interrupt)(struct intel_sst_drv *ctx);
	int (*start)(struct intel_sst_drv *ctx);
	int (*reset)(struct intel_sst_drv *ctx);
	void (*process_reply)(struct intel_sst_drv *ctx, struct ipc_post *msg);
	int (*post_message)(struct intel_sst_drv *ctx,
			struct ipc_post *msg, bool sync);
	void (*process_message)(struct ipc_post *msg);
	void (*set_bypass)(bool set);
	int (*save_dsp_context)(struct intel_sst_drv *sst);
	void (*restore_dsp_context)(void);
	int (*alloc_stream)(struct intel_sst_drv *ctx, void *params);
	void (*post_download)(struct intel_sst_drv *sst);
};

int sst_pause_stream(struct intel_sst_drv *sst_drv_ctx, int id);
int sst_resume_stream(struct intel_sst_drv *sst_drv_ctx, int id);
int sst_drop_stream(struct intel_sst_drv *sst_drv_ctx, int id);
int sst_free_stream(struct intel_sst_drv *sst_drv_ctx, int id);
int sst_start_stream(struct intel_sst_drv *sst_drv_ctx, int str_id);
int sst_send_byte_stream_mrfld(struct intel_sst_drv *ctx,
			struct snd_sst_bytes_v2 *sbytes);
int sst_set_stream_param(int str_id, struct snd_sst_params *str_param);
int sst_set_metadata(int str_id, char *params);
int sst_get_stream(struct intel_sst_drv *sst_drv_ctx,
		struct snd_sst_params *str_param);
int sst_get_stream_allocated(struct intel_sst_drv *ctx,
		struct snd_sst_params *str_param,
		struct snd_sst_lib_download **lib_dnld);
int sst_drain_stream(struct intel_sst_drv *sst_drv_ctx,
		int str_id, bool partial_drain);
int sst_post_message_mrfld(struct intel_sst_drv *ctx,
		struct ipc_post *msg, bool sync);
void sst_process_reply_mrfld(struct intel_sst_drv *ctx, struct ipc_post *msg);
int sst_start_mrfld(struct intel_sst_drv *ctx);
int intel_sst_reset_dsp_mrfld(struct intel_sst_drv *ctx);
void intel_sst_clear_intr_mrfld(struct intel_sst_drv *ctx);

int sst_load_fw(struct intel_sst_drv *ctx);
int sst_load_library(struct snd_sst_lib_download *lib, u8 ops);
void sst_post_download_mrfld(struct intel_sst_drv *ctx);
int sst_get_block_stream(struct intel_sst_drv *sst_drv_ctx);
void sst_memcpy_free_resources(struct intel_sst_drv *ctx);

int sst_wait_interruptible(struct intel_sst_drv *sst_drv_ctx,
				struct sst_block *block);
int sst_wait_timeout(struct intel_sst_drv *sst_drv_ctx,
			struct sst_block *block);
int sst_create_ipc_msg(struct ipc_post **arg, bool large);
int free_stream_context(struct intel_sst_drv *ctx, unsigned int str_id);
void sst_clean_stream(struct stream_info *stream);
int intel_sst_register_compress(struct intel_sst_drv *sst);
int intel_sst_remove_compress(struct intel_sst_drv *sst);
void sst_cdev_fragment_elapsed(struct intel_sst_drv *ctx, int str_id);
int sst_send_sync_msg(int ipc, int str_id);
int sst_get_num_channel(struct snd_sst_params *str_param);
int sst_get_sfreq(struct snd_sst_params *str_param);
int sst_alloc_stream_mrfld(struct intel_sst_drv *sst_drv_ctx, void *params);
void sst_restore_fw_context(void);
struct sst_block *sst_create_block(struct intel_sst_drv *ctx,
				u32 msg_id, u32 drv_id);
int sst_create_block_and_ipc_msg(struct ipc_post **arg, bool large,
		struct intel_sst_drv *sst_drv_ctx, struct sst_block **block,
		u32 msg_id, u32 drv_id);
int sst_free_block(struct intel_sst_drv *ctx, struct sst_block *freed);
int sst_wake_up_block(struct intel_sst_drv *ctx, int result,
		u32 drv_id, u32 ipc, void *data, u32 size);
int sst_request_firmware_async(struct intel_sst_drv *ctx);
int sst_driver_ops(struct intel_sst_drv *sst);
struct sst_platform_info *sst_get_acpi_driver_data(const char *hid);
void sst_firmware_load_cb(const struct firmware *fw, void *context);
int sst_prepare_and_post_msg(struct intel_sst_drv *sst,
		int task_id, int ipc_msg, int cmd_id, int pipe_id,
		size_t mbox_data_len, const void *mbox_data, void **data,
		bool large, bool fill_dsp, bool sync, bool response);

void sst_process_pending_msg(struct work_struct *work);
int sst_assign_pvt_id(struct intel_sst_drv *sst_drv_ctx);
void sst_init_stream(struct stream_info *stream,
		int codec, int sst_id, int ops, u8 slot);
int sst_validate_strid(struct intel_sst_drv *sst_drv_ctx, int str_id);
struct stream_info *get_stream_info(struct intel_sst_drv *sst_drv_ctx,
		int str_id);
int get_stream_id_mrfld(struct intel_sst_drv *sst_drv_ctx,
		u32 pipe_id);
u32 relocate_imr_addr_mrfld(u32 base_addr);
void sst_add_to_dispatch_list_and_post(struct intel_sst_drv *sst,
					struct ipc_post *msg);
int sst_pm_runtime_put(struct intel_sst_drv *sst_drv);
int sst_shim_write(void __iomem *addr, int offset, int value);
u32 sst_shim_read(void __iomem *addr, int offset);
u64 sst_reg_read64(void __iomem *addr, int offset);
int sst_shim_write64(void __iomem *addr, int offset, u64 value);
u64 sst_shim_read64(void __iomem *addr, int offset);
void sst_set_fw_state_locked(
		struct intel_sst_drv *sst_drv_ctx, int sst_state);
void sst_fill_header_mrfld(union ipc_header_mrfld *header,
				int msg, int task_id, int large, int drv_id);
void sst_fill_header_dsp(struct ipc_dsp_hdr *dsp, int msg,
					int pipe_id, int len);

int sst_register(struct device *);
int sst_unregister(struct device *);

int sst_alloc_drv_context(struct intel_sst_drv **ctx,
		struct device *dev, unsigned int dev_id);
int sst_context_init(struct intel_sst_drv *ctx);
void sst_context_cleanup(struct intel_sst_drv *ctx);
void sst_configure_runtime_pm(struct intel_sst_drv *ctx);
void memcpy32_toio(void __iomem *dst, const void *src, int count);
void memcpy32_fromio(void *dst, const void __iomem *src, int count);

#endif
