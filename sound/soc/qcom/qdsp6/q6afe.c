// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include <dt-bindings/sound/qcom,q6afe.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/soc/qcom/apr.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "q6dsp-errno.h"
#include "q6core.h"
#include "q6afe.h"

/* AFE CMDs */
#define AFE_PORT_CMD_DEVICE_START	0x000100E5
#define AFE_PORT_CMD_DEVICE_STOP	0x000100E6
#define AFE_PORT_CMD_SET_PARAM_V2	0x000100EF
#define AFE_SVC_CMD_SET_PARAM		0x000100f3
#define AFE_PORT_CMDRSP_GET_PARAM_V2	0x00010106
#define AFE_PARAM_ID_HDMI_CONFIG	0x00010210
#define AFE_MODULE_AUDIO_DEV_INTERFACE	0x0001020C
#define AFE_MODULE_TDM			0x0001028A

#define AFE_PARAM_ID_CDC_SLIMBUS_SLAVE_CFG 0x00010235
#define AFE_PARAM_ID_USB_AUDIO_DEV_PARAMS    0x000102A5
#define AFE_PARAM_ID_USB_AUDIO_DEV_LPCM_FMT 0x000102AA

#define AFE_PARAM_ID_LPAIF_CLK_CONFIG	0x00010238
#define AFE_PARAM_ID_INT_DIGITAL_CDC_CLK_CONFIG	0x00010239

#define AFE_PARAM_ID_SLIMBUS_CONFIG    0x00010212
#define AFE_PARAM_ID_I2S_CONFIG	0x0001020D
#define AFE_PARAM_ID_TDM_CONFIG	0x0001029D
#define AFE_PARAM_ID_PORT_SLOT_MAPPING_CONFIG	0x00010297
#define AFE_PARAM_ID_CODEC_DMA_CONFIG	0x000102B8
#define AFE_PARAM_ID_USB_AUDIO_CONFIG    0x000102A4
#define AFE_CMD_REMOTE_LPASS_CORE_HW_VOTE_REQUEST	0x000100f4
#define AFE_CMD_RSP_REMOTE_LPASS_CORE_HW_VOTE_REQUEST   0x000100f5
#define AFE_CMD_REMOTE_LPASS_CORE_HW_DEVOTE_REQUEST	0x000100f6

/* I2S config specific */
#define AFE_API_VERSION_I2S_CONFIG	0x1
#define AFE_PORT_I2S_SD0		0x1
#define AFE_PORT_I2S_SD1		0x2
#define AFE_PORT_I2S_SD2		0x3
#define AFE_PORT_I2S_SD3		0x4
#define AFE_PORT_I2S_SD0_MASK		BIT(0x0)
#define AFE_PORT_I2S_SD1_MASK		BIT(0x1)
#define AFE_PORT_I2S_SD2_MASK		BIT(0x2)
#define AFE_PORT_I2S_SD3_MASK		BIT(0x3)
#define AFE_PORT_I2S_SD0_1_MASK		GENMASK(1, 0)
#define AFE_PORT_I2S_SD2_3_MASK		GENMASK(3, 2)
#define AFE_PORT_I2S_SD0_1_2_MASK	GENMASK(2, 0)
#define AFE_PORT_I2S_SD0_1_2_3_MASK	GENMASK(3, 0)
#define AFE_PORT_I2S_QUAD01		0x5
#define AFE_PORT_I2S_QUAD23		0x6
#define AFE_PORT_I2S_6CHS		0x7
#define AFE_PORT_I2S_8CHS		0x8
#define AFE_PORT_I2S_MONO		0x0
#define AFE_PORT_I2S_STEREO		0x1
#define AFE_PORT_CONFIG_I2S_WS_SRC_EXTERNAL	0x0
#define AFE_PORT_CONFIG_I2S_WS_SRC_INTERNAL	0x1
#define AFE_LINEAR_PCM_DATA				0x0

#define AFE_API_MINOR_VERSION_USB_AUDIO_CONFIG 0x1

/* Port IDs */
#define AFE_API_VERSION_HDMI_CONFIG	0x1
#define AFE_PORT_ID_MULTICHAN_HDMI_RX	0x100E
#define AFE_PORT_ID_HDMI_OVER_DP_RX	0x6020

/* USB AFE port */
#define AFE_PORT_ID_USB_RX                       0x7000

#define AFE_API_VERSION_SLIMBUS_CONFIG 0x1
/* Clock set API version */
#define AFE_API_VERSION_CLOCK_SET 1
#define Q6AFE_LPASS_CLK_CONFIG_API_VERSION	0x1
#define AFE_MODULE_CLOCK_SET		0x0001028F
#define AFE_PARAM_ID_CLOCK_SET		0x00010290

/* SLIMbus Rx port on channel 0. */
#define AFE_PORT_ID_SLIMBUS_MULTI_CHAN_0_RX      0x4000
/* SLIMbus Tx port on channel 0. */
#define AFE_PORT_ID_SLIMBUS_MULTI_CHAN_0_TX      0x4001
/* SLIMbus Rx port on channel 1. */
#define AFE_PORT_ID_SLIMBUS_MULTI_CHAN_1_RX      0x4002
/* SLIMbus Tx port on channel 1. */
#define AFE_PORT_ID_SLIMBUS_MULTI_CHAN_1_TX      0x4003
/* SLIMbus Rx port on channel 2. */
#define AFE_PORT_ID_SLIMBUS_MULTI_CHAN_2_RX      0x4004
/* SLIMbus Tx port on channel 2. */
#define AFE_PORT_ID_SLIMBUS_MULTI_CHAN_2_TX      0x4005
/* SLIMbus Rx port on channel 3. */
#define AFE_PORT_ID_SLIMBUS_MULTI_CHAN_3_RX      0x4006
/* SLIMbus Tx port on channel 3. */
#define AFE_PORT_ID_SLIMBUS_MULTI_CHAN_3_TX      0x4007
/* SLIMbus Rx port on channel 4. */
#define AFE_PORT_ID_SLIMBUS_MULTI_CHAN_4_RX      0x4008
/* SLIMbus Tx port on channel 4. */
#define AFE_PORT_ID_SLIMBUS_MULTI_CHAN_4_TX      0x4009
/* SLIMbus Rx port on channel 5. */
#define AFE_PORT_ID_SLIMBUS_MULTI_CHAN_5_RX      0x400a
/* SLIMbus Tx port on channel 5. */
#define AFE_PORT_ID_SLIMBUS_MULTI_CHAN_5_TX      0x400b
/* SLIMbus Rx port on channel 6. */
#define AFE_PORT_ID_SLIMBUS_MULTI_CHAN_6_RX      0x400c
/* SLIMbus Tx port on channel 6. */
#define AFE_PORT_ID_SLIMBUS_MULTI_CHAN_6_TX      0x400d
#define AFE_PORT_ID_PRIMARY_MI2S_RX         0x1000
#define AFE_PORT_ID_PRIMARY_MI2S_TX         0x1001
#define AFE_PORT_ID_SECONDARY_MI2S_RX       0x1002
#define AFE_PORT_ID_SECONDARY_MI2S_TX       0x1003
#define AFE_PORT_ID_TERTIARY_MI2S_RX        0x1004
#define AFE_PORT_ID_TERTIARY_MI2S_TX        0x1005
#define AFE_PORT_ID_QUATERNARY_MI2S_RX      0x1006
#define AFE_PORT_ID_QUATERNARY_MI2S_TX      0x1007
#define AFE_PORT_ID_QUINARY_MI2S_RX	    0x1016
#define AFE_PORT_ID_QUINARY_MI2S_TX	    0x1017

/* Start of the range of port IDs for TDM devices. */
#define AFE_PORT_ID_TDM_PORT_RANGE_START	0x9000

/* End of the range of port IDs for TDM devices. */
#define AFE_PORT_ID_TDM_PORT_RANGE_END \
	(AFE_PORT_ID_TDM_PORT_RANGE_START+0x50-1)

/* Size of the range of port IDs for TDM ports. */
#define AFE_PORT_ID_TDM_PORT_RANGE_SIZE \
	(AFE_PORT_ID_TDM_PORT_RANGE_END - \
	AFE_PORT_ID_TDM_PORT_RANGE_START+1)

#define AFE_PORT_ID_PRIMARY_TDM_RX \
	(AFE_PORT_ID_TDM_PORT_RANGE_START + 0x00)
#define AFE_PORT_ID_PRIMARY_TDM_RX_1 \
	(AFE_PORT_ID_PRIMARY_TDM_RX + 0x02)
#define AFE_PORT_ID_PRIMARY_TDM_RX_2 \
	(AFE_PORT_ID_PRIMARY_TDM_RX + 0x04)
#define AFE_PORT_ID_PRIMARY_TDM_RX_3 \
	(AFE_PORT_ID_PRIMARY_TDM_RX + 0x06)
#define AFE_PORT_ID_PRIMARY_TDM_RX_4 \
	(AFE_PORT_ID_PRIMARY_TDM_RX + 0x08)
#define AFE_PORT_ID_PRIMARY_TDM_RX_5 \
	(AFE_PORT_ID_PRIMARY_TDM_RX + 0x0A)
#define AFE_PORT_ID_PRIMARY_TDM_RX_6 \
	(AFE_PORT_ID_PRIMARY_TDM_RX + 0x0C)
#define AFE_PORT_ID_PRIMARY_TDM_RX_7 \
	(AFE_PORT_ID_PRIMARY_TDM_RX + 0x0E)

#define AFE_PORT_ID_PRIMARY_TDM_TX \
	(AFE_PORT_ID_TDM_PORT_RANGE_START + 0x01)
#define AFE_PORT_ID_PRIMARY_TDM_TX_1 \
	(AFE_PORT_ID_PRIMARY_TDM_TX + 0x02)
#define AFE_PORT_ID_PRIMARY_TDM_TX_2 \
	(AFE_PORT_ID_PRIMARY_TDM_TX + 0x04)
#define AFE_PORT_ID_PRIMARY_TDM_TX_3 \
	(AFE_PORT_ID_PRIMARY_TDM_TX + 0x06)
#define AFE_PORT_ID_PRIMARY_TDM_TX_4 \
	(AFE_PORT_ID_PRIMARY_TDM_TX + 0x08)
#define AFE_PORT_ID_PRIMARY_TDM_TX_5 \
	(AFE_PORT_ID_PRIMARY_TDM_TX + 0x0A)
#define AFE_PORT_ID_PRIMARY_TDM_TX_6 \
	(AFE_PORT_ID_PRIMARY_TDM_TX + 0x0C)
#define AFE_PORT_ID_PRIMARY_TDM_TX_7 \
	(AFE_PORT_ID_PRIMARY_TDM_TX + 0x0E)

#define AFE_PORT_ID_SECONDARY_TDM_RX \
	(AFE_PORT_ID_TDM_PORT_RANGE_START + 0x10)
#define AFE_PORT_ID_SECONDARY_TDM_RX_1 \
	(AFE_PORT_ID_SECONDARY_TDM_RX + 0x02)
#define AFE_PORT_ID_SECONDARY_TDM_RX_2 \
	(AFE_PORT_ID_SECONDARY_TDM_RX + 0x04)
#define AFE_PORT_ID_SECONDARY_TDM_RX_3 \
	(AFE_PORT_ID_SECONDARY_TDM_RX + 0x06)
#define AFE_PORT_ID_SECONDARY_TDM_RX_4 \
	(AFE_PORT_ID_SECONDARY_TDM_RX + 0x08)
#define AFE_PORT_ID_SECONDARY_TDM_RX_5 \
	(AFE_PORT_ID_SECONDARY_TDM_RX + 0x0A)
#define AFE_PORT_ID_SECONDARY_TDM_RX_6 \
	(AFE_PORT_ID_SECONDARY_TDM_RX + 0x0C)
#define AFE_PORT_ID_SECONDARY_TDM_RX_7 \
	(AFE_PORT_ID_SECONDARY_TDM_RX + 0x0E)

