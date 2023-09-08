/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef __SND_AR_TOKENS_H__
#define __SND_AR_TOKENS_H__

#define APM_SUB_GRAPH_PERF_MODE_LOW_POWER	0x1
#define APM_SUB_GRAPH_PERF_MODE_LOW_LATENCY	0x2

#define APM_SUB_GRAPH_DIRECTION_TX		0x1
#define APM_SUB_GRAPH_DIRECTION_RX		0x2

/** Scenario ID Audio Playback */
#define APM_SUB_GRAPH_SID_AUDIO_PLAYBACK          0x1
/* Scenario ID Audio Record */
#define APM_SUB_GRAPH_SID_AUDIO_RECORD            0x2
/* Scenario ID Voice call. */
#define APM_SUB_GRAPH_SID_VOICE_CALL              0x3

/* container capability ID Pre/Post Processing (PP) */
#define APM_CONTAINER_CAP_ID_PP                   0x1
/* container capability ID Compression/Decompression (CD) */
#define APM_CONTAINER_CAP_ID_CD                   0x2
/* container capability ID End Point(EP) */
#define APM_CONTAINER_CAP_ID_EP                   0x3
/* container capability ID Offload (OLC) */
#define APM_CONTAINER_CAP_ID_OLC                  0x4

/* container graph position Stream */
#define APM_CONT_GRAPH_POS_STREAM                 0x1
/* container graph position Per Stream Per Device*/
#define APM_CONT_GRAPH_POS_PER_STR_PER_DEV        0x2
/* container graph position Stream-Device */
#define APM_CONT_GRAPH_POS_STR_DEV                0x3
/* container graph position Global Device */
#define APM_CONT_GRAPH_POS_GLOBAL_DEV             0x4

#define APM_PROC_DOMAIN_ID_MDSP			0x1
#define APM_PROC_DOMAIN_ID_ADSP			0x2
#define APM_PROC_DOMAIN_ID_SDSP			0x4
#define APM_PROC_DOMAIN_ID_CDSP			0x5

#define PCM_INTERLEAVED			1
#define PCM_DEINTERLEAVED_PACKED	2
#define PCM_DEINTERLEAVED_UNPACKED	3
#define AR_I2S_WS_SRC_EXTERNAL	0
#define AR_I2S_WS_SRC_INTERNAL	1

enum ar_event_types {
	AR_EVENT_NONE = 0,
	AR_PGA_DAPM_EVENT
};

/*
 * Kcontrol IDs
 */
#define SND_SOC_AR_TPLG_FE_BE_GRAPH_CTL_MIX	256
#define SND_SOC_AR_TPLG_VOL_CTL			257

