/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __LINUX_USB_DWC3_MSM_H
#define __LINUX_USB_DWC3_MSM_H

#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>
#include <linux/usb/gadget.h>

/* used for struct usb_phy flags */
#define PHY_HOST_MODE			BIT(0)
#define DEVICE_IN_SS_MODE		BIT(1)
#define PHY_LANE_A			BIT(2)
#define PHY_LANE_B			BIT(3)
#define PHY_HSFS_MODE			BIT(4)
#define PHY_LS_MODE			BIT(5)
#define EUD_SPOOF_DISCONNECT		BIT(6)
#define EUD_SPOOF_CONNECT		BIT(7)
#define PHY_SUS_OVERRIDE		BIT(8)
#define PHY_DP_MODE			BIT(9)
#define PHY_USB_DP_CONCURRENT_MODE	BIT(10)

/*
 * The following are bit fields describing the USB BAM options.
 * These bit fields are set by function drivers that wish to queue
 * usb_requests with sps/bam parameters.
 */
#define MSM_TX_PIPE_ID_OFS		(16)
#define MSM_SPS_MODE			BIT(5)
#define MSM_IS_FINITE_TRANSFER		BIT(6)
#define MSM_PRODUCER			BIT(7)
#define MSM_DISABLE_WB			BIT(8)
#define MSM_ETD_IOC			BIT(9)
#define MSM_INTERNAL_MEM		BIT(10)
#define MSM_VENDOR_ID			BIT(16)

/* EBC TRB parameters */
#define EBC_TRB_SIZE			16384

/* Operations codes for GSI enabled EPs */
enum gsi_ep_op {
	GSI_EP_OP_CONFIG = 0,
	GSI_EP_OP_STARTXFER,
	GSI_EP_OP_STORE_DBL_INFO,
	GSI_EP_OP_ENABLE_GSI,
	GSI_EP_OP_UPDATEXFER,
	GSI_EP_OP_RING_DB,
	GSI_EP_OP_ENDXFER,
	GSI_EP_OP_GET_CH_INFO,
	GSI_EP_OP_GET_XFER_IDX,
	GSI_EP_OP_PREPARE_TRBS,
	GSI_EP_OP_FREE_TRBS,
	GSI_EP_OP_SET_CLR_BLOCK_DBL,
	GSI_EP_OP_CHECK_FOR_SUSPEND,
	GSI_EP_OP_DISABLE,
};

enum usb_hw_ep_mode {
	USB_EP_NONE,
	USB_EP_BAM,
	USB_EP_GSI,
	USB_EP_EBC,
};

enum dwc3_notify_event {
	DWC3_CONTROLLER_ERROR_EVENT,
	DWC3_CONTROLLER_RESET_EVENT,
	DWC3_CONTROLLER_POST_RESET_EVENT,
	DWC3_CORE_PM_SUSPEND_EVENT,
	DWC3_CORE_PM_RESUME_EVENT,
	DWC3_CONTROLLER_CONNDONE_EVENT,
	DWC3_CONTROLLER_NOTIFY_OTG_EVENT,
	DWC3_CONTROLLER_NOTIFY_DISABLE_UPDXFER,
	DWC3_CONTROLLER_PULLUP_ENTER,
	DWC3_CONTROLLER_PULLUP_EXIT,

	/* USB GSI event buffer related notification */
	DWC3_GSI_EVT_BUF_ALLOC,
	DWC3_GSI_EVT_BUF_SETUP,
	DWC3_GSI_EVT_BUF_CLEANUP,
	DWC3_GSI_EVT_BUF_CLEAR,
	DWC3_GSI_EVT_BUF_FREE,
	DWC3_CONTROLLER_NOTIFY_CLEAR_DB,
	DWC3_IMEM_UPDATE_PID,
};

/*
 * @buf_base_addr: Base pointer to buffer allocated for each GSI enabled EP.
 *	TRBs point to buffers that are split from this pool. The size of the
 *	buffer is num_bufs times buf_len. num_bufs and buf_len are determined
	based on desired performance and aggregation size.
 * @dma: DMA address corresponding to buf_base_addr.
 * @num_bufs: Number of buffers associated with the GSI enabled EP. This
 *	corresponds to the number of non-zlp TRBs allocated for the EP.
 *	The value is determined based on desired performance for the EP.
 * @buf_len: Size of each individual buffer is determined based on aggregation
 *	negotiated as per the protocol. In case of no aggregation supported by
 *	the protocol, we use default values.
 * @db_reg_phs_addr_lsb: IPA channel doorbell register's physical address LSB
 * @mapped_db_reg_phs_addr_lsb: doorbell LSB IOVA address mapped with IOMMU
 * @db_reg_phs_addr_msb: IPA channel doorbell register's physical address MSB
 * @ep_intr_num: Interrupter number for EP.
 */