#define AFE_PORT_ID_SECONDARY_TDM_TX \
	(AFE_PORT_ID_TDM_PORT_RANGE_START + 0x11)
#define AFE_PORT_ID_SECONDARY_TDM_TX_1 \
	(AFE_PORT_ID_SECONDARY_TDM_TX + 0x02)
#define AFE_PORT_ID_SECONDARY_TDM_TX_2 \
	(AFE_PORT_ID_SECONDARY_TDM_TX + 0x04)
#define AFE_PORT_ID_SECONDARY_TDM_TX_3 \
	(AFE_PORT_ID_SECONDARY_TDM_TX + 0x06)
#define AFE_PORT_ID_SECONDARY_TDM_TX_4 \
	(AFE_PORT_ID_SECONDARY_TDM_TX + 0x08)
#define AFE_PORT_ID_SECONDARY_TDM_TX_5 \
	(AFE_PORT_ID_SECONDARY_TDM_TX + 0x0A)
#define AFE_PORT_ID_SECONDARY_TDM_TX_6 \
	(AFE_PORT_ID_SECONDARY_TDM_TX + 0x0C)
#define AFE_PORT_ID_SECONDARY_TDM_TX_7 \
	(AFE_PORT_ID_SECONDARY_TDM_TX + 0x0E)

#define AFE_PORT_ID_TERTIARY_TDM_RX \
	(AFE_PORT_ID_TDM_PORT_RANGE_START + 0x20)
#define AFE_PORT_ID_TERTIARY_TDM_RX_1 \
	(AFE_PORT_ID_TERTIARY_TDM_RX + 0x02)
#define AFE_PORT_ID_TERTIARY_TDM_RX_2 \
	(AFE_PORT_ID_TERTIARY_TDM_RX + 0x04)
#define AFE_PORT_ID_TERTIARY_TDM_RX_3 \
	(AFE_PORT_ID_TERTIARY_TDM_RX + 0x06)
#define AFE_PORT_ID_TERTIARY_TDM_RX_4 \
	(AFE_PORT_ID_TERTIARY_TDM_RX + 0x08)
#define AFE_PORT_ID_TERTIARY_TDM_RX_5 \
	(AFE_PORT_ID_TERTIARY_TDM_RX + 0x0A)
#define AFE_PORT_ID_TERTIARY_TDM_RX_6 \
	(AFE_PORT_ID_TERTIARY_TDM_RX + 0x0C)
#define AFE_PORT_ID_TERTIARY_TDM_RX_7 \
	(AFE_PORT_ID_TERTIARY_TDM_RX + 0x0E)

#define AFE_PORT_ID_TERTIARY_TDM_TX \
	(AFE_PORT_ID_TDM_PORT_RANGE_START + 0x21)
#define AFE_PORT_ID_TERTIARY_TDM_TX_1 \
	(AFE_PORT_ID_TERTIARY_TDM_TX + 0x02)
#define AFE_PORT_ID_TERTIARY_TDM_TX_2 \
	(AFE_PORT_ID_TERTIARY_TDM_TX + 0x04)
#define AFE_PORT_ID_TERTIARY_TDM_TX_3 \
	(AFE_PORT_ID_TERTIARY_TDM_TX + 0x06)
#define AFE_PORT_ID_TERTIARY_TDM_TX_4 \
	(AFE_PORT_ID_TERTIARY_TDM_TX + 0x08)
#define AFE_PORT_ID_TERTIARY_TDM_TX_5 \
	(AFE_PORT_ID_TERTIARY_TDM_TX + 0x0A)
#define AFE_PORT_ID_TERTIARY_TDM_TX_6 \
	(AFE_PORT_ID_TERTIARY_TDM_TX + 0x0C)
#define AFE_PORT_ID_TERTIARY_TDM_TX_7 \
	(AFE_PORT_ID_TERTIARY_TDM_TX + 0x0E)

#define AFE_PORT_ID_QUATERNARY_TDM_RX \
	(AFE_PORT_ID_TDM_PORT_RANGE_START + 0x30)
#define AFE_PORT_ID_QUATERNARY_TDM_RX_1 \
	(AFE_PORT_ID_QUATERNARY_TDM_RX + 0x02)
#define AFE_PORT_ID_QUATERNARY_TDM_RX_2 \
	(AFE_PORT_ID_QUATERNARY_TDM_RX + 0x04)
#define AFE_PORT_ID_QUATERNARY_TDM_RX_3 \
	(AFE_PORT_ID_QUATERNARY_TDM_RX + 0x06)
#define AFE_PORT_ID_QUATERNARY_TDM_RX_4 \
	(AFE_PORT_ID_QUATERNARY_TDM_RX + 0x08)
#define AFE_PORT_ID_QUATERNARY_TDM_RX_5 \
	(AFE_PORT_ID_QUATERNARY_TDM_RX + 0x0A)
#define AFE_PORT_ID_QUATERNARY_TDM_RX_6 \
	(AFE_PORT_ID_QUATERNARY_TDM_RX + 0x0C)
#define AFE_PORT_ID_QUATERNARY_TDM_RX_7 \
	(AFE_PORT_ID_QUATERNARY_TDM_RX + 0x0E)

#define AFE_PORT_ID_QUATERNARY_TDM_TX \
	(AFE_PORT_ID_TDM_PORT_RANGE_START + 0x31)
#define AFE_PORT_ID_QUATERNARY_TDM_TX_1 \
	(AFE_PORT_ID_QUATERNARY_TDM_TX + 0x02)
#define AFE_PORT_ID_QUATERNARY_TDM_TX_2 \
	(AFE_PORT_ID_QUATERNARY_TDM_TX + 0x04)
#define AFE_PORT_ID_QUATERNARY_TDM_TX_3 \
	(AFE_PORT_ID_QUATERNARY_TDM_TX + 0x06)
#define AFE_PORT_ID_QUATERNARY_TDM_TX_4 \
	(AFE_PORT_ID_QUATERNARY_TDM_TX + 0x08)
#define AFE_PORT_ID_QUATERNARY_TDM_TX_5 \
	(AFE_PORT_ID_QUATERNARY_TDM_TX + 0x0A)
#define AFE_PORT_ID_QUATERNARY_TDM_TX_6 \
	(AFE_PORT_ID_QUATERNARY_TDM_TX + 0x0C)
#define AFE_PORT_ID_QUATERNARY_TDM_TX_7 \
	(AFE_PORT_ID_QUATERNARY_TDM_TX + 0x0E)

#define AFE_PORT_ID_QUINARY_TDM_RX \
	(AFE_PORT_ID_TDM_PORT_RANGE_START + 0x40)
#define AFE_PORT_ID_QUINARY_TDM_RX_1 \
	(AFE_PORT_ID_QUINARY_TDM_RX + 0x02)
#define AFE_PORT_ID_QUINARY_TDM_RX_2 \
	(AFE_PORT_ID_QUINARY_TDM_RX + 0x04)
#define AFE_PORT_ID_QUINARY_TDM_RX_3 \
	(AFE_PORT_ID_QUINARY_TDM_RX + 0x06)
#define AFE_PORT_ID_QUINARY_TDM_RX_4 \
	(AFE_PORT_ID_QUINARY_TDM_RX + 0x08)
#define AFE_PORT_ID_QUINARY_TDM_RX_5 \
	(AFE_PORT_ID_QUINARY_TDM_RX + 0x0A)
#define AFE_PORT_ID_QUINARY_TDM_RX_6 \
	(AFE_PORT_ID_QUINARY_TDM_RX + 0x0C)
#define AFE_PORT_ID_QUINARY_TDM_RX_7 \
	(AFE_PORT_ID_QUINARY_TDM_RX + 0x0E)

#define AFE_PORT_ID_QUINARY_TDM_TX \
	(AFE_PORT_ID_TDM_PORT_RANGE_START + 0x41)
#define AFE_PORT_ID_QUINARY_TDM_TX_1 \
	(AFE_PORT_ID_QUINARY_TDM_TX + 0x02)
#define AFE_PORT_ID_QUINARY_TDM_TX_2 \
	(AFE_PORT_ID_QUINARY_TDM_TX + 0x04)
#define AFE_PORT_ID_QUINARY_TDM_TX_3 \
	(AFE_PORT_ID_QUINARY_TDM_TX + 0x06)
#define AFE_PORT_ID_QUINARY_TDM_TX_4 \
	(AFE_PORT_ID_QUINARY_TDM_TX + 0x08)
#define AFE_PORT_ID_QUINARY_TDM_TX_5 \
	(AFE_PORT_ID_QUINARY_TDM_TX + 0x0A)
#define AFE_PORT_ID_QUINARY_TDM_TX_6 \
	(AFE_PORT_ID_QUINARY_TDM_TX + 0x0C)
#define AFE_PORT_ID_QUINARY_TDM_TX_7 \
	(AFE_PORT_ID_QUINARY_TDM_TX + 0x0E)

/* AFE WSA Codec DMA Rx port 0 */
#define AFE_PORT_ID_WSA_CODEC_DMA_RX_0	0xB000
/* AFE WSA Codec DMA Tx port 0 */
#define AFE_PORT_ID_WSA_CODEC_DMA_TX_0	0xB001
/* AFE WSA Codec DMA Rx port 1 */
#define AFE_PORT_ID_WSA_CODEC_DMA_RX_1	0xB002
/* AFE WSA Codec DMA Tx port 1 */
#define AFE_PORT_ID_WSA_CODEC_DMA_TX_1	0xB003
/* AFE WSA Codec DMA Tx port 2 */
#define AFE_PORT_ID_WSA_CODEC_DMA_TX_2	0xB005
/* AFE VA Codec DMA Tx port 0 */
#define AFE_PORT_ID_VA_CODEC_DMA_TX_0	0xB021
/* AFE VA Codec DMA Tx port 1 */
#define AFE_PORT_ID_VA_CODEC_DMA_TX_1	0xB023
/* AFE VA Codec DMA Tx port 2 */
#define AFE_PORT_ID_VA_CODEC_DMA_TX_2	0xB025
/* AFE Rx Codec DMA Rx port 0 */
#define AFE_PORT_ID_RX_CODEC_DMA_RX_0	0xB030
/* AFE Tx Codec DMA Tx port 0 */
#define AFE_PORT_ID_TX_CODEC_DMA_TX_0	0xB031
/* AFE Rx Codec DMA Rx port 1 */
#define AFE_PORT_ID_RX_CODEC_DMA_RX_1	0xB032
/* AFE Tx Codec DMA Tx port 1 */
#define AFE_PORT_ID_TX_CODEC_DMA_TX_1	0xB033
/* AFE Rx Codec DMA Rx port 2 */
#define AFE_PORT_ID_RX_CODEC_DMA_RX_2	0xB034
/* AFE Tx Codec DMA Tx port 2 */
#define AFE_PORT_ID_TX_CODEC_DMA_TX_2	0xB035
/* AFE Rx Codec DMA Rx port 3 */
#define AFE_PORT_ID_RX_CODEC_DMA_RX_3	0xB036
/* AFE Tx Codec DMA Tx port 3 */
#define AFE_PORT_ID_TX_CODEC_DMA_TX_3	0xB037
/* AFE Rx Codec DMA Rx port 4 */
#define AFE_PORT_ID_RX_CODEC_DMA_RX_4	0xB038
/* AFE Tx Codec DMA Tx port 4 */
#define AFE_PORT_ID_TX_CODEC_DMA_TX_4	0xB039
/* AFE Rx Codec DMA Rx port 5 */
#define AFE_PORT_ID_RX_CODEC_DMA_RX_5	0xB03A
/* AFE Tx Codec DMA Tx port 5 */
#define AFE_PORT_ID_TX_CODEC_DMA_TX_5	0xB03B
/* AFE Rx Codec DMA Rx port 6 */
#define AFE_PORT_ID_RX_CODEC_DMA_RX_6	0xB03C
/* AFE Rx Codec DMA Rx port 7 */
#define AFE_PORT_ID_RX_CODEC_DMA_RX_7	0xB03E

