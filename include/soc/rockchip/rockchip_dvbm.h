/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd
 */
#ifndef __SOC_ROCKCHIP_DVBM_H
#define __SOC_ROCKCHIP_DVBM_H

#include <linux/dma-buf.h>
#include <linux/platform_device.h>

enum dvbm_port_dir {
	DVBM_ISP_PORT,
	DVBM_VEPU_PORT,
};

enum dvbm_cmd {
	DVBM_ISP_CMD_BASE   = 0,
	DVBM_ISP_SET_CFG,
	DVBM_ISP_FRM_START,
	DVBM_ISP_FRM_END,
	DVBM_ISP_FRM_QUARTER,
	DVBM_ISP_FRM_HALF,
	DVBM_ISP_FRM_THREE_QUARTERS,
	DVBM_ISP_CMD_BUTT,

	DVBM_VEPU_CMD_BASE  = 0x10,
	DVBM_VEPU_SET_RESYNC,
	DVBM_VEPU_SET_CFG,
	DVBM_VEPU_GET_ADR,
	DVBM_VEPU_GET_FRAME_INFO,
	DVBM_VEPU_DUMP_REGS,
	DVBM_VEPU_CMD_BUTT,
};

enum isp_frame_status {
	ISP_FRAME_START,
	ISP_FRAME_ONE_QUARTER,
	ISP_FRAME_HALF,
	ISP_FRAME_THREE_QUARTERS,
	ISP_FRAME_FINISH,
};

enum dvbm_cb_event {
	DVBM_ISP_EVENT_BASE   = 0,
	DVBM_ISP_EVENT_BUTT,

	DVBM_VEPU_EVENT_BASE  = 0x10,
	DVBM_VEPU_NOTIFY_ADDR,
	DVBM_VEPU_NOTIFY_DUMP,
	DVBM_VEPU_REQ_CONNECT,
	DVBM_VEPU_NOTIFY_FRM_STR,
	DVBM_VEPU_NOTIFY_FRM_END,
	DVBM_VEPU_NOTIFY_FRM_INFO,
	DVBM_VEPU_EVENT_BUTT,
};

struct dvbm_port {
	enum dvbm_port_dir dir;
	u32 linked;
};

struct dvbm_isp_cfg_t {
	u32 fmt;
	u32 timeout;

	struct dmabuf *buf;
	dma_addr_t dma_addr;
	u32 ybuf_top;
	u32 ybuf_bot;
	u32 ybuf_lstd;
	u32 ybuf_fstd;
	u32 cbuf_top;
	u32 cbuf_bot;
	u32 cbuf_lstd;
	u32 cbuf_fstd;
};

struct dvbm_isp_frm_cfg {
	s32 frm_idx;
	u32 ybuf_start;
	u32 cbuf_start;
};

struct dvbm_isp_frm_info {
	u32 frame_cnt;
	u32 line_cnt;
	u32 wrap_line;
	u32 max_line_cnt;
};

struct dvbm_addr_cfg {
	u32 ybuf_top;
	u32 ybuf_bot;
	u32 ybuf_sadr;
	u32 cbuf_top;
	u32 cbuf_bot;
	u32 cbuf_sadr;
	u32 frame_id;
	u32 line_cnt;
	u32 overflow;
};

struct dvbm_vepu_cfg {
	u32 auto_resyn;
	u32 ignore_vepu_cnct_ack;
	u32 start_point_after_vepu_cnct;
};

typedef int (*dvbm_callback)(void *ctx, enum dvbm_cb_event event, void *arg);

struct dvbm_cb {
	dvbm_callback cb;
	void *ctx;
	int event;
};

#if IS_ENABLED(CONFIG_ROCKCHIP_DVBM)

struct dvbm_port *rk_dvbm_get_port(struct platform_device *pdev,
				   enum dvbm_port_dir dir);
int rk_dvbm_put(struct dvbm_port *port);
int rk_dvbm_link(struct dvbm_port *port);
int rk_dvbm_unlink(struct dvbm_port *port);
int rk_dvbm_set_cb(struct dvbm_port *port, struct dvbm_cb *cb);
int rk_dvbm_ctrl(struct dvbm_port *port, enum dvbm_cmd cmd, void *arg);

#else

static inline struct dvbm_port *rk_dvbm_get_port(struct platform_device *pdev,
						 enum dvbm_port_dir dir)
{
	return ERR_PTR(-ENODEV);
}

static inline int rk_dvbm_put(struct dvbm_port *port)
{
	return -ENODEV;
}

static inline int rk_dvbm_link(struct dvbm_port *port)
{
	return -ENODEV;
}
static inline int rk_dvbm_unlink(struct dvbm_port *port)
{
	return -ENODEV;
}

static inline int rk_dvbm_set_cb(struct dvbm_port *port, struct dvbm_cb *cb)
{
	return -ENODEV;
}

static inline int rk_dvbm_ctrl(struct dvbm_port *port, enum dvbm_cmd cmd, void *arg)
{
	return -ENODEV;
}

#endif

#endif