struct usb_gsi_request {
	void *buf_base_addr;
	dma_addr_t dma;
	size_t num_bufs;
	size_t buf_len;
	u32 db_reg_phs_addr_lsb;
	dma_addr_t mapped_db_reg_phs_addr_lsb;
	u32 db_reg_phs_addr_msb;
	u8 ep_intr_num;
	struct sg_table sgt_trb_xfer_ring;
	struct sg_table sgt_data_buff;
};

/*
 * @last_trb_addr: Address (LSB - based on alignment restrictions) of
 *	last TRB in queue. Used to identify rollover case.
 * @const_buffer_size: TRB buffer size in KB (similar to IPA aggregation
 *	configuration). Must be aligned to Max USB Packet Size.
 *	Should be 1 <= const_buffer_size <= 31.
 * @depcmd_low_addr: Used by GSI hardware to write "Update Transfer" cmd
 * @depcmd_hi_addr: Used to write "Update Transfer" command.
 * @gevntcount_low_addr: GEVNCOUNT low address for GSI hardware to read and
 *	clear processed events.
 * @gevntcount_hi_addr:	GEVNCOUNT high address.
 * @xfer_ring_len: length of transfer ring in bytes (must be integral
 *	multiple of TRB size - 16B for xDCI).
 * @xfer_ring_base_addr: physical base address of transfer ring. Address must
 *	be aligned to xfer_ring_len rounded to power of two.
 * @ch_req: Used to pass request specific info for certain operations on GSI EP
 */
struct gsi_channel_info {
	u16 last_trb_addr;
	u8 const_buffer_size;
	u32 depcmd_low_addr;
	u8 depcmd_hi_addr;
	u32 gevntcount_low_addr;
	u8 gevntcount_hi_addr;
	u16 xfer_ring_len;
	u64 xfer_ring_base_addr;
	struct usb_gsi_request *ch_req;
};

struct dwc3;
extern void *dwc_trace_ipc_log_ctxt;

/**
 * usb_gadget_autopm_get - increment PM-usage counter of usb gadget's parent
 * device.
 * @gadget: usb gadget whose parent device counter is incremented
 *
 * This routine should be called by function driver when it wants to use
 * gadget's parent device and needs to guarantee that it is not suspended. In
 * addition, the routine prevents subsequent autosuspends of gadget's parent
 * device. However if the autoresume fails then the counter is re-decremented.
 *
 * This routine can run only in process context.
 */
static inline int usb_gadget_autopm_get(struct usb_gadget *gadget)
{
	int status = -ENODEV;

	if (!gadget || !gadget->dev.parent)
		return status;

	status = pm_runtime_get_sync(gadget->dev.parent);
	if (status < 0)
		pm_runtime_put_sync(gadget->dev.parent);

	if (status > 0)
		status = 0;
	return status;
}

/**
 * usb_gadget_autopm_get_async - increment PM-usage counter of usb gadget's
 * parent device.
 * @gadget: usb gadget whose parent device counter is incremented
 *
 * This routine increments @gadget parent device PM usage counter and queue an
 * autoresume request if the device is suspended. It does not autoresume device
 * directly (it only queues a request). After a successful call, the device may
 * not yet be resumed.
 *
 * This routine can run in atomic context.
 */
static inline int usb_gadget_autopm_get_async(struct usb_gadget *gadget)
{
	int status = -ENODEV;

	if (!gadget || !gadget->dev.parent)
		return status;

	status = pm_runtime_get(gadget->dev.parent);
	if (status < 0 && status != -EINPROGRESS)
		pm_runtime_put_noidle(gadget->dev.parent);

	if (status > 0 || status == -EINPROGRESS)
		status = 0;
	return status;
}

/**
 * usb_gadget_autopm_get_noresume - increment PM-usage counter of usb gadget's
 * parent device.
 * @gadget: usb gadget whose parent device counter is incremented
 *
 * This routine increments PM-usage count of @gadget parent device but does not
 * carry out an autoresume.
 *
 * This routine can run in atomic context.
 */
static inline void usb_gadget_autopm_get_noresume(struct usb_gadget *gadget)
{
	if (gadget && gadget->dev.parent)
		pm_runtime_get_noresume(gadget->dev.parent);
}