#define Q6AFE_LPASS_MODE_CLK1_VALID 1
#define Q6AFE_LPASS_MODE_CLK2_VALID 2
#define Q6AFE_LPASS_CLK_SRC_INTERNAL 1
#define Q6AFE_LPASS_CLK_ROOT_DEFAULT 0
#define AFE_API_VERSION_TDM_CONFIG              1
#define AFE_API_VERSION_SLOT_MAPPING_CONFIG	1
#define AFE_API_VERSION_CODEC_DMA_CONFIG	1

#define TIMEOUT_MS 3000
#define AFE_CMD_RESP_AVAIL	0
#define AFE_CMD_RESP_NONE	1
#define AFE_CLK_TOKEN		1024

struct q6afe {
	struct apr_device *apr;
	struct device *dev;
	struct q6core_svc_api_info ainfo;
	struct mutex lock;
	struct aprv2_ibasic_rsp_result_t result;
	wait_queue_head_t wait;
	struct list_head port_list;
	spinlock_t port_list_lock;
};

struct afe_port_cmd_device_start {
	u16 port_id;
	u16 reserved;
} __packed;

struct afe_port_cmd_device_stop {
	u16 port_id;
	u16 reserved;
/* Reserved for 32-bit alignment. This field must be set to 0.*/
} __packed;

struct afe_port_param_data_v2 {
	u32 module_id;
	u32 param_id;
	u16 param_size;
	u16 reserved;
} __packed;

struct afe_svc_cmd_set_param {
	uint32_t payload_size;
	uint32_t payload_address_lsw;
	uint32_t payload_address_msw;
	uint32_t mem_map_handle;
} __packed;

struct afe_port_cmd_set_param_v2 {
	u16 port_id;
	u16 payload_size;
	u32 payload_address_lsw;
	u32 payload_address_msw;
	u32 mem_map_handle;
} __packed;

struct afe_param_id_hdmi_multi_chan_audio_cfg {
	u32 hdmi_cfg_minor_version;
	u16 datatype;
	u16 channel_allocation;
	u32 sample_rate;
	u16 bit_width;
	u16 reserved;
} __packed;

struct afe_param_id_slimbus_cfg {
	u32                  sb_cfg_minor_version;
/* Minor version used for tracking the version of the SLIMBUS
 * configuration interface.
 * Supported values: #AFE_API_VERSION_SLIMBUS_CONFIG
 */

	u16                  slimbus_dev_id;
/* SLIMbus hardware device ID, which is required to handle
 * multiple SLIMbus hardware blocks.
 * Supported values: - #AFE_SLIMBUS_DEVICE_1 - #AFE_SLIMBUS_DEVICE_2
 */
	u16                  bit_width;
/* Bit width of the sample.
 * Supported values: 16, 24
 */
	u16                  data_format;
/* Data format supported by the SLIMbus hardware. The default is
 * 0 (#AFE_SB_DATA_FORMAT_NOT_INDICATED), which indicates the
 * hardware does not perform any format conversions before the data
 * transfer.
 */
	u16                  num_channels;
/* Number of channels.
 * Supported values: 1 to #AFE_PORT_MAX_AUDIO_CHAN_CNT
 */
	u8  shared_ch_mapping[AFE_PORT_MAX_AUDIO_CHAN_CNT];
/* Mapping of shared channel IDs (128 to 255) to which the
 * master port is to be connected.
 * Shared_channel_mapping[i] represents the shared channel assigned
 * for audio channel i in multichannel audio data.
 */
	u32              sample_rate;
/* Sampling rate of the port.
 * Supported values:
 * - #AFE_PORT_SAMPLE_RATE_8K
 * - #AFE_PORT_SAMPLE_RATE_16K
 * - #AFE_PORT_SAMPLE_RATE_48K
 * - #AFE_PORT_SAMPLE_RATE_96K
 * - #AFE_PORT_SAMPLE_RATE_192K
 */
} __packed;

struct afe_clk_cfg {
	u32                  i2s_cfg_minor_version;
	u32                  clk_val1;
	u32                  clk_val2;
	u16                  clk_src;
	u16                  clk_root;
	u16                  clk_set_mode;
	u16                  reserved;
} __packed;

struct afe_digital_clk_cfg {
	u32                  i2s_cfg_minor_version;
	u32                  clk_val;
	u16                  clk_root;
	u16                  reserved;
} __packed;

struct afe_param_id_i2s_cfg {
	u32	i2s_cfg_minor_version;
	u16	bit_width;
	u16	channel_mode;
	u16	mono_stereo;
	u16	ws_src;
	u32	sample_rate;
	u16	data_format;
	u16	reserved;
} __packed;

struct afe_param_id_tdm_cfg {
	u32	tdm_cfg_minor_version;
	u32	num_channels;
	u32	sample_rate;
	u32	bit_width;
	u16	data_format;
	u16	sync_mode;
	u16	sync_src;
	u16	nslots_per_frame;
	u16	ctrl_data_out_enable;
	u16	ctrl_invert_sync_pulse;
	u16	ctrl_sync_data_delay;
	u16	slot_width;
	u32	slot_mask;
} __packed;

struct afe_param_id_cdc_dma_cfg {
	u32	cdc_dma_cfg_minor_version;
	u32	sample_rate;
	u16	bit_width;
	u16	data_format;
	u16	num_channels;
	u16	active_channels_mask;
} __packed;

struct afe_param_id_usb_cfg {
/* Minor version used for tracking USB audio device configuration.
 * Supported values: AFE_API_MINOR_VERSION_USB_AUDIO_CONFIG
 */
	u32	cfg_minor_version;
/* Sampling rate of the port.
 * Supported values:
 * - AFE_PORT_SAMPLE_RATE_8K
 * - AFE_PORT_SAMPLE_RATE_11025
 * - AFE_PORT_SAMPLE_RATE_12K
 * - AFE_PORT_SAMPLE_RATE_16K
 * - AFE_PORT_SAMPLE_RATE_22050
 * - AFE_PORT_SAMPLE_RATE_24K
 * - AFE_PORT_SAMPLE_RATE_32K
 * - AFE_PORT_SAMPLE_RATE_44P1K
 * - AFE_PORT_SAMPLE_RATE_48K
 * - AFE_PORT_SAMPLE_RATE_96K
 * - AFE_PORT_SAMPLE_RATE_192K
 */
	u32	sample_rate;
/* Bit width of the sample.
 * Supported values: 16, 24
 */
	u16	bit_width;
/* Number of channels.
 * Supported values: 1 and 2
 */
	u16	num_channels;
/* Data format supported by the USB. The supported value is
 * 0 (#AFE_USB_AUDIO_DATA_FORMAT_LINEAR_PCM).
 */
	u16	data_format;
/* this field must be 0 */
	u16	reserved;
/* device token of actual end USB audio device */
	u32	dev_token;
/* endianness of this interface */
	u32	endian;
/* service interval */
	u32	service_interval;
} __packed;

/**
 * struct afe_param_id_usb_audio_dev_params
 * @cfg_minor_version: Minor version used for tracking USB audio device
 * configuration.
 * Supported values:
 *     AFE_API_MINOR_VERSION_USB_AUDIO_CONFIG
 * @dev_token: device token of actual end USB audio device
 **/
struct afe_param_id_usb_audio_dev_params {
	u32	cfg_minor_version;
	u32	dev_token;
} __packed;

/**
 * struct afe_param_id_usb_audio_dev_lpcm_fmt
 * @cfg_minor_version: Minor version used for tracking USB audio device
 * configuration.
 * Supported values:
 *     AFE_API_MINOR_VERSION_USB_AUDIO_CONFIG
 * @endian: endianness of this interface
 **/
struct afe_param_id_usb_audio_dev_lpcm_fmt {
	u32	cfg_minor_version;
	u32	endian;
} __packed;

#define AFE_PARAM_ID_USB_AUDIO_SVC_INTERVAL     0x000102B7

/**
 * struct afe_param_id_usb_audio_svc_interval
 * @cfg_minor_version: Minor version used for tracking USB audio device
 * configuration.
 * Supported values:
 *     AFE_API_MINOR_VERSION_USB_AUDIO_CONFIG
 * @svc_interval: service interval
 **/
struct afe_param_id_usb_audio_svc_interval {
	u32	cfg_minor_version;
	u32	svc_interval;
} __packed;

union afe_port_config {
	struct afe_param_id_hdmi_multi_chan_audio_cfg hdmi_multi_ch;
	struct afe_param_id_slimbus_cfg           slim_cfg;
	struct afe_param_id_i2s_cfg	i2s_cfg;
	struct afe_param_id_tdm_cfg	tdm_cfg;
	struct afe_param_id_cdc_dma_cfg	dma_cfg;
	struct afe_param_id_usb_cfg usb_cfg;
} __packed;


struct afe_clk_set {
	uint32_t clk_set_minor_version;
	uint32_t clk_id;
	uint32_t clk_freq_in_hz;
	uint16_t clk_attri;
	uint16_t clk_root;
	uint32_t enable;
};

struct afe_param_id_slot_mapping_cfg {
	u32	minor_version;
	u16	num_channels;
	u16	bitwidth;
	u32	data_align_type;
	u16	ch_mapping[AFE_PORT_MAX_AUDIO_CHAN_CNT];
} __packed;

struct q6afe_port {
	wait_queue_head_t wait;
	union afe_port_config port_cfg;
	struct afe_param_id_slot_mapping_cfg *scfg;
	struct aprv2_ibasic_rsp_result_t result;
	int token;
	int id;
	int cfg_type;
	struct q6afe *afe;
	struct kref refcount;
	struct list_head node;
};

struct afe_cmd_remote_lpass_core_hw_vote_request {
	uint32_t  hw_block_id;
	char client_name[8];
} __packed;

struct afe_cmd_remote_lpass_core_hw_devote_request {
	uint32_t  hw_block_id;
	uint32_t client_handle;
} __packed;



struct afe_port_map {
	int port_id;
	int token;
	int is_rx;
	int is_dig_pcm;
};

/*
 * Mapping between Virtual Port IDs to DSP AFE Port ID
 * On B Family SoCs DSP Port IDs are consistent across multiple SoCs
 * on A Family SoCs DSP port IDs are same as virtual Port IDs.
 */

