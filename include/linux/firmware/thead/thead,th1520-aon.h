/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 */

#ifndef _THEAD_AON_H
#define _THEAD_AON_H

#include <linux/device.h>
#include <linux/types.h>

#define AON_RPC_MSG_MAGIC (0xef)
#define TH1520_AON_RPC_VERSION 2
#define TH1520_AON_RPC_MSG_NUM 7

struct th1520_aon_chan;

enum th1520_aon_rpc_svc {
	TH1520_AON_RPC_SVC_UNKNOWN = 0,
	TH1520_AON_RPC_SVC_PM = 1,
	TH1520_AON_RPC_SVC_MISC = 2,
	TH1520_AON_RPC_SVC_AVFS = 3,
	TH1520_AON_RPC_SVC_SYS = 4,
	TH1520_AON_RPC_SVC_WDG = 5,
	TH1520_AON_RPC_SVC_LPM = 6,
	TH1520_AON_RPC_SVC_MAX = 0x3F,
};

enum th1520_aon_misc_func {
	TH1520_AON_MISC_FUNC_UNKNOWN = 0,
	TH1520_AON_MISC_FUNC_SET_CONTROL = 1,
	TH1520_AON_MISC_FUNC_GET_CONTROL = 2,
	TH1520_AON_MISC_FUNC_REGDUMP_CFG = 3,
};

enum th1520_aon_wdg_func {
	TH1520_AON_WDG_FUNC_UNKNOWN = 0,
	TH1520_AON_WDG_FUNC_START = 1,
	TH1520_AON_WDG_FUNC_STOP = 2,
	TH1520_AON_WDG_FUNC_PING = 3,
	TH1520_AON_WDG_FUNC_TIMEOUTSET = 4,
	TH1520_AON_WDG_FUNC_RESTART = 5,
	TH1520_AON_WDG_FUNC_GET_STATE = 6,
	TH1520_AON_WDG_FUNC_POWER_OFF = 7,
	TH1520_AON_WDG_FUNC_AON_WDT_ON = 8,
	TH1520_AON_WDG_FUNC_AON_WDT_OFF = 9,
};

enum th1520_aon_sys_func {
	TH1520_AON_SYS_FUNC_UNKNOWN = 0,
	TH1520_AON_SYS_FUNC_AON_RESERVE_MEM = 1,
};

enum th1520_aon_lpm_func {
	TH1520_AON_LPM_FUNC_UNKNOWN = 0,
	TH1520_AON_LPM_FUNC_REQUIRE_STR = 1,
	TH1520_AON_LPM_FUNC_RESUME_STR = 2,
	TH1520_AON_LPM_FUNC_REQUIRE_STD = 3,
	TH1520_AON_LPM_FUNC_CPUHP = 4,
	TH1520_AON_LPM_FUNC_REGDUMP_CFG = 5,
};

enum th1520_aon_pm_func {
	TH1520_AON_PM_FUNC_UNKNOWN = 0,
	TH1520_AON_PM_FUNC_SET_RESOURCE_REGULATOR = 1,
	TH1520_AON_PM_FUNC_GET_RESOURCE_REGULATOR = 2,
	TH1520_AON_PM_FUNC_SET_RESOURCE_POWER_MODE = 3,
	TH1520_AON_PM_FUNC_PWR_SET = 4,
	TH1520_AON_PM_FUNC_PWR_GET = 5,
	TH1520_AON_PM_FUNC_CHECK_FAULT = 6,
	TH1520_AON_PM_FUNC_GET_TEMPERATURE = 7,
};

struct th1520_aon_rpc_msg_hdr {
	u8 ver; /* version of msg hdr */
	u8 size; /*  msg size ,uinit in bytes,the size includes rpc msg header self */
	u8 svc; /* rpc main service id */
	u8 func; /* rpc sub func id of specific service, sent by caller */
} __packed __aligned(1);

struct th1520_aon_rpc_ack_common {
	struct th1520_aon_rpc_msg_hdr hdr;
	u8 err_code;
} __packed __aligned(1);

#define RPC_SVC_MSG_TYPE_DATA 0
#define RPC_SVC_MSG_TYPE_ACK 1
#define RPC_SVC_MSG_NEED_ACK 0
#define RPC_SVC_MSG_NO_NEED_ACK 1

#define RPC_GET_VER(MESG) ((MESG)->ver)
#define RPC_SET_VER(MESG, VER) ((MESG)->ver = (VER))
#define RPC_GET_SVC_ID(MESG) ((MESG)->svc & 0x3F)
#define RPC_SET_SVC_ID(MESG, ID) ((MESG)->svc |= 0x3F & (ID))
#define RPC_GET_SVC_FLAG_MSG_TYPE(MESG) (((MESG)->svc & 0x80) >> 7)
#define RPC_SET_SVC_FLAG_MSG_TYPE(MESG, TYPE) ((MESG)->svc |= (TYPE) << 7)
#define RPC_GET_SVC_FLAG_ACK_TYPE(MESG) (((MESG)->svc & 0x40) >> 6)
#define RPC_SET_SVC_FLAG_ACK_TYPE(MESG, ACK) ((MESG)->svc |= (ACK) << 6)