/**
 * usb_gadget_autopm_put - decrement PM-usage counter of usb gadget's parent
 * device.
 * @gadget: usb gadget whose parent device counter is decremented.
 *
 * This routine should be called by function driver when it is finished using
 * @gadget parent device and wants to allow it to autosuspend. It decrements
 * PM-usage counter of @gadget parent device, when the counter reaches 0, a
 * delayed autosuspend request is attempted.
 *
 * This routine can run only in process context.
 */
static inline void usb_gadget_autopm_put(struct usb_gadget *gadget)
{
	if (gadget && gadget->dev.parent)
		pm_runtime_put_sync(gadget->dev.parent);
}

/**
 * usb_gadget_autopm_put_async - decrement PM-usage counter of usb gadget's
 * parent device.
 * @gadget: usb gadget whose parent device counter is decremented.
 *
 * This routine decrements PM-usage counter of @gadget parent device and
 * schedules a delayed autosuspend request if the counter is <= 0.
 *
 * This routine can run in atomic context.
 */
static inline void usb_gadget_autopm_put_async(struct usb_gadget *gadget)
{
	if (gadget && gadget->dev.parent)
		pm_runtime_put(gadget->dev.parent);
}

/**
 * usb_gadget_autopm_put_no_suspend - decrement PM-usage counter of usb gadget
's
 * parent device.
 * @gadget: usb gadget whose parent device counter is decremented.
 *
 * This routine decrements PM-usage counter of @gadget parent device but does
 * not carry out an autosuspend.
 *
 * This routine can run in atomic context.
 */
static inline void usb_gadget_autopm_put_no_suspend(struct usb_gadget *gadget)
{
	if (gadget && gadget->dev.parent)
		pm_runtime_put_noidle(gadget->dev.parent);
}

#if IS_ENABLED(CONFIG_USB_DWC3_MSM)
void dwc3_msm_notify_event(struct dwc3 *dwc,
		enum dwc3_notify_event event, unsigned int value);
int usb_gsi_ep_op(struct usb_ep *ep, void *op_data, enum gsi_ep_op op);
int msm_ep_config(struct usb_ep *ep, struct usb_request *request, u32 bam_opts);
int msm_ep_unconfig(struct usb_ep *ep);
void dwc3_tx_fifo_resize_request(struct usb_ep *ep, bool qdss_enable);
int msm_data_fifo_config(struct usb_ep *ep, unsigned long addr, u32 size,
	u8 dst_pipe_idx);
int msm_dwc3_reset_dbm_ep(struct usb_ep *ep);
int dwc3_msm_set_dp_mode(struct device *dev, bool connected, int lanes);
int msm_ep_update_ops(struct usb_ep *ep);
int msm_ep_clear_ops(struct usb_ep *ep);
int msm_ep_set_mode(struct usb_ep *ep, enum usb_hw_ep_mode mode);
int dwc3_core_stop_hw_active_transfers(struct dwc3 *dwc);
#else
void dwc3_msm_notify_event(struct dwc3 *dwc,
		enum dwc3_notify_event event, unsigned int value)
{ }
static inline int usb_gsi_ep_op(struct usb_ep *ep, void *op_data,
		enum gsi_ep_op op)
{ return 0; }
static inline int msm_data_fifo_config(struct usb_ep *ep, unsigned long addr,
	u32 size, u8 dst_pipe_idx)
{ return -ENODEV; }
static inline int msm_ep_config(struct usb_ep *ep, struct usb_request *request,
		u32 bam_opts)
{ return -ENODEV; }
static inline int msm_ep_unconfig(struct usb_ep *ep)
{ return -ENODEV; }
static inline void dwc3_tx_fifo_resize_request(struct usb_ep *ep,
	bool qdss_enable)
{ }
static inline bool msm_dwc3_reset_ep_after_lpm(struct usb_gadget *gadget)
{ return false; }
static inline int dwc3_msm_set_dp_mode(struct device *dev, bool connected, int lanes)
{ return -ENODEV; }
int msm_ep_update_ops(struct usb_ep *ep)
{ return -ENODEV; }
int msm_ep_clear_ops(struct usb_ep *ep)
{ return -ENODEV; }
int msm_ep_set_mode(struct usb_ep *ep, enum usb_hw_ep_mode mode)
{ return -ENODEV; }
inline int dwc3_core_stop_hw_active_transfers(struct dwc3 *dwc)
{ return 0; }
#endif

#ifdef CONFIG_ARM64
int dwc3_msm_kretprobe_init(void);
void dwc3_msm_kretprobe_exit(void);
#else
int dwc3_msm_kretprobe_init(void)
{ return 0; }
void dwc3_msm_kretprobe_exit(void)
{ }
#endif

#endif /* __LINUX_USB_DWC3_MSM_H */