static struct afe_port_map port_maps[AFE_PORT_MAX] = {
	[HDMI_RX] = { AFE_PORT_ID_MULTICHAN_HDMI_RX, HDMI_RX, 1, 1},
	[SLIMBUS_0_RX] = { AFE_PORT_ID_SLIMBUS_MULTI_CHAN_0_RX,
				SLIMBUS_0_RX, 1, 1},
	[SLIMBUS_1_RX] = { AFE_PORT_ID_SLIMBUS_MULTI_CHAN_1_RX,
				SLIMBUS_1_RX, 1, 1},
	[SLIMBUS_2_RX] = { AFE_PORT_ID_SLIMBUS_MULTI_CHAN_2_RX,
				SLIMBUS_2_RX, 1, 1},
	[SLIMBUS_3_RX] = { AFE_PORT_ID_SLIMBUS_MULTI_CHAN_3_RX,
				SLIMBUS_3_RX, 1, 1},
	[SLIMBUS_4_RX] = { AFE_PORT_ID_SLIMBUS_MULTI_CHAN_4_RX,
				SLIMBUS_4_RX, 1, 1},
	[SLIMBUS_5_RX] = { AFE_PORT_ID_SLIMBUS_MULTI_CHAN_5_RX,
				SLIMBUS_5_RX, 1, 1},
	[SLIMBUS_6_RX] = { AFE_PORT_ID_SLIMBUS_MULTI_CHAN_6_RX,
				SLIMBUS_6_RX, 1, 1},
	[SLIMBUS_0_TX] = { AFE_PORT_ID_SLIMBUS_MULTI_CHAN_0_TX,
				SLIMBUS_0_TX, 0, 1},
	[SLIMBUS_1_TX] = { AFE_PORT_ID_SLIMBUS_MULTI_CHAN_1_TX,
				SLIMBUS_1_TX, 0, 1},
	[SLIMBUS_2_TX] = { AFE_PORT_ID_SLIMBUS_MULTI_CHAN_2_TX,
				SLIMBUS_2_TX, 0, 1},
	[SLIMBUS_3_TX] = { AFE_PORT_ID_SLIMBUS_MULTI_CHAN_3_TX,
				SLIMBUS_3_TX, 0, 1},
	[SLIMBUS_4_TX] = { AFE_PORT_ID_SLIMBUS_MULTI_CHAN_4_TX,
				SLIMBUS_4_TX, 0, 1},
	[SLIMBUS_5_TX] = { AFE_PORT_ID_SLIMBUS_MULTI_CHAN_5_TX,
				SLIMBUS_5_TX, 0, 1},
	[SLIMBUS_6_TX] = { AFE_PORT_ID_SLIMBUS_MULTI_CHAN_6_TX,
				SLIMBUS_6_TX, 0, 1},
	[PRIMARY_MI2S_RX] = { AFE_PORT_ID_PRIMARY_MI2S_RX,
				PRIMARY_MI2S_RX, 1, 1},
	[PRIMARY_MI2S_TX] = { AFE_PORT_ID_PRIMARY_MI2S_TX,
				PRIMARY_MI2S_RX, 0, 1},
	[SECONDARY_MI2S_RX] = { AFE_PORT_ID_SECONDARY_MI2S_RX,
				SECONDARY_MI2S_RX, 1, 1},
	[SECONDARY_MI2S_TX] = { AFE_PORT_ID_SECONDARY_MI2S_TX,
				SECONDARY_MI2S_TX, 0, 1},
	[TERTIARY_MI2S_RX] = { AFE_PORT_ID_TERTIARY_MI2S_RX,
				TERTIARY_MI2S_RX, 1, 1},
	[TERTIARY_MI2S_TX] = { AFE_PORT_ID_TERTIARY_MI2S_TX,
				TERTIARY_MI2S_TX, 0, 1},
	[QUATERNARY_MI2S_RX] = { AFE_PORT_ID_QUATERNARY_MI2S_RX,
				QUATERNARY_MI2S_RX, 1, 1},
	[QUATERNARY_MI2S_TX] = { AFE_PORT_ID_QUATERNARY_MI2S_TX,
				QUATERNARY_MI2S_TX, 0, 1},
	[QUINARY_MI2S_RX]  =  { AFE_PORT_ID_QUINARY_MI2S_RX,
				QUINARY_MI2S_RX, 1, 1},
	[QUINARY_MI2S_TX] =   { AFE_PORT_ID_QUINARY_MI2S_TX,
				QUINARY_MI2S_TX, 0, 1},
	[PRIMARY_TDM_RX_0] =  { AFE_PORT_ID_PRIMARY_TDM_RX,
				PRIMARY_TDM_RX_0, 1, 1},
	[PRIMARY_TDM_TX_0] =  { AFE_PORT_ID_PRIMARY_TDM_TX,
				PRIMARY_TDM_TX_0, 0, 1},
	[PRIMARY_TDM_RX_1] =  { AFE_PORT_ID_PRIMARY_TDM_RX_1,
				PRIMARY_TDM_RX_1, 1, 1},
	[PRIMARY_TDM_TX_1] =  { AFE_PORT_ID_PRIMARY_TDM_TX_1,
				PRIMARY_TDM_TX_1, 0, 1},
	[PRIMARY_TDM_RX_2] =  { AFE_PORT_ID_PRIMARY_TDM_RX_2,
				PRIMARY_TDM_RX_2, 1, 1},
	[PRIMARY_TDM_TX_2] =  { AFE_PORT_ID_PRIMARY_TDM_TX_2,
				PRIMARY_TDM_TX_2, 0, 1},
	[PRIMARY_TDM_RX_3] =  { AFE_PORT_ID_PRIMARY_TDM_RX_3,
				PRIMARY_TDM_RX_3, 1, 1},
	[PRIMARY_TDM_TX_3] =  { AFE_PORT_ID_PRIMARY_TDM_TX_3,
				PRIMARY_TDM_TX_3, 0, 1},
	[PRIMARY_TDM_RX_4] =  { AFE_PORT_ID_PRIMARY_TDM_RX_4,
				PRIMARY_TDM_RX_4, 1, 1},
	[PRIMARY_TDM_TX_4] =  { AFE_PORT_ID_PRIMARY_TDM_TX_4,
				PRIMARY_TDM_TX_4, 0, 1},
	[PRIMARY_TDM_RX_5] =  { AFE_PORT_ID_PRIMARY_TDM_RX_5,
				PRIMARY_TDM_RX_5, 1, 1},
	[PRIMARY_TDM_TX_5] =  { AFE_PORT_ID_PRIMARY_TDM_TX_5,
				PRIMARY_TDM_TX_5, 0, 1},
	[PRIMARY_TDM_RX_6] =  { AFE_PORT_ID_PRIMARY_TDM_RX_6,
				PRIMARY_TDM_RX_6, 1, 1},
	[PRIMARY_TDM_TX_6] =  { AFE_PORT_ID_PRIMARY_TDM_TX_6,
				PRIMARY_TDM_TX_6, 0, 1},
	[PRIMARY_TDM_RX_7] =  { AFE_PORT_ID_PRIMARY_TDM_RX_7,
				PRIMARY_TDM_RX_7, 1, 1},
	[PRIMARY_TDM_TX_7] =  { AFE_PORT_ID_PRIMARY_TDM_TX_7,
				PRIMARY_TDM_TX_7, 0, 1},
	[SECONDARY_TDM_RX_0] =  { AFE_PORT_ID_SECONDARY_TDM_RX,
				SECONDARY_TDM_RX_0, 1, 1},
	[SECONDARY_TDM_TX_0] =  { AFE_PORT_ID_SECONDARY_TDM_TX,
				SECONDARY_TDM_TX_0, 0, 1},
	[SECONDARY_TDM_RX_1] =  { AFE_PORT_ID_SECONDARY_TDM_RX_1,
				SECONDARY_TDM_RX_1, 1, 1},
	[SECONDARY_TDM_TX_1] =  { AFE_PORT_ID_SECONDARY_TDM_TX_1,
				SECONDARY_TDM_TX_1, 0, 1},
	[SECONDARY_TDM_RX_2] =  { AFE_PORT_ID_SECONDARY_TDM_RX_2,
				SECONDARY_TDM_RX_2, 1, 1},
	[SECONDARY_TDM_TX_2] =  { AFE_PORT_ID_SECONDARY_TDM_TX_2,
				SECONDARY_TDM_TX_2, 0, 1},
	[SECONDARY_TDM_RX_3] =  { AFE_PORT_ID_SECONDARY_TDM_RX_3,
				SECONDARY_TDM_RX_3, 1, 1},
	[SECONDARY_TDM_TX_3] =  { AFE_PORT_ID_SECONDARY_TDM_TX_3,
				SECONDARY_TDM_TX_3, 0, 1},
	[SECONDARY_TDM_RX_4] =  { AFE_PORT_ID_SECONDARY_TDM_RX_4,
				SECONDARY_TDM_RX_4, 1, 1},
	[SECONDARY_TDM_TX_4] =  { AFE_PORT_ID_SECONDARY_TDM_TX_4,
				SECONDARY_TDM_TX_4, 0, 1},
	[SECONDARY_TDM_RX_5] =  { AFE_PORT_ID_SECONDARY_TDM_RX_5,
				SECONDARY_TDM_RX_5, 1, 1},
	[SECONDARY_TDM_TX_5] =  { AFE_PORT_ID_SECONDARY_TDM_TX_5,
				SECONDARY_TDM_TX_5, 0, 1},
	[SECONDARY_TDM_RX_6] =  { AFE_PORT_ID_SECONDARY_TDM_RX_6,
				SECONDARY_TDM_RX_6, 1, 1},
	[SECONDARY_TDM_TX_6] =  { AFE_PORT_ID_SECONDARY_TDM_TX_6,
				SECONDARY_TDM_TX_6, 0, 1},
	[SECONDARY_TDM_RX_7] =  { AFE_PORT_ID_SECONDARY_TDM_RX_7,
				SECONDARY_TDM_RX_7, 1, 1},
	[SECONDARY_TDM_TX_7] =  { AFE_PORT_ID_SECONDARY_TDM_TX_7,
				SECONDARY_TDM_TX_7, 0, 1},
	[TERTIARY_TDM_RX_0] =  { AFE_PORT_ID_TERTIARY_TDM_RX,
				TERTIARY_TDM_RX_0, 1, 1},
	[TERTIARY_TDM_TX_0] =  { AFE_PORT_ID_TERTIARY_TDM_TX,
				TERTIARY_TDM_TX_0, 0, 1},
	[TERTIARY_TDM_RX_1] =  { AFE_PORT_ID_TERTIARY_TDM_RX_1,
				TERTIARY_TDM_RX_1, 1, 1},
	[TERTIARY_TDM_TX_1] =  { AFE_PORT_ID_TERTIARY_TDM_TX_1,
				TERTIARY_TDM_TX_1, 0, 1},
	[TERTIARY_TDM_RX_2] =  { AFE_PORT_ID_TERTIARY_TDM_RX_2,
				TERTIARY_TDM_RX_2, 1, 1},
	[TERTIARY_TDM_TX_2] =  { AFE_PORT_ID_TERTIARY_TDM_TX_2,
				TERTIARY_TDM_TX_2, 0, 1},
	[TERTIARY_TDM_RX_3] =  { AFE_PORT_ID_TERTIARY_TDM_RX_3,
				TERTIARY_TDM_RX_3, 1, 1},
	[TERTIARY_TDM_TX_3] =  { AFE_PORT_ID_TERTIARY_TDM_TX_3,
				TERTIARY_TDM_TX_3, 0, 1},
	[TERTIARY_TDM_RX_4] =  { AFE_PORT_ID_TERTIARY_TDM_RX_4,
				TERTIARY_TDM_RX_4, 1, 1},
	[TERTIARY_TDM_TX_4] =  { AFE_PORT_ID_TERTIARY_TDM_TX_4,
				TERTIARY_TDM_TX_4, 0, 1},
	[TERTIARY_TDM_RX_5] =  { AFE_PORT_ID_TERTIARY_TDM_RX_5,
				TERTIARY_TDM_RX_5, 1, 1},
	[TERTIARY_TDM_TX_5] =  { AFE_PORT_ID_TERTIARY_TDM_TX_5,
				TERTIARY_TDM_TX_5, 0, 1},
	[TERTIARY_TDM_RX_6] =  { AFE_PORT_ID_TERTIARY_TDM_RX_6,
				TERTIARY_TDM_RX_6, 1, 1},
	[TERTIARY_TDM_TX_6] =  { AFE_PORT_ID_TERTIARY_TDM_TX_6,
				TERTIARY_TDM_TX_6, 0, 1},
	[TERTIARY_TDM_RX_7] =  { AFE_PORT_ID_TERTIARY_TDM_RX_7,
				TERTIARY_TDM_RX_7, 1, 1},
	[TERTIARY_TDM_TX_7] =  { AFE_PORT_ID_TERTIARY_TDM_TX_7,
				TERTIARY_TDM_TX_7, 0, 1},
	[QUATERNARY_TDM_RX_0] =  { AFE_PORT_ID_QUATERNARY_TDM_RX,
				QUATERNARY_TDM_RX_0, 1, 1},
	[QUATERNARY_TDM_TX_0] =  { AFE_PORT_ID_QUATERNARY_TDM_TX,
				QUATERNARY_TDM_TX_0, 0, 1},
	[QUATERNARY_TDM_RX_1] =  { AFE_PORT_ID_QUATERNARY_TDM_RX_1,
				QUATERNARY_TDM_RX_1, 1, 1},
	[QUATERNARY_TDM_TX_1] =  { AFE_PORT_ID_QUATERNARY_TDM_TX_1,
				QUATERNARY_TDM_TX_1, 0, 1},
	[QUATERNARY_TDM_RX_2] =  { AFE_PORT_ID_QUATERNARY_TDM_RX_2,
				QUATERNARY_TDM_RX_2, 1, 1},
	[QUATERNARY_TDM_TX_2] =  { AFE_PORT_ID_QUATERNARY_TDM_TX_2,
				QUATERNARY_TDM_TX_2, 0, 1},
	[QUATERNARY_TDM_RX_3] =  { AFE_PORT_ID_QUATERNARY_TDM_RX_3,
				QUATERNARY_TDM_RX_3, 1, 1},
	[QUATERNARY_TDM_TX_3] =  { AFE_PORT_ID_QUATERNARY_TDM_TX_3,
				QUATERNARY_TDM_TX_3, 0, 1},
	[QUATERNARY_TDM_RX_4] =  { AFE_PORT_ID_QUATERNARY_TDM_RX_4,
				QUATERNARY_TDM_RX_4, 1, 1},
	[QUATERNARY_TDM_TX_4] =  { AFE_PORT_ID_QUATERNARY_TDM_TX_4,
				QUATERNARY_TDM_TX_4, 0, 1},
	[QUATERNARY_TDM_RX_5] =  { AFE_PORT_ID_QUATERNARY_TDM_RX_5,
				QUATERNARY_TDM_RX_5, 1, 1},
	[QUATERNARY_TDM_TX_5] =  { AFE_PORT_ID_QUATERNARY_TDM_TX_5,
				QUATERNARY_TDM_TX_5, 0, 1},
	[QUATERNARY_TDM_RX_6] =  { AFE_PORT_ID_QUATERNARY_TDM_RX_6,
				QUATERNARY_TDM_RX_6, 1, 1},
	[QUATERNARY_TDM_TX_6] =  { AFE_PORT_ID_QUATERNARY_TDM_TX_6,
				QUATERNARY_TDM_TX_6, 0, 1},
	[QUATERNARY_TDM_RX_7] =  { AFE_PORT_ID_QUATERNARY_TDM_RX_7,
				QUATERNARY_TDM_RX_7, 1, 1},
	[QUATERNARY_TDM_TX_7] =  { AFE_PORT_ID_QUATERNARY_TDM_TX_7,
				QUATERNARY_TDM_TX_7, 0, 1},
	[QUINARY_TDM_RX_0] =  { AFE_PORT_ID_QUINARY_TDM_RX,
				QUINARY_TDM_RX_0, 1, 1},
	[QUINARY_TDM_TX_0] =  { AFE_PORT_ID_QUINARY_TDM_TX,
				QUINARY_TDM_TX_0, 0, 1},
	[QUINARY_TDM_RX_1] =  { AFE_PORT_ID_QUINARY_TDM_RX_1,
				QUINARY_TDM_RX_1, 1, 1},
	[QUINARY_TDM_TX_1] =  { AFE_PORT_ID_QUINARY_TDM_TX_1,
				QUINARY_TDM_TX_1, 0, 1},
	[QUINARY_TDM_RX_2] =  { AFE_PORT_ID_QUINARY_TDM_RX_2,
				QUINARY_TDM_RX_2, 1, 1},
	[QUINARY_TDM_TX_2] =  { AFE_PORT_ID_QUINARY_TDM_TX_2,
				QUINARY_TDM_TX_2, 0, 1},
	[QUINARY_TDM_RX_3] =  { AFE_PORT_ID_QUINARY_TDM_RX_3,
				QUINARY_TDM_RX_3, 1, 1},
	[QUINARY_TDM_TX_3] =  { AFE_PORT_ID_QUINARY_TDM_TX_3,
				QUINARY_TDM_TX_3, 0, 1},
	[QUINARY_TDM_RX_4] =  { AFE_PORT_ID_QUINARY_TDM_RX_4,
				QUINARY_TDM_RX_4, 1, 1},
	[QUINARY_TDM_TX_4] =  { AFE_PORT_ID_QUINARY_TDM_TX_4,
				QUINARY_TDM_TX_4, 0, 1},
	[QUINARY_TDM_RX_5] =  { AFE_PORT_ID_QUINARY_TDM_RX_5,
				QUINARY_TDM_RX_5, 1, 1},
	[QUINARY_TDM_TX_5] =  { AFE_PORT_ID_QUINARY_TDM_TX_5,
				QUINARY_TDM_TX_5, 0, 1},
	[QUINARY_TDM_RX_6] =  { AFE_PORT_ID_QUINARY_TDM_RX_6,
				QUINARY_TDM_RX_6, 1, 1},
	[QUINARY_TDM_TX_6] =  { AFE_PORT_ID_QUINARY_TDM_TX_6,
				QUINARY_TDM_TX_6, 0, 1},
	[QUINARY_TDM_RX_7] =  { AFE_PORT_ID_QUINARY_TDM_RX_7,
				QUINARY_TDM_RX_7, 1, 1},
	[QUINARY_TDM_TX_7] =  { AFE_PORT_ID_QUINARY_TDM_TX_7,
				QUINARY_TDM_TX_7, 0, 1},
	[DISPLAY_PORT_RX] = { AFE_PORT_ID_HDMI_OVER_DP_RX,
				DISPLAY_PORT_RX, 1, 1},
	[WSA_CODEC_DMA_RX_0] = { AFE_PORT_ID_WSA_CODEC_DMA_RX_0,
				WSA_CODEC_DMA_RX_0, 1, 1},
	[WSA_CODEC_DMA_TX_0] = { AFE_PORT_ID_WSA_CODEC_DMA_TX_0,
				WSA_CODEC_DMA_TX_0, 0, 1},
	[WSA_CODEC_DMA_RX_1] = { AFE_PORT_ID_WSA_CODEC_DMA_RX_1,
				WSA_CODEC_DMA_RX_1, 1, 1},
	[WSA_CODEC_DMA_TX_1] = { AFE_PORT_ID_WSA_CODEC_DMA_TX_1,
				WSA_CODEC_DMA_TX_1, 0, 1},
	[WSA_CODEC_DMA_TX_2] = { AFE_PORT_ID_WSA_CODEC_DMA_TX_2,
				WSA_CODEC_DMA_TX_2, 0, 1},
	[VA_CODEC_DMA_TX_0] = { AFE_PORT_ID_VA_CODEC_DMA_TX_0,
				VA_CODEC_DMA_TX_0, 0, 1},
	[VA_CODEC_DMA_TX_1] = { AFE_PORT_ID_VA_CODEC_DMA_TX_1,
				VA_CODEC_DMA_TX_1, 0, 1},
	[VA_CODEC_DMA_TX_2] = { AFE_PORT_ID_VA_CODEC_DMA_TX_2,
				VA_CODEC_DMA_TX_2, 0, 1},
	[RX_CODEC_DMA_RX_0] = { AFE_PORT_ID_RX_CODEC_DMA_RX_0,
				RX_CODEC_DMA_RX_0, 1, 1},
	[TX_CODEC_DMA_TX_0] = { AFE_PORT_ID_TX_CODEC_DMA_TX_0,
				TX_CODEC_DMA_TX_0, 0, 1},
	[RX_CODEC_DMA_RX_1] = { AFE_PORT_ID_RX_CODEC_DMA_RX_1,
				RX_CODEC_DMA_RX_1, 1, 1},
	[TX_CODEC_DMA_TX_1] = { AFE_PORT_ID_TX_CODEC_DMA_TX_1,
				TX_CODEC_DMA_TX_1, 0, 1},
	[RX_CODEC_DMA_RX_2] = { AFE_PORT_ID_RX_CODEC_DMA_RX_2,
				RX_CODEC_DMA_RX_2, 1, 1},
	[TX_CODEC_DMA_TX_2] = { AFE_PORT_ID_TX_CODEC_DMA_TX_2,
				TX_CODEC_DMA_TX_2, 0, 1},
	[RX_CODEC_DMA_RX_3] = { AFE_PORT_ID_RX_CODEC_DMA_RX_3,
				RX_CODEC_DMA_RX_3, 1, 1},
	[TX_CODEC_DMA_TX_3] = { AFE_PORT_ID_TX_CODEC_DMA_TX_3,
				TX_CODEC_DMA_TX_3, 0, 1},
	[RX_CODEC_DMA_RX_4] = { AFE_PORT_ID_RX_CODEC_DMA_RX_4,
				RX_CODEC_DMA_RX_4, 1, 1},
	[TX_CODEC_DMA_TX_4] = { AFE_PORT_ID_TX_CODEC_DMA_TX_4,
				TX_CODEC_DMA_TX_4, 0, 1},
	[RX_CODEC_DMA_RX_5] = { AFE_PORT_ID_RX_CODEC_DMA_RX_5,
				RX_CODEC_DMA_RX_5, 1, 1},
	[TX_CODEC_DMA_TX_5] = { AFE_PORT_ID_TX_CODEC_DMA_TX_5,
				TX_CODEC_DMA_TX_5, 0, 1},
	[RX_CODEC_DMA_RX_6] = { AFE_PORT_ID_RX_CODEC_DMA_RX_6,
				RX_CODEC_DMA_RX_6, 1, 1},
	[RX_CODEC_DMA_RX_7] = { AFE_PORT_ID_RX_CODEC_DMA_RX_7,
				RX_CODEC_DMA_RX_7, 1, 1},
	[USB_RX] = { AFE_PORT_ID_USB_RX, USB_RX, 1, 1},
};