/**
 * %AR_TKN_U32_SUB_GRAPH_INSTANCE_ID:		Sub Graph Instance Id
 *
 * %AR_TKN_U32_SUB_GRAPH_PERF_MODE:		Performance mode of subgraph
 *						APM_SUB_GRAPH_PERF_MODE_LOW_POWER = 1,
 *						APM_SUB_GRAPH_PERF_MODE_LOW_LATENCY = 2
 *
 * %AR_TKN_U32_SUB_GRAPH_DIRECTION:		Direction of subgraph
 *						APM_SUB_GRAPH_DIRECTION_TX = 1,
 *						APM_SUB_GRAPH_DIRECTION_RX = 2
 *
 * %AR_TKN_U32_SUB_GRAPH_SCENARIO_ID:		Scenario ID for subgraph
 *						APM_SUB_GRAPH_SID_AUDIO_PLAYBACK = 1,
 *						APM_SUB_GRAPH_SID_AUDIO_RECORD = 2,
 *						APM_SUB_GRAPH_SID_VOICE_CALL = 3
 *
 * %AR_TKN_U32_CONTAINER_INSTANCE_ID:		Container Instance ID
 *
 * %AR_TKN_U32_CONTAINER_CAPABILITY_ID:		Container capability ID
 *						APM_CONTAINER_CAP_ID_PP = 1,
 *						APM_CONTAINER_CAP_ID_CD = 2,
 *						APM_CONTAINER_CAP_ID_EP = 3,
 *						APM_CONTAINER_CAP_ID_OLC = 4
 *
 * %AR_TKN_U32_CONTAINER_STACK_SIZE:		Stack size in the container.
 *
 * %AR_TKN_U32_CONTAINER_GRAPH_POS:		Graph Position
 *						APM_CONT_GRAPH_POS_STREAM = 1,
 *						APM_CONT_GRAPH_POS_PER_STR_PER_DEV = 2,
 *						APM_CONT_GRAPH_POS_STR_DEV = 3,
 *						APM_CONT_GRAPH_POS_GLOBAL_DEV = 4
 *
 * %AR_TKN_U32_CONTAINER_PROC_DOMAIN:		Processor domain of container
 *						APM_PROC_DOMAIN_ID_MDSP = 1,
 *						APM_PROC_DOMAIN_ID_ADSP = 2,
 *						APM_PROC_DOMAIN_ID_SDSP = 4,
 *						APM_PROC_DOMAIN_ID_CDSP = 5
 *
 * %AR_TKN_U32_MODULE_ID:			Module ID
 *
 * %AR_TKN_U32_MODULE_INSTANCE_ID:		Module Instance ID.
 *
 * %AR_TKN_U32_MODULE_MAX_IP_PORTS:		Module maximum input ports
 *
 * %AR_TKN_U32_MODULE_MAX_OP_PORTS:		Module maximum output ports.
 *
 * %AR_TKN_U32_MODULE_IN_PORTS:			Number of in ports
 *
 * %AR_TKN_U32_MODULE_OUT_PORTS:		Number of out ports.
 *
 * %AR_TKN_U32_MODULE_SRC_OP_PORT_ID:		Source module output port ID
 *
 * %AR_TKN_U32_MODULE_DST_IN_PORT_ID:		Destination module input port ID
 *
 * %AR_TKN_U32_MODULE_HW_IF_IDX:		Interface index types for I2S/LPAIF
 *
 * %AR_TKN_U32_MODULE_HW_IF_TYPE:		Interface type
 *						LPAIF = 0,
 *						LPAIF_RXTX = 1,
 *						LPAIF_WSA = 2,
 *						LPAIF_VA = 3,
 *						LPAIF_AXI = 4
 *
 * %AR_TKN_U32_MODULE_FMT_INTERLEAVE:		PCM Interleaving
 *						PCM_INTERLEAVED = 1,
 *						PCM_DEINTERLEAVED_PACKED = 2,
 *						PCM_DEINTERLEAVED_UNPACKED = 3
 *
 * %AR_TKN_U32_MODULE_FMT_DATA:			data format
 *						FIXED POINT = 1,
 *						IEC60958 PACKETIZED = 3,
 *						IEC60958 PACKETIZED NON LINEAR = 8,
 *						COMPR OVER PCM PACKETIZED = 7,
 *						IEC61937 PACKETIZED = 2,
 *						GENERIC COMPRESSED = 5
 *
 * %AR_TKN_U32_MODULE_FMT_SAMPLE_RATE:		sample rate
 *
 * %AR_TKN_U32_MODULE_FMT_BIT_DEPTH:		bit depth
 *
 * %AR_TKN_U32_MODULE_SD_LINE_IDX:		I2S serial data line idx
 *						I2S_SD0 = 1,
 *						I2S_SD1 = 2,
 *						I2S_SD2 = 3,
 *						I2S_SD3 = 4,
 *						I2S_QUAD01 = 5,
 *						I2S_QUAD23 = 6,
 *						I2S_6CHS = 7,
 *						I2S_8CHS = 8
 *
 * %AR_TKN_U32_MODULE_WS_SRC:			Word Select Source
 *						AR_I2S_WS_SRC_EXTERNAL = 0,
 *						AR_I2S_WS_SRC_INTERNAL = 1,
 *
 * %AR_TKN_U32_MODULE_FRAME_SZ_FACTOR:		Frame size factor
 *
 * %AR_TKN_U32_MODULE_LOG_CODE:			Log Module Code
 *
 * %AR_TKN_U32_MODULE_LOG_TAP_POINT_ID:		logging tap point of this module
 *
 * %AR_TKN_U32_MODULE_LOG_MODE:			logging mode
 *						LOG_WAIT = 0,
 *						LOG_IMMEDIATELY = 1
 *
 * %AR_TKN_DAI_INDEX:				dai index
 *
 */

