/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Lin Huang <hl@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#ifndef __SOC_ROCKCHIP_SIP_H
#define __SOC_ROCKCHIP_SIP_H

#define ROCKCHIP_SIP_DRAM_FREQ			0x82000008
#define ROCKCHIP_SIP_CONFIG_DRAM_INIT		0x00
#define ROCKCHIP_SIP_CONFIG_DRAM_SET_RATE	0x01
#define ROCKCHIP_SIP_CONFIG_DRAM_ROUND_RATE	0x02
#define ROCKCHIP_SIP_CONFIG_DRAM_SET_AT_SR	0x03
#define ROCKCHIP_SIP_CONFIG_DRAM_GET_BW		0x04
#define ROCKCHIP_SIP_CONFIG_DRAM_GET_RATE	0x05
#define ROCKCHIP_SIP_CONFIG_DRAM_CLR_IRQ	0x06
#define ROCKCHIP_SIP_CONFIG_DRAM_SET_PARAM	0x07
#define ROCKCHIP_SIP_CONFIG_DRAM_GET_VERSION	0x08
#define ROCKCHIP_SIP_CONFIG_DRAM_POST_SET_RATE	0x09
#define ROCKCHIP_SIP_CONFIG_DRAM_SET_MSCH_RL	0x0a

#endif