static void q6afe_port_free(struct kref *ref)
{
	struct q6afe_port *port;
	struct q6afe *afe;
	unsigned long flags;

	port = container_of(ref, struct q6afe_port, refcount);
	afe = port->afe;
	spin_lock_irqsave(&afe->port_list_lock, flags);
	list_del(&port->node);
	spin_unlock_irqrestore(&afe->port_list_lock, flags);
	kfree(port->scfg);
	kfree(port);
}

static struct q6afe_port *q6afe_find_port(struct q6afe *afe, int token)
{
	struct q6afe_port *p;
	struct q6afe_port *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&afe->port_list_lock, flags);
	list_for_each_entry(p, &afe->port_list, node)
		if (p->token == token) {
			ret = p;
			kref_get(&p->refcount);
			break;
		}

	spin_unlock_irqrestore(&afe->port_list_lock, flags);
	return ret;
}

static int q6afe_callback(struct apr_device *adev, struct apr_resp_pkt *data)
{
	struct q6afe *afe = dev_get_drvdata(&adev->dev);
	struct aprv2_ibasic_rsp_result_t *res;
	struct apr_hdr *hdr = &data->hdr;
	struct q6afe_port *port;

	if (!data->payload_size)
		return 0;

	res = data->payload;
	switch (hdr->opcode) {
	case APR_BASIC_RSP_RESULT: {
		if (res->status) {
			dev_err(afe->dev, "cmd = 0x%x returned error = 0x%x\n",
				res->opcode, res->status);
		}
		switch (res->opcode) {
		case AFE_PORT_CMD_SET_PARAM_V2:
		case AFE_PORT_CMD_DEVICE_STOP:
		case AFE_PORT_CMD_DEVICE_START:
		case AFE_SVC_CMD_SET_PARAM:
			port = q6afe_find_port(afe, hdr->token);
			if (port) {
				port->result = *res;
				wake_up(&port->wait);
				kref_put(&port->refcount, q6afe_port_free);
			} else if (hdr->token == AFE_CLK_TOKEN) {
				afe->result = *res;
				wake_up(&afe->wait);
			}
			break;
		default:
			dev_err(afe->dev, "Unknown cmd 0x%x\n",	res->opcode);
			break;
		}
	}
		break;
	case AFE_CMD_RSP_REMOTE_LPASS_CORE_HW_VOTE_REQUEST:
		afe->result.opcode = hdr->opcode;
		afe->result.status = res->status;
		wake_up(&afe->wait);
		break;
	default:
		break;
	}

	return 0;
}

/**
 * q6afe_get_port_id() - Get port id from a given port index
 *
 * @index: port index
 *
 * Return: Will be an negative on error or valid port_id on success
 */
int q6afe_get_port_id(int index)
{
	if (index < 0 || index >= AFE_PORT_MAX)
		return -EINVAL;

	return port_maps[index].port_id;
}
EXPORT_SYMBOL_GPL(q6afe_get_port_id);

