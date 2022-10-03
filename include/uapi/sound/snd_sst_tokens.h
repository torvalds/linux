/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * snd_sst_tokens.h - Intel SST tokens definition
 *
 * Copyright (C) 2016 Intel Corp
 * Author: Shreyas NC <shreyas.nc@intel.com>
 */
#ifndef __SND_SST_TOKENS_H__
#define __SND_SST_TOKENS_H__

/**
 * %SKL_TKN_UUID:               Module UUID
 *
 * %SKL_TKN_U8_BLOCK_TYPE:      Type of the private data block.Can be:
 *                              tuples, bytes, short and words
 *
 * %SKL_TKN_U8_IN_PIN_TYPE:     Input pin type,
 *                              homogenous=0, heterogenous=1
 *
 * %SKL_TKN_U8_OUT_PIN_TYPE:    Output pin type,
 *                              homogenous=0, heterogenous=1
 * %SKL_TKN_U8_DYN_IN_PIN:      Configure Input pin dynamically
 *                              if true
 *
 * %SKL_TKN_U8_DYN_OUT_PIN:     Configure Output pin dynamically
 *                              if true
 *
 * %SKL_TKN_U8_IN_QUEUE_COUNT:  Store the number of Input pins
 *
 * %SKL_TKN_U8_OUT_QUEUE_COUNT: Store the number of Output pins
 *
 * %SKL_TKN_U8_TIME_SLOT:       TDM slot number
 *
 * %SKL_TKN_U8_CORE_ID:         Stores module affinity value.Can take
 *                              the values:
 *                              SKL_AFFINITY_CORE_0 = 0,
 *                              SKL_AFFINITY_CORE_1,
 *                              SKL_AFFINITY_CORE_MAX
 *
 * %SKL_TKN_U8_MOD_TYPE:        Module type value.
 *
 * %SKL_TKN_U8_CONN_TYPE:       Module connection type can be a FE,
 *                              BE or NONE as defined :
 *                              SKL_PIPE_CONN_TYPE_NONE = 0,
 *                              SKL_PIPE_CONN_TYPE_FE = 1 (HOST_DMA)
 *                              SKL_PIPE_CONN_TYPE_BE = 2 (LINK_DMA)
 *
 * %SKL_TKN_U8_DEV_TYPE:        Type of device to which the module is
 *                              connected
 *                              Can take the values:
 *                              SKL_DEVICE_BT = 0x0,
 *                              SKL_DEVICE_DMIC = 0x1,
 *                              SKL_DEVICE_I2S = 0x2,
 *                              SKL_DEVICE_SLIMBUS = 0x3,
 *                              SKL_DEVICE_HDALINK = 0x4,
 *                              SKL_DEVICE_HDAHOST = 0x5,
 *                              SKL_DEVICE_NONE
 *
 * %SKL_TKN_U8_HW_CONN_TYPE:    Connection type of the HW to which the
 *                              module is connected
 *                              SKL_CONN_NONE = 0,
 *                              SKL_CONN_SOURCE = 1,
 *                              SKL_CONN_SINK = 2
 *
 * %SKL_TKN_U16_PIN_INST_ID:    Stores the pin instance id
 *
 * %SKL_TKN_U16_MOD_INST_ID:    Stores the mdule instance id
 *
 * %SKL_TKN_U32_MAX_MCPS:       Module max mcps value
 *
 * %SKL_TKN_U32_MEM_PAGES:      Module resource pages
 *
 * %SKL_TKN_U32_OBS:            Stores Output Buffer size
 *
 * %SKL_TKN_U32_IBS:            Stores input buffer size
 *
 * %SKL_TKN_U32_VBUS_ID:        Module VBUS_ID. PDM=0, SSP0=0,
 *                              SSP1=1,SSP2=2,
 *                              SSP3=3, SSP4=4,
 *                              SSP5=5, SSP6=6,INVALID
 *
 * %SKL_TKN_U32_PARAMS_FIXUP:   Module Params fixup mask
 * %SKL_TKN_U32_CONVERTER:      Module params converter mask
 * %SKL_TKN_U32_PIPE_ID:        Stores the pipe id
 *
 * %SKL_TKN_U32_PIPE_CONN_TYPE: Type of the token to which the pipe is
 *                              connected to. It can be
 *                              SKL_PIPE_CONN_TYPE_NONE = 0,
 *                              SKL_PIPE_CONN_TYPE_FE = 1 (HOST_DMA),
 *                              SKL_PIPE_CONN_TYPE_BE = 2 (LINK_DMA),
 *
 * %SKL_TKN_U32_PIPE_PRIORITY:  Pipe priority value
 * %SKL_TKN_U32_PIPE_MEM_PGS:   Pipe resource pages
 *
 * %SKL_TKN_U32_DIR_PIN_COUNT:  Value for the direction to set input/output
 *                              formats and the pin count.
 *                              The first 4 bits have the direction
 *                              value and the next 4 have
 *                              the pin count value.
 *                              SKL_DIR_IN = 0, SKL_DIR_OUT = 1.
 *                              The input and output formats
 *                              share the same set of tokens
 *                              with the distinction between input
 *                              and output made by reading direction
 *                              token.
 *
 * %SKL_TKN_U32_FMT_CH:         Supported channel count
 *
 * %SKL_TKN_U32_FMT_FREQ:       Supported frequency/sample rate
 *
 * %SKL_TKN_U32_FMT_BIT_DEPTH:  Supported container size
 *
 * %SKL_TKN_U32_FMT_SAMPLE_SIZE:Number of samples in the container
 *
 * %SKL_TKN_U32_FMT_CH_CONFIG:  Supported channel configurations for the
 *                              input/output.
 *
 * %SKL_TKN_U32_FMT_INTERLEAVE: Interleaving style which can be per
 *                              channel or per sample. The values can be :
 *                              SKL_INTERLEAVING_PER_CHANNEL = 0,
 *                              SKL_INTERLEAVING_PER_SAMPLE = 1,
 *
 * %SKL_TKN_U32_FMT_SAMPLE_TYPE:
 *                              Specifies the sample type. Can take the
 *                              values: SKL_SAMPLE_TYPE_INT_MSB = 0,
 *                              SKL_SAMPLE_TYPE_INT_LSB = 1,
 *                              SKL_SAMPLE_TYPE_INT_SIGNED = 2,
 *                              SKL_SAMPLE_TYPE_INT_UNSIGNED = 3,
 *                              SKL_SAMPLE_TYPE_FLOAT = 4
 *
 * %SKL_TKN_U32_CH_MAP:         Channel map values
 * %SKL_TKN_U32_MOD_SET_PARAMS: It can take these values:
 *                              SKL_PARAM_DEFAULT, SKL_PARAM_INIT,
 *                              SKL_PARAM_SET, SKL_PARAM_BIND
 *
 * %SKL_TKN_U32_MOD_PARAM_ID:   ID of the module params
 *
 * %SKL_TKN_U32_CAPS_SET_PARAMS:
 *                              Set params value
 *
 * %SKL_TKN_U32_CAPS_PARAMS_ID: Params ID
 *
 * %SKL_TKN_U32_CAPS_SIZE:      Caps size
 *
 * %SKL_TKN_U32_PROC_DOMAIN:    Specify processing domain
 *
 * %SKL_TKN_U32_LIB_COUNT:      Specifies the number of libraries
 *
 * %SKL_TKN_STR_LIB_NAME:       Specifies the library name
 *
 * %SKL_TKN_U32_PMODE:		Specifies the power mode for pipe
 *
 * %SKL_TKL_U32_D0I3_CAPS:	Specifies the D0i3 capability for module
 *
 * %SKL_TKN_U32_DMA_BUF_SIZE:	DMA buffer size in millisec
 *
 * %SKL_TKN_U32_PIPE_DIR:       Specifies pipe direction. Can be
 *                              playback/capture.
 *
 * %SKL_TKN_U32_NUM_CONFIGS:    Number of pipe configs
 *
 * %SKL_TKN_U32_PATH_MEM_PGS:   Size of memory (in pages) required for pipeline
 *                              and its data
 *
 * %SKL_TKN_U32_PIPE_CONFIG_ID: Config id for the modules in the pipe
 *                              and PCM params supported by that pipe
 *                              config. This is used as index to fill
 *                              up the pipe config and module config
 *                              structure.
 *
 * %SKL_TKN_U32_CFG_FREQ:
 * %SKL_TKN_U8_CFG_CHAN:
 * %SKL_TKN_U8_CFG_BPS:         PCM params (freq, channels, bits per sample)
 *                              supported for each of the pipe configs.
 *
 * %SKL_TKN_CFG_MOD_RES_ID:     Module's resource index for each of the
 *                              pipe config
 *
 * %SKL_TKN_CFG_MOD_FMT_ID:     Module's interface index for each of the
 *                              pipe config
 *
 * %SKL_TKN_U8_NUM_MOD:         Number of modules in the manifest
 *
 * %SKL_TKN_MM_U8_MOD_IDX:      Current index of the module in the manifest
 *
 * %SKL_TKN_MM_U8_NUM_RES:      Number of resources for the module
 *
 * %SKL_TKN_MM_U8_NUM_INTF:     Number of interfaces for the module
 *
 * %SKL_TKN_MM_U32_RES_ID:      Resource index for the resource info to
 *                              be filled into.
 *                              A module can support multiple resource
 *                              configuration and is represnted as a
 *                              resource table. This index is used to
 *                              fill information into appropriate index.
 *
 * %SKL_TKN_MM_U32_CPS:         DSP cycles per second
 *
 * %SKL_TKN_MM_U32_DMA_SIZE:    Allocated buffer size for gateway DMA
 *
 * %SKL_TKN_MM_U32_CPC:         DSP cycles allocated per frame
 *
 * %SKL_TKN_MM_U32_RES_PIN_ID:  Resource pin index in the module
 *
 * %SKL_TKN_MM_U32_INTF_PIN_ID: Interface index in the module
 *
 * %SKL_TKN_MM_U32_PIN_BUF:     Buffer size of the module pin
 *
 * %SKL_TKN_MM_U32_FMT_ID:      Format index for each of the interface/
 *                              format information to be filled into.
 *
 * %SKL_TKN_MM_U32_NUM_IN_FMT:  Number of input formats
 * %SKL_TKN_MM_U32_NUM_OUT_FMT: Number of output formats
 *
 * %SKL_TKN_U32_ASTATE_IDX:     Table Index for the A-State entry to be filled
 *                              with kcps and clock source
 *
 * %SKL_TKN_U32_ASTATE_COUNT:   Number of valid entries in A-State table
 *
 * %SKL_TKN_U32_ASTATE_KCPS:    Specifies the core load threshold (in kilo
 *                              cycles per second) below which DSP is clocked
 *                              from source specified by clock source.
 *
 * %SKL_TKN_U32_ASTATE_CLK_SRC: Clock source for A-State entry
 *
 * %SKL_TKN_U32_FMT_CFG_IDX:    Format config index
 *
 * module_id and loadable flags dont have tokens as these values will be
 * read from the DSP FW manifest
 *
 * Tokens defined can be used either in the manifest or widget private data.
 *
 * SKL_TKN_MM is used as a suffix for all tokens that represent
 * module data in the manifest.
 */