#define RPC_SET_BE64(MESG, OFFSET, SET_DATA)                                \
	do {                                                                \
		u8 *data = (u8 *)(MESG);                                    \
		u64 _offset = (OFFSET);                                     \
		u64 _set_data = (SET_DATA);                                 \
		data[_offset + 7] = _set_data & 0xFF;                       \
		data[_offset + 6] = (_set_data & 0xFF00) >> 8;              \
		data[_offset + 5] = (_set_data & 0xFF0000) >> 16;           \
		data[_offset + 4] = (_set_data & 0xFF000000) >> 24;         \
		data[_offset + 3] = (_set_data & 0xFF00000000) >> 32;       \
		data[_offset + 2] = (_set_data & 0xFF0000000000) >> 40;     \
		data[_offset + 1] = (_set_data & 0xFF000000000000) >> 48;   \
		data[_offset + 0] = (_set_data & 0xFF00000000000000) >> 56; \
	} while (0)

#define RPC_SET_BE32(MESG, OFFSET, SET_DATA)			    \
	do {							    \
		u8 *data = (u8 *)(MESG);			    \
		u64 _offset = (OFFSET);				    \
		u64 _set_data = (SET_DATA);			    \
		data[_offset + 3] = (_set_data) & 0xFF;		    \
		data[_offset + 2] = (_set_data & 0xFF00) >> 8;	    \
		data[_offset + 1] = (_set_data & 0xFF0000) >> 16;   \
		data[_offset + 0] = (_set_data & 0xFF000000) >> 24; \
	} while (0)

#define RPC_SET_BE16(MESG, OFFSET, SET_DATA)		       \
	do {						       \
		u8 *data = (u8 *)(MESG);		       \
		u64 _offset = (OFFSET);			       \
		u64 _set_data = (SET_DATA);		       \
		data[_offset + 1] = (_set_data) & 0xFF;	       \
		data[_offset + 0] = (_set_data & 0xFF00) >> 8; \
	} while (0)

#define RPC_SET_U8(MESG, OFFSET, SET_DATA)	  \
	do {					  \
		u8 *data = (u8 *)(MESG);	  \
		data[OFFSET] = (SET_DATA) & 0xFF; \
	} while (0)

#define RPC_GET_BE64(MESG, OFFSET, PTR)                                      \
	do {                                                                 \
		u8 *data = (u8 *)(MESG);                                     \
		u64 _offset = (OFFSET);                                      \
		*(u32 *)(PTR) =                                              \
			(data[_offset + 7] | data[_offset + 6] << 8 |        \
			 data[_offset + 5] << 16 | data[_offset + 4] << 24 | \
			 data[_offset + 3] << 32 | data[_offset + 2] << 40 | \
			 data[_offset + 1] << 48 | data[_offset + 0] << 56); \
	} while (0)

#define RPC_GET_BE32(MESG, OFFSET, PTR)                                      \
	do {                                                                 \
		u8 *data = (u8 *)(MESG);                                     \
		u64 _offset = (OFFSET);                                      \
		*(u32 *)(PTR) =                                              \
			(data[_offset + 3] | data[_offset + 2] << 8 |        \
			 data[_offset + 1] << 16 | data[_offset + 0] << 24); \
	} while (0)

#define RPC_GET_BE16(MESG, OFFSET, PTR)                                       \
	do {                                                                  \
		u8 *data = (u8 *)(MESG);                                      \
		u64 _offset = (OFFSET);                                       \
		*(u16 *)(PTR) = (data[_offset + 1] | data[_offset + 0] << 8); \
	} while (0)

#define RPC_GET_U8(MESG, OFFSET, PTR)          \
	do {                                   \
		u8 *data = (u8 *)(MESG);       \
		*(u8 *)(PTR) = (data[OFFSET]); \
	} while (0)

/*
 * Defines for SC PM Power Mode
 */
#define TH1520_AON_PM_PW_MODE_OFF 0 /* Power off */
#define TH1520_AON_PM_PW_MODE_STBY 1 /* Power in standby */
#define TH1520_AON_PM_PW_MODE_LP 2 /* Power in low-power */
#define TH1520_AON_PM_PW_MODE_ON 3 /* Power on */

/*
 * Defines for AON power islands
 */
#define TH1520_AON_AUDIO_PD 0
#define TH1520_AON_VDEC_PD 1
#define TH1520_AON_NPU_PD 2
#define TH1520_AON_VENC_PD 3
#define TH1520_AON_GPU_PD 4
#define TH1520_AON_DSP0_PD 5
#define TH1520_AON_DSP1_PD 6

struct th1520_aon_chan *th1520_aon_init(struct device *dev);
void th1520_aon_deinit(struct th1520_aon_chan *aon_chan);

int th1520_aon_call_rpc(struct th1520_aon_chan *aon_chan, void *msg);
int th1520_aon_power_update(struct th1520_aon_chan *aon_chan, u16 rsrc,
			    bool power_on);

#endif /* _THEAD_AON_H */