static int afe_apr_send_pkt(struct q6afe *afe, struct apr_pkt *pkt,
			    struct q6afe_port *port, uint32_t rsp_opcode)
{
	wait_queue_head_t *wait;
	struct aprv2_ibasic_rsp_result_t *result;
	int ret;

	mutex_lock(&afe->lock);
	if (port) {
		wait = &port->wait;
		result = &port->result;
	} else {
		result = &afe->result;
		wait = &afe->wait;
	}

	result->opcode = 0;
	result->status = 0;

	ret = apr_send_pkt(afe->apr, pkt);
	if (ret < 0) {
		dev_err(afe->dev, "packet not transmitted (%d)\n", ret);
		ret = -EINVAL;
		goto err;
	}

	ret = wait_event_timeout(*wait, (result->opcode == rsp_opcode),
				 msecs_to_jiffies(TIMEOUT_MS));
	if (!ret) {
		ret = -ETIMEDOUT;
	} else if (result->status > 0) {
		dev_err(afe->dev, "DSP returned error[%x]\n",
			result->status);
		ret = -EINVAL;
	} else {
		ret = 0;
	}

err:
	mutex_unlock(&afe->lock);

	return ret;
}

static int q6afe_set_param(struct q6afe *afe, struct q6afe_port *port,
			   void *data, int param_id, int module_id, int psize,
			   int token)
{
	struct afe_svc_cmd_set_param *param;
	struct afe_port_param_data_v2 *pdata;
	struct apr_pkt *pkt;
	int ret, pkt_size;
	void *p, *pl;

	pkt_size = APR_HDR_SIZE + sizeof(*param) + sizeof(*pdata) + psize;
	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	param = p + APR_HDR_SIZE;
	pdata = p + APR_HDR_SIZE + sizeof(*param);
	pl = p + APR_HDR_SIZE + sizeof(*param) + sizeof(*pdata);
	memcpy(pl, data, psize);

	pkt->hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
					   APR_HDR_LEN(APR_HDR_SIZE),
					   APR_PKT_VER);
	pkt->hdr.pkt_size = pkt_size;
	pkt->hdr.src_port = 0;
	pkt->hdr.dest_port = 0;
	pkt->hdr.token = token;
	pkt->hdr.opcode = AFE_SVC_CMD_SET_PARAM;

	param->payload_size = sizeof(*pdata) + psize;
	param->payload_address_lsw = 0x00;
	param->payload_address_msw = 0x00;
	param->mem_map_handle = 0x00;
	pdata->module_id = module_id;
	pdata->param_id = param_id;
	pdata->param_size = psize;

	ret = afe_apr_send_pkt(afe, pkt, port, AFE_SVC_CMD_SET_PARAM);
	if (ret)
		dev_err(afe->dev, "AFE set params failed %d\n", ret);

	kfree(pkt);
	return ret;
}

static int q6afe_port_set_param(struct q6afe_port *port, void *data,
				int param_id, int module_id, int psize)
{
	return q6afe_set_param(port->afe, port, data, param_id, module_id,
			       psize, port->token);
}

static int q6afe_port_set_param_v2(struct q6afe_port *port, void *data,
				   int param_id, int module_id, int psize)
{
	struct afe_port_cmd_set_param_v2 *param;
	struct afe_port_param_data_v2 *pdata;
	struct q6afe *afe = port->afe;
	struct apr_pkt *pkt;
	u16 port_id = port->id;
	int ret, pkt_size;
	void *p, *pl;

	pkt_size = APR_HDR_SIZE + sizeof(*param) + sizeof(*pdata) + psize;
	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	param = p + APR_HDR_SIZE;
	pdata = p + APR_HDR_SIZE + sizeof(*param);
	pl = p + APR_HDR_SIZE + sizeof(*param) + sizeof(*pdata);
	memcpy(pl, data, psize);

	pkt->hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
					   APR_HDR_LEN(APR_HDR_SIZE),
					   APR_PKT_VER);
	pkt->hdr.pkt_size = pkt_size;
	pkt->hdr.src_port = 0;
	pkt->hdr.dest_port = 0;
	pkt->hdr.token = port->token;
	pkt->hdr.opcode = AFE_PORT_CMD_SET_PARAM_V2;

	param->port_id = port_id;
	param->payload_size = sizeof(*pdata) + psize;
	param->payload_address_lsw = 0x00;
	param->payload_address_msw = 0x00;
	param->mem_map_handle = 0x00;
	pdata->module_id = module_id;
	pdata->param_id = param_id;
	pdata->param_size = psize;

	ret = afe_apr_send_pkt(afe, pkt, port, AFE_PORT_CMD_SET_PARAM_V2);
	if (ret)
		dev_err(afe->dev, "AFE enable for port 0x%x failed %d\n",
		       port_id, ret);

	kfree(pkt);
	return ret;
}

static int q6afe_port_set_lpass_clock(struct q6afe_port *port,
				 struct afe_clk_cfg *cfg)
{
	return q6afe_port_set_param_v2(port, cfg,
				       AFE_PARAM_ID_LPAIF_CLK_CONFIG,
				       AFE_MODULE_AUDIO_DEV_INTERFACE,
				       sizeof(*cfg));
}

static int q6afe_set_lpass_clock_v2(struct q6afe_port *port,
				 struct afe_clk_set *cfg)
{
	return q6afe_port_set_param(port, cfg, AFE_PARAM_ID_CLOCK_SET,
				    AFE_MODULE_CLOCK_SET, sizeof(*cfg));
}

static int q6afe_set_digital_codec_core_clock(struct q6afe_port *port,
					      struct afe_digital_clk_cfg *cfg)
{
	return q6afe_port_set_param_v2(port, cfg,
				       AFE_PARAM_ID_INT_DIGITAL_CDC_CLK_CONFIG,
				       AFE_MODULE_AUDIO_DEV_INTERFACE,
				       sizeof(*cfg));
}

int q6afe_set_lpass_clock(struct device *dev, int clk_id, int attri,
			  int clk_root, unsigned int freq)
{
	struct q6afe *afe = dev_get_drvdata(dev->parent);
	struct afe_clk_set cset = {0,};

	cset.clk_set_minor_version = AFE_API_VERSION_CLOCK_SET;
	cset.clk_id = clk_id;
	cset.clk_freq_in_hz = freq;
	cset.clk_attri = attri;
	cset.clk_root = clk_root;
	cset.enable = !!freq;

	return q6afe_set_param(afe, NULL, &cset, AFE_PARAM_ID_CLOCK_SET,
			       AFE_MODULE_CLOCK_SET, sizeof(cset),
			       AFE_CLK_TOKEN);
}
EXPORT_SYMBOL_GPL(q6afe_set_lpass_clock);