enum SKL_TKNS {
	SKL_TKN_UUID = 1,
	SKL_TKN_U8_NUM_BLOCKS,
	SKL_TKN_U8_BLOCK_TYPE,
	SKL_TKN_U8_IN_PIN_TYPE,
	SKL_TKN_U8_OUT_PIN_TYPE,
	SKL_TKN_U8_DYN_IN_PIN,
	SKL_TKN_U8_DYN_OUT_PIN,
	SKL_TKN_U8_IN_QUEUE_COUNT,
	SKL_TKN_U8_OUT_QUEUE_COUNT,
	SKL_TKN_U8_TIME_SLOT,
	SKL_TKN_U8_CORE_ID,
	SKL_TKN_U8_MOD_TYPE,
	SKL_TKN_U8_CONN_TYPE,
	SKL_TKN_U8_DEV_TYPE,
	SKL_TKN_U8_HW_CONN_TYPE,
	SKL_TKN_U16_MOD_INST_ID,
	SKL_TKN_U16_BLOCK_SIZE,
	SKL_TKN_U32_MAX_MCPS,
	SKL_TKN_U32_MEM_PAGES,
	SKL_TKN_U32_OBS,
	SKL_TKN_U32_IBS,
	SKL_TKN_U32_VBUS_ID,
	SKL_TKN_U32_PARAMS_FIXUP,
	SKL_TKN_U32_CONVERTER,
	SKL_TKN_U32_PIPE_ID,
	SKL_TKN_U32_PIPE_CONN_TYPE,
	SKL_TKN_U32_PIPE_PRIORITY,
	SKL_TKN_U32_PIPE_MEM_PGS,
	SKL_TKN_U32_DIR_PIN_COUNT,
	SKL_TKN_U32_FMT_CH,
	SKL_TKN_U32_FMT_FREQ,
	SKL_TKN_U32_FMT_BIT_DEPTH,
	SKL_TKN_U32_FMT_SAMPLE_SIZE,
	SKL_TKN_U32_FMT_CH_CONFIG,
	SKL_TKN_U32_FMT_INTERLEAVE,
	SKL_TKN_U32_FMT_SAMPLE_TYPE,
	SKL_TKN_U32_FMT_CH_MAP,
	SKL_TKN_U32_PIN_MOD_ID,
	SKL_TKN_U32_PIN_INST_ID,
	SKL_TKN_U32_MOD_SET_PARAMS,
	SKL_TKN_U32_MOD_PARAM_ID,
	SKL_TKN_U32_CAPS_SET_PARAMS,
	SKL_TKN_U32_CAPS_PARAMS_ID,
	SKL_TKN_U32_CAPS_SIZE,
	SKL_TKN_U32_PROC_DOMAIN,
	SKL_TKN_U32_LIB_COUNT,
	SKL_TKN_STR_LIB_NAME,
	SKL_TKN_U32_PMODE,
	SKL_TKL_U32_D0I3_CAPS, /* Typo added at v4.10 */
	SKL_TKN_U32_D0I3_CAPS = SKL_TKL_U32_D0I3_CAPS,
	SKL_TKN_U32_DMA_BUF_SIZE,