/* DAI Tokens */
#define AR_TKN_DAI_INDEX			1
/* SUB GRAPH Tokens */
#define AR_TKN_U32_SUB_GRAPH_INSTANCE_ID	2
#define AR_TKN_U32_SUB_GRAPH_PERF_MODE		3
#define AR_TKN_U32_SUB_GRAPH_DIRECTION		4
#define AR_TKN_U32_SUB_GRAPH_SCENARIO_ID	5

/* Container Tokens */
#define AR_TKN_U32_CONTAINER_INSTANCE_ID	100
#define AR_TKN_U32_CONTAINER_CAPABILITY_ID	101
#define AR_TKN_U32_CONTAINER_STACK_SIZE		102
#define AR_TKN_U32_CONTAINER_GRAPH_POS		103
#define AR_TKN_U32_CONTAINER_PROC_DOMAIN	104

/* Module Tokens */
#define AR_TKN_U32_MODULE_ID			200
#define AR_TKN_U32_MODULE_INSTANCE_ID		201
#define AR_TKN_U32_MODULE_MAX_IP_PORTS		202
#define AR_TKN_U32_MODULE_MAX_OP_PORTS		203
#define AR_TKN_U32_MODULE_IN_PORTS		204
#define AR_TKN_U32_MODULE_OUT_PORTS		205
#define AR_TKN_U32_MODULE_SRC_OP_PORT_ID	206
#define AR_TKN_U32_MODULE_DST_IN_PORT_ID	207
#define AR_TKN_U32_MODULE_SRC_INSTANCE_ID	208
#define AR_TKN_U32_MODULE_DST_INSTANCE_ID	209

#define AR_TKN_U32_MODULE_SRC_OP_PORT_ID1	210
#define AR_TKN_U32_MODULE_DST_IN_PORT_ID1	211
#define AR_TKN_U32_MODULE_DST_INSTANCE_ID1	212

#define AR_TKN_U32_MODULE_SRC_OP_PORT_ID2	213
#define AR_TKN_U32_MODULE_DST_IN_PORT_ID2	214
#define AR_TKN_U32_MODULE_DST_INSTANCE_ID2	215

#define AR_TKN_U32_MODULE_SRC_OP_PORT_ID3	216
#define AR_TKN_U32_MODULE_DST_IN_PORT_ID3	217
#define AR_TKN_U32_MODULE_DST_INSTANCE_ID3	218

#define AR_TKN_U32_MODULE_SRC_OP_PORT_ID4	219
#define AR_TKN_U32_MODULE_DST_IN_PORT_ID4	220
#define AR_TKN_U32_MODULE_DST_INSTANCE_ID4	221

#define AR_TKN_U32_MODULE_SRC_OP_PORT_ID5	222
#define AR_TKN_U32_MODULE_DST_IN_PORT_ID5	223
#define AR_TKN_U32_MODULE_DST_INSTANCE_ID5	224

#define AR_TKN_U32_MODULE_SRC_OP_PORT_ID6	225
#define AR_TKN_U32_MODULE_DST_IN_PORT_ID6	226
#define AR_TKN_U32_MODULE_DST_INSTANCE_ID6	227

#define AR_TKN_U32_MODULE_SRC_OP_PORT_ID7	228
#define AR_TKN_U32_MODULE_DST_IN_PORT_ID7	229
#define AR_TKN_U32_MODULE_DST_INSTANCE_ID7	230

#define AR_TKN_U32_MODULE_HW_IF_IDX		250
#define AR_TKN_U32_MODULE_HW_IF_TYPE		251
#define AR_TKN_U32_MODULE_FMT_INTERLEAVE	252
#define AR_TKN_U32_MODULE_FMT_DATA		253
#define AR_TKN_U32_MODULE_FMT_SAMPLE_RATE	254
#define AR_TKN_U32_MODULE_FMT_BIT_DEPTH		255
#define AR_TKN_U32_MODULE_SD_LINE_IDX		256
#define AR_TKN_U32_MODULE_WS_SRC		257
#define AR_TKN_U32_MODULE_FRAME_SZ_FACTOR	258
#define AR_TKN_U32_MODULE_LOG_CODE		259
#define AR_TKN_U32_MODULE_LOG_TAP_POINT_ID	260
#define AR_TKN_U32_MODULE_LOG_MODE		261

#endif /* __SND_AR_TOKENS_H__ */