int q6afe_port_set_sysclk(struct q6afe_port *port, int clk_id,
			  int clk_src, int clk_root,
			  unsigned int freq, int dir)
{
	struct afe_clk_cfg ccfg = {0,};
	struct afe_clk_set cset = {0,};
	struct afe_digital_clk_cfg dcfg = {0,};
	int ret;

	switch (clk_id) {
	case LPAIF_DIG_CLK:
		dcfg.i2s_cfg_minor_version = AFE_API_VERSION_I2S_CONFIG;
		dcfg.clk_val = freq;
		dcfg.clk_root = clk_root;
		ret = q6afe_set_digital_codec_core_clock(port, &dcfg);
		break;
	case LPAIF_BIT_CLK:
		ccfg.i2s_cfg_minor_version = AFE_API_VERSION_I2S_CONFIG;
		ccfg.clk_val1 = freq;
		ccfg.clk_src = clk_src;
		ccfg.clk_root = clk_root;
		ccfg.clk_set_mode = Q6AFE_LPASS_MODE_CLK1_VALID;
		ret = q6afe_port_set_lpass_clock(port, &ccfg);
		break;

	case LPAIF_OSR_CLK:
		ccfg.i2s_cfg_minor_version = AFE_API_VERSION_I2S_CONFIG;
		ccfg.clk_val2 = freq;
		ccfg.clk_src = clk_src;
		ccfg.clk_root = clk_root;
		ccfg.clk_set_mode = Q6AFE_LPASS_MODE_CLK2_VALID;
		ret = q6afe_port_set_lpass_clock(port, &ccfg);
		break;
	case Q6AFE_LPASS_CLK_ID_PRI_MI2S_IBIT ... Q6AFE_LPASS_CLK_ID_QUI_MI2S_OSR:
	case Q6AFE_LPASS_CLK_ID_MCLK_1 ... Q6AFE_LPASS_CLK_ID_INT_MCLK_1:
	case Q6AFE_LPASS_CLK_ID_PRI_TDM_IBIT ... Q6AFE_LPASS_CLK_ID_QUIN_TDM_EBIT:
	case Q6AFE_LPASS_CLK_ID_WSA_CORE_MCLK ... Q6AFE_LPASS_CLK_ID_VA_CORE_2X_MCLK:
		cset.clk_set_minor_version = AFE_API_VERSION_CLOCK_SET;
		cset.clk_id = clk_id;
		cset.clk_freq_in_hz = freq;
		cset.clk_attri = clk_src;
		cset.clk_root = clk_root;
		cset.enable = !!freq;
		ret = q6afe_set_lpass_clock_v2(port, &cset);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(q6afe_port_set_sysclk);

/**
 * q6afe_port_stop() - Stop a afe port
 *
 * @port: Instance of port to stop
 *
 * Return: Will be an negative on packet size on success.
 */
int q6afe_port_stop(struct q6afe_port *port)
{
	struct afe_port_cmd_device_stop *stop;
	struct q6afe *afe = port->afe;
	struct apr_pkt *pkt;
	int port_id = port->id;
	int ret = 0;
	int index, pkt_size;
	void *p;

	index = port->token;
	if (index < 0 || index >= AFE_PORT_MAX) {
		dev_err(afe->dev, "AFE port index[%d] invalid!\n", index);
		return -EINVAL;
	}

	pkt_size = APR_HDR_SIZE + sizeof(*stop);
	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	stop = p + APR_HDR_SIZE;

	pkt->hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
					   APR_HDR_LEN(APR_HDR_SIZE),
					   APR_PKT_VER);
	pkt->hdr.pkt_size = pkt_size;
	pkt->hdr.src_port = 0;
	pkt->hdr.dest_port = 0;
	pkt->hdr.token = index;
	pkt->hdr.opcode = AFE_PORT_CMD_DEVICE_STOP;
	stop->port_id = port_id;
	stop->reserved = 0;

	ret = afe_apr_send_pkt(afe, pkt, port, AFE_PORT_CMD_DEVICE_STOP);
	if (ret)
		dev_err(afe->dev, "AFE close failed %d\n", ret);

	kfree(pkt);
	return ret;
}
EXPORT_SYMBOL_GPL(q6afe_port_stop);

/**
 * q6afe_slim_port_prepare() - Prepare slim afe port.
 *
 * @port: Instance of afe port
 * @cfg: SLIM configuration for the afe port
 *
 */
void q6afe_slim_port_prepare(struct q6afe_port *port,
			     struct q6afe_slim_cfg *cfg)
{
	union afe_port_config *pcfg = &port->port_cfg;

	pcfg->slim_cfg.sb_cfg_minor_version = AFE_API_VERSION_SLIMBUS_CONFIG;
	pcfg->slim_cfg.sample_rate = cfg->sample_rate;
	pcfg->slim_cfg.bit_width = cfg->bit_width;
	pcfg->slim_cfg.num_channels = cfg->num_channels;
	pcfg->slim_cfg.data_format = cfg->data_format;
	pcfg->slim_cfg.shared_ch_mapping[0] = cfg->ch_mapping[0];
	pcfg->slim_cfg.shared_ch_mapping[1] = cfg->ch_mapping[1];
	pcfg->slim_cfg.shared_ch_mapping[2] = cfg->ch_mapping[2];
	pcfg->slim_cfg.shared_ch_mapping[3] = cfg->ch_mapping[3];

}
EXPORT_SYMBOL_GPL(q6afe_slim_port_prepare);

/**
 * q6afe_tdm_port_prepare() - Prepare tdm afe port.
 *
 * @port: Instance of afe port
 * @cfg: TDM configuration for the afe port
 *
 */
void q6afe_tdm_port_prepare(struct q6afe_port *port,
			     struct q6afe_tdm_cfg *cfg)
{
	union afe_port_config *pcfg = &port->port_cfg;

	pcfg->tdm_cfg.tdm_cfg_minor_version = AFE_API_VERSION_TDM_CONFIG;
	pcfg->tdm_cfg.num_channels = cfg->num_channels;
	pcfg->tdm_cfg.sample_rate = cfg->sample_rate;
	pcfg->tdm_cfg.bit_width = cfg->bit_width;
	pcfg->tdm_cfg.data_format = cfg->data_format;
	pcfg->tdm_cfg.sync_mode = cfg->sync_mode;
	pcfg->tdm_cfg.sync_src = cfg->sync_src;
	pcfg->tdm_cfg.nslots_per_frame = cfg->nslots_per_frame;

	pcfg->tdm_cfg.slot_width = cfg->slot_width;
	pcfg->tdm_cfg.slot_mask = cfg->slot_mask;
	port->scfg = kzalloc(sizeof(*port->scfg), GFP_KERNEL);
	if (!port->scfg)
		return;

	port->scfg->minor_version = AFE_API_VERSION_SLOT_MAPPING_CONFIG;
	port->scfg->num_channels = cfg->num_channels;
	port->scfg->bitwidth = cfg->bit_width;
	port->scfg->data_align_type = cfg->data_align_type;
	memcpy(port->scfg->ch_mapping, cfg->ch_mapping,
			sizeof(u16) * AFE_PORT_MAX_AUDIO_CHAN_CNT);
}
EXPORT_SYMBOL_GPL(q6afe_tdm_port_prepare);

/**
 * afe_port_send_usb_dev_param() - Send USB dev token
 *
 * @port: Instance of afe port
 * @cardidx: USB SND card index to reference
 * @pcmidx: USB SND PCM device index to reference
 *
 * The USB dev token carries information about which USB SND card instance and
 * PCM device to execute the offload on.  This information is carried through
 * to the stream enable QMI request, which is handled by the offload class
 * driver.  The information is parsed to determine which USB device to query
 * the required resources for.
 */
int afe_port_send_usb_dev_param(struct q6afe_port *port, int cardidx, int pcmidx)
{
	struct afe_param_id_usb_audio_dev_params usb_dev;
	int ret;

	memset(&usb_dev, 0, sizeof(usb_dev));

	usb_dev.cfg_minor_version = AFE_API_MINOR_VERSION_USB_AUDIO_CONFIG;
	usb_dev.dev_token = (cardidx << 16) | (pcmidx << 8);
	ret = q6afe_port_set_param_v2(port, &usb_dev,
				      AFE_PARAM_ID_USB_AUDIO_DEV_PARAMS,
				      AFE_MODULE_AUDIO_DEV_INTERFACE,
				      sizeof(usb_dev));
	if (ret)
		dev_err(port->afe->dev, "%s: AFE device param cmd failed %d\n",
			__func__, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(afe_port_send_usb_dev_param);

static int afe_port_send_usb_params(struct q6afe_port *port, struct q6afe_usb_cfg *cfg)
{
	union afe_port_config *pcfg = &port->port_cfg;
	struct afe_param_id_usb_audio_dev_lpcm_fmt lpcm_fmt;
	struct afe_param_id_usb_audio_svc_interval svc_int;
	int ret;

	if (!pcfg) {
		dev_err(port->afe->dev, "%s: Error, no configuration data\n", __func__);
		return -EINVAL;
	}

	memset(&lpcm_fmt, 0, sizeof(lpcm_fmt));
	memset(&svc_int, 0, sizeof(svc_int));

	lpcm_fmt.cfg_minor_version = AFE_API_MINOR_VERSION_USB_AUDIO_CONFIG;
	lpcm_fmt.endian = pcfg->usb_cfg.endian;
	ret = q6afe_port_set_param_v2(port, &lpcm_fmt,
				      AFE_PARAM_ID_USB_AUDIO_DEV_LPCM_FMT,
				      AFE_MODULE_AUDIO_DEV_INTERFACE, sizeof(lpcm_fmt));
	if (ret) {
		dev_err(port->afe->dev, "%s: AFE device param cmd LPCM_FMT failed %d\n",
			__func__, ret);
		return ret;
	}

	svc_int.cfg_minor_version = AFE_API_MINOR_VERSION_USB_AUDIO_CONFIG;
	svc_int.svc_interval = pcfg->usb_cfg.service_interval;
	ret = q6afe_port_set_param_v2(port, &svc_int,
				      AFE_PARAM_ID_USB_AUDIO_SVC_INTERVAL,
				      AFE_MODULE_AUDIO_DEV_INTERFACE, sizeof(svc_int));
	if (ret)
		dev_err(port->afe->dev, "%s: AFE device param cmd svc_interval failed %d\n",
			__func__, ret);

	return ret;
}

/**
 * q6afe_usb_port_prepare() - Prepare usb afe port.
 *
 * @port: Instance of afe port
 * @cfg: USB configuration for the afe port
 *
 */
void q6afe_usb_port_prepare(struct q6afe_port *port,
			    struct q6afe_usb_cfg *cfg)
{
	union afe_port_config *pcfg = &port->port_cfg;

	pcfg->usb_cfg.cfg_minor_version = AFE_API_MINOR_VERSION_USB_AUDIO_CONFIG;
	pcfg->usb_cfg.sample_rate = cfg->sample_rate;
	pcfg->usb_cfg.num_channels = cfg->num_channels;
	pcfg->usb_cfg.bit_width = cfg->bit_width;

	afe_port_send_usb_params(port, cfg);
}
EXPORT_SYMBOL_GPL(q6afe_usb_port_prepare);

/**
 * q6afe_hdmi_port_prepare() - Prepare hdmi afe port.
 *
 * @port: Instance of afe port
 * @cfg: HDMI configuration for the afe port
 *
 */
void q6afe_hdmi_port_prepare(struct q6afe_port *port,
			     struct q6afe_hdmi_cfg *cfg)
{
	union afe_port_config *pcfg = &port->port_cfg;

	pcfg->hdmi_multi_ch.hdmi_cfg_minor_version =
					AFE_API_VERSION_HDMI_CONFIG;
	pcfg->hdmi_multi_ch.datatype = cfg->datatype;
	pcfg->hdmi_multi_ch.channel_allocation = cfg->channel_allocation;
	pcfg->hdmi_multi_ch.sample_rate = cfg->sample_rate;
	pcfg->hdmi_multi_ch.bit_width = cfg->bit_width;
}
EXPORT_SYMBOL_GPL(q6afe_hdmi_port_prepare);

/**
 * q6afe_i2s_port_prepare() - Prepare i2s afe port.
 *
 * @port: Instance of afe port
 * @cfg: I2S configuration for the afe port
 * Return: Will be an negative on error and zero on success.
 */
int q6afe_i2s_port_prepare(struct q6afe_port *port, struct q6afe_i2s_cfg *cfg)
{
	union afe_port_config *pcfg = &port->port_cfg;
	struct device *dev = port->afe->dev;
	int num_sd_lines;

	pcfg->i2s_cfg.i2s_cfg_minor_version = AFE_API_VERSION_I2S_CONFIG;
	pcfg->i2s_cfg.sample_rate = cfg->sample_rate;
	pcfg->i2s_cfg.bit_width = cfg->bit_width;
	pcfg->i2s_cfg.data_format = AFE_LINEAR_PCM_DATA;

	switch (cfg->fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		pcfg->i2s_cfg.ws_src = AFE_PORT_CONFIG_I2S_WS_SRC_INTERNAL;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		/* CPU is slave */
		pcfg->i2s_cfg.ws_src = AFE_PORT_CONFIG_I2S_WS_SRC_EXTERNAL;
		break;
	default:
		break;
	}

	num_sd_lines = hweight_long(cfg->sd_line_mask);

	switch (num_sd_lines) {
	case 0:
		dev_err(dev, "no line is assigned\n");
		return -EINVAL;
	case 1:
		switch (cfg->sd_line_mask) {
		case AFE_PORT_I2S_SD0_MASK:
			pcfg->i2s_cfg.channel_mode = AFE_PORT_I2S_SD0;
			break;
		case AFE_PORT_I2S_SD1_MASK:
			pcfg->i2s_cfg.channel_mode = AFE_PORT_I2S_SD1;
			break;
		case AFE_PORT_I2S_SD2_MASK:
			pcfg->i2s_cfg.channel_mode = AFE_PORT_I2S_SD2;
			break;
		case AFE_PORT_I2S_SD3_MASK:
			pcfg->i2s_cfg.channel_mode = AFE_PORT_I2S_SD3;
			break;
		default:
			dev_err(dev, "Invalid SD lines\n");
			return -EINVAL;
		}
		break;
	case 2:
		switch (cfg->sd_line_mask) {
		case AFE_PORT_I2S_SD0_1_MASK:
			pcfg->i2s_cfg.channel_mode = AFE_PORT_I2S_QUAD01;
			break;
		case AFE_PORT_I2S_SD2_3_MASK:
			pcfg->i2s_cfg.channel_mode = AFE_PORT_I2S_QUAD23;
			break;
		default:
			dev_err(dev, "Invalid SD lines\n");
			return -EINVAL;
		}
		break;
	case 3:
		switch (cfg->sd_line_mask) {
		case AFE_PORT_I2S_SD0_1_2_MASK:
			pcfg->i2s_cfg.channel_mode = AFE_PORT_I2S_6CHS;
			break;
		default:
			dev_err(dev, "Invalid SD lines\n");
			return -EINVAL;
		}
		break;
	case 4:
		switch (cfg->sd_line_mask) {
		case AFE_PORT_I2S_SD0_1_2_3_MASK:
			pcfg->i2s_cfg.channel_mode = AFE_PORT_I2S_8CHS;

			break;
		default:
			dev_err(dev, "Invalid SD lines\n");
			return -EINVAL;
		}
		break;
	default:
		dev_err(dev, "Invalid SD lines\n");
		return -EINVAL;
	}

	switch (cfg->num_channels) {
	case 1:
	case 2:
		switch (pcfg->i2s_cfg.channel_mode) {
		case AFE_PORT_I2S_QUAD01:
		case AFE_PORT_I2S_6CHS:
		case AFE_PORT_I2S_8CHS:
			pcfg->i2s_cfg.channel_mode = AFE_PORT_I2S_SD0;
			break;
		case AFE_PORT_I2S_QUAD23:
				pcfg->i2s_cfg.channel_mode = AFE_PORT_I2S_SD2;
			break;
		}

		if (cfg->num_channels == 2)
			pcfg->i2s_cfg.mono_stereo = AFE_PORT_I2S_STEREO;
		else
			pcfg->i2s_cfg.mono_stereo = AFE_PORT_I2S_MONO;

		break;
	case 3:
	case 4:
		if (pcfg->i2s_cfg.channel_mode < AFE_PORT_I2S_QUAD01) {
			dev_err(dev, "Invalid Channel mode\n");
			return -EINVAL;
		}
		break;
	case 5:
	case 6:
		if (pcfg->i2s_cfg.channel_mode < AFE_PORT_I2S_6CHS) {
			dev_err(dev, "Invalid Channel mode\n");
			return -EINVAL;
		}
		break;
	case 7:
	case 8:
		if (pcfg->i2s_cfg.channel_mode < AFE_PORT_I2S_8CHS) {
			dev_err(dev, "Invalid Channel mode\n");
			return -EINVAL;
		}
		break;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(q6afe_i2s_port_prepare);

/**
 * q6afe_cdc_dma_port_prepare() - Prepare dma afe port.
 *
 * @port: Instance of afe port
 * @cfg: DMA configuration for the afe port
 *
 */
void q6afe_cdc_dma_port_prepare(struct q6afe_port *port,
				struct q6afe_cdc_dma_cfg *cfg)
{
	union afe_port_config *pcfg = &port->port_cfg;
	struct afe_param_id_cdc_dma_cfg *dma_cfg = &pcfg->dma_cfg;

	dma_cfg->cdc_dma_cfg_minor_version = AFE_API_VERSION_CODEC_DMA_CONFIG;
	dma_cfg->sample_rate = cfg->sample_rate;
	dma_cfg->bit_width = cfg->bit_width;
	dma_cfg->data_format = cfg->data_format;
	dma_cfg->num_channels = cfg->num_channels;
	if (!cfg->active_channels_mask)
		dma_cfg->active_channels_mask = (1 << cfg->num_channels) - 1;
}
EXPORT_SYMBOL_GPL(q6afe_cdc_dma_port_prepare);
/**
 * q6afe_port_start() - Start a afe port
 *
 * @port: Instance of port to start
 *
 * Return: Will be an negative on packet size on success.
 */
int q6afe_port_start(struct q6afe_port *port)
{
	struct afe_port_cmd_device_start *start;
	struct q6afe *afe = port->afe;
	int port_id = port->id;
	int ret, param_id = port->cfg_type;
	struct apr_pkt *pkt;
	int pkt_size;
	void *p;

	ret  = q6afe_port_set_param_v2(port, &port->port_cfg, param_id,
				       AFE_MODULE_AUDIO_DEV_INTERFACE,
				       sizeof(port->port_cfg));
	if (ret) {
		dev_err(afe->dev, "AFE enable for port 0x%x failed %d\n",
			port_id, ret);
		return ret;
	}

	if (port->scfg) {
		ret  = q6afe_port_set_param_v2(port, port->scfg,
					AFE_PARAM_ID_PORT_SLOT_MAPPING_CONFIG,
					AFE_MODULE_TDM, sizeof(*port->scfg));
		if (ret) {
			dev_err(afe->dev, "AFE enable for port 0x%x failed %d\n",
			port_id, ret);
			return ret;
		}
	}

	pkt_size = APR_HDR_SIZE + sizeof(*start);
	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	start = p + APR_HDR_SIZE;

	pkt->hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
					    APR_HDR_LEN(APR_HDR_SIZE),
					    APR_PKT_VER);
	pkt->hdr.pkt_size = pkt_size;
	pkt->hdr.src_port = 0;
	pkt->hdr.dest_port = 0;
	pkt->hdr.token = port->token;
	pkt->hdr.opcode = AFE_PORT_CMD_DEVICE_START;

	start->port_id = port_id;

	ret = afe_apr_send_pkt(afe, pkt, port, AFE_PORT_CMD_DEVICE_START);
	if (ret)
		dev_err(afe->dev, "AFE enable for port 0x%x failed %d\n",
			port_id, ret);

	kfree(pkt);
	return ret;
}
EXPORT_SYMBOL_GPL(q6afe_port_start);

/**
 * q6afe_port_get_from_id() - Get port instance from a port id
 *
 * @dev: Pointer to afe child device.
 * @id: port id
 *
 * Return: Will be an error pointer on error or a valid afe port
 * on success.
 */
struct q6afe_port *q6afe_port_get_from_id(struct device *dev, int id)
{
	int port_id;
	struct q6afe *afe = dev_get_drvdata(dev->parent);
	struct q6afe_port *port;
	unsigned long flags;
	int cfg_type;

	if (id < 0 || id >= AFE_PORT_MAX) {
		dev_err(dev, "AFE port token[%d] invalid!\n", id);
		return ERR_PTR(-EINVAL);
	}

	/* if port is multiple times bind/unbind before callback finishes */
	port = q6afe_find_port(afe, id);
	if (port) {
		dev_err(dev, "AFE Port already open\n");
		return port;
	}

	port_id = port_maps[id].port_id;

	switch (port_id) {
	case AFE_PORT_ID_MULTICHAN_HDMI_RX:
	case AFE_PORT_ID_HDMI_OVER_DP_RX:
		cfg_type = AFE_PARAM_ID_HDMI_CONFIG;
		break;
	case AFE_PORT_ID_SLIMBUS_MULTI_CHAN_0_TX:
	case AFE_PORT_ID_SLIMBUS_MULTI_CHAN_1_TX:
	case AFE_PORT_ID_SLIMBUS_MULTI_CHAN_2_TX:
	case AFE_PORT_ID_SLIMBUS_MULTI_CHAN_3_TX:
	case AFE_PORT_ID_SLIMBUS_MULTI_CHAN_4_TX:
	case AFE_PORT_ID_SLIMBUS_MULTI_CHAN_5_TX:
	case AFE_PORT_ID_SLIMBUS_MULTI_CHAN_6_TX:
	case AFE_PORT_ID_SLIMBUS_MULTI_CHAN_0_RX:
	case AFE_PORT_ID_SLIMBUS_MULTI_CHAN_1_RX:
	case AFE_PORT_ID_SLIMBUS_MULTI_CHAN_2_RX:
	case AFE_PORT_ID_SLIMBUS_MULTI_CHAN_3_RX:
	case AFE_PORT_ID_SLIMBUS_MULTI_CHAN_4_RX:
	case AFE_PORT_ID_SLIMBUS_MULTI_CHAN_5_RX:
	case AFE_PORT_ID_SLIMBUS_MULTI_CHAN_6_RX:
		cfg_type = AFE_PARAM_ID_SLIMBUS_CONFIG;
		break;

	case AFE_PORT_ID_PRIMARY_MI2S_RX:
	case AFE_PORT_ID_PRIMARY_MI2S_TX:
	case AFE_PORT_ID_SECONDARY_MI2S_RX:
	case AFE_PORT_ID_SECONDARY_MI2S_TX:
	case AFE_PORT_ID_TERTIARY_MI2S_RX:
	case AFE_PORT_ID_TERTIARY_MI2S_TX:
	case AFE_PORT_ID_QUATERNARY_MI2S_RX:
	case AFE_PORT_ID_QUATERNARY_MI2S_TX:
	case AFE_PORT_ID_QUINARY_MI2S_RX:
	case AFE_PORT_ID_QUINARY_MI2S_TX:
		cfg_type = AFE_PARAM_ID_I2S_CONFIG;
		break;
	case AFE_PORT_ID_PRIMARY_TDM_RX ... AFE_PORT_ID_QUINARY_TDM_TX_7:
		cfg_type = AFE_PARAM_ID_TDM_CONFIG;
		break;
	case AFE_PORT_ID_WSA_CODEC_DMA_RX_0 ... AFE_PORT_ID_RX_CODEC_DMA_RX_7:
		cfg_type = AFE_PARAM_ID_CODEC_DMA_CONFIG;
		break;
	case AFE_PORT_ID_USB_RX:
		cfg_type = AFE_PARAM_ID_USB_AUDIO_CONFIG;
		break;
	default:
		dev_err(dev, "Invalid port id 0x%x\n", port_id);
		return ERR_PTR(-EINVAL);
	}

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return ERR_PTR(-ENOMEM);

	init_waitqueue_head(&port->wait);

	port->token = id;
	port->id = port_id;
	port->afe = afe;
	port->cfg_type = cfg_type;
	kref_init(&port->refcount);

	spin_lock_irqsave(&afe->port_list_lock, flags);
	list_add_tail(&port->node, &afe->port_list);
	spin_unlock_irqrestore(&afe->port_list_lock, flags);

	return port;

}
EXPORT_SYMBOL_GPL(q6afe_port_get_from_id);

/**
 * q6afe_port_put() - Release port reference
 *
 * @port: Instance of port to put
 */
void q6afe_port_put(struct q6afe_port *port)
{
	kref_put(&port->refcount, q6afe_port_free);
}
EXPORT_SYMBOL_GPL(q6afe_port_put);

int q6afe_unvote_lpass_core_hw(struct device *dev, uint32_t hw_block_id,
			       uint32_t client_handle)
{
	struct q6afe *afe = dev_get_drvdata(dev->parent);
	struct afe_cmd_remote_lpass_core_hw_devote_request *vote_cfg;
	struct apr_pkt *pkt;
	int ret = 0;
	int pkt_size;
	void *p;

	pkt_size = APR_HDR_SIZE + sizeof(*vote_cfg);
	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	vote_cfg = p + APR_HDR_SIZE;

	pkt->hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
					   APR_HDR_LEN(APR_HDR_SIZE),
					   APR_PKT_VER);
	pkt->hdr.pkt_size = pkt_size;
	pkt->hdr.src_port = 0;
	pkt->hdr.dest_port = 0;
	pkt->hdr.token = hw_block_id;
	pkt->hdr.opcode = AFE_CMD_REMOTE_LPASS_CORE_HW_DEVOTE_REQUEST;
	vote_cfg->hw_block_id = hw_block_id;
	vote_cfg->client_handle = client_handle;

	ret = apr_send_pkt(afe->apr, pkt);
	if (ret < 0)
		dev_err(afe->dev, "AFE failed to unvote (%d)\n", hw_block_id);

	kfree(pkt);
	return ret;
}
EXPORT_SYMBOL(q6afe_unvote_lpass_core_hw);

int q6afe_vote_lpass_core_hw(struct device *dev, uint32_t hw_block_id,
			     const char *client_name, uint32_t *client_handle)
{
	struct q6afe *afe = dev_get_drvdata(dev->parent);
	struct afe_cmd_remote_lpass_core_hw_vote_request *vote_cfg;
	struct apr_pkt *pkt;
	int ret = 0;
	int pkt_size;
	void *p;

	pkt_size = APR_HDR_SIZE + sizeof(*vote_cfg);
	p = kzalloc(pkt_size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pkt = p;
	vote_cfg = p + APR_HDR_SIZE;

	pkt->hdr.hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD,
					   APR_HDR_LEN(APR_HDR_SIZE),
					   APR_PKT_VER);
	pkt->hdr.pkt_size = pkt_size;
	pkt->hdr.src_port = 0;
	pkt->hdr.dest_port = 0;
	pkt->hdr.token = hw_block_id;
	pkt->hdr.opcode = AFE_CMD_REMOTE_LPASS_CORE_HW_VOTE_REQUEST;
	vote_cfg->hw_block_id = hw_block_id;
	strscpy(vote_cfg->client_name, client_name,
			sizeof(vote_cfg->client_name));

	ret = afe_apr_send_pkt(afe, pkt, NULL,
			       AFE_CMD_RSP_REMOTE_LPASS_CORE_HW_VOTE_REQUEST);
	if (ret)
		dev_err(afe->dev, "AFE failed to vote (%d)\n", hw_block_id);


	kfree(pkt);
	return ret;
}
EXPORT_SYMBOL(q6afe_vote_lpass_core_hw);

static int q6afe_probe(struct apr_device *adev)
{
	struct q6afe *afe;
	struct device *dev = &adev->dev;

	afe = devm_kzalloc(dev, sizeof(*afe), GFP_KERNEL);
	if (!afe)
		return -ENOMEM;

	q6core_get_svc_api_info(adev->svc_id, &afe->ainfo);
	afe->apr = adev;
	mutex_init(&afe->lock);
	init_waitqueue_head(&afe->wait);
	afe->dev = dev;
	INIT_LIST_HEAD(&afe->port_list);
	spin_lock_init(&afe->port_list_lock);

	dev_set_drvdata(dev, afe);

	return devm_of_platform_populate(dev);
}

#ifdef CONFIG_OF
static const struct of_device_id q6afe_device_id[]  = {
	{ .compatible = "qcom,q6afe" },
	{},
};
MODULE_DEVICE_TABLE(of, q6afe_device_id);
#endif

static struct apr_driver qcom_q6afe_driver = {
	.probe = q6afe_probe,
	.callback = q6afe_callback,
	.driver = {
		.name = "qcom-q6afe",
		.of_match_table = of_match_ptr(q6afe_device_id),

	},
};

module_apr_driver(qcom_q6afe_driver);
MODULE_DESCRIPTION("Q6 Audio Front End");
MODULE_LICENSE("GPL v2");