	SKL_TKN_U32_PIPE_DIRECTION,
	SKL_TKN_U32_PIPE_CONFIG_ID,
	SKL_TKN_U32_NUM_CONFIGS,
	SKL_TKN_U32_PATH_MEM_PGS,

	SKL_TKN_U32_CFG_FREQ,
	SKL_TKN_U8_CFG_CHAN,
	SKL_TKN_U8_CFG_BPS,
	SKL_TKN_CFG_MOD_RES_ID,
	SKL_TKN_CFG_MOD_FMT_ID,
	SKL_TKN_U8_NUM_MOD,

	SKL_TKN_MM_U8_MOD_IDX,
	SKL_TKN_MM_U8_NUM_RES,
	SKL_TKN_MM_U8_NUM_INTF,
	SKL_TKN_MM_U32_RES_ID,
	SKL_TKN_MM_U32_CPS,
	SKL_TKN_MM_U32_DMA_SIZE,
	SKL_TKN_MM_U32_CPC,
	SKL_TKN_MM_U32_RES_PIN_ID,
	SKL_TKN_MM_U32_INTF_PIN_ID,
	SKL_TKN_MM_U32_PIN_BUF,
	SKL_TKN_MM_U32_FMT_ID,
	SKL_TKN_MM_U32_NUM_IN_FMT,
	SKL_TKN_MM_U32_NUM_OUT_FMT,

	SKL_TKN_U32_ASTATE_IDX,
	SKL_TKN_U32_ASTATE_COUNT,
	SKL_TKN_U32_ASTATE_KCPS,
	SKL_TKN_U32_ASTATE_CLK_SRC,

	SKL_TKN_U32_FMT_CFG_IDX = 96,
	SKL_TKN_MAX = SKL_TKN_U32_FMT_CFG_IDX,
};

#endif
