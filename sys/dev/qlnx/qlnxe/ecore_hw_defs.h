/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef _ECORE_IGU_DEF_H_
#define _ECORE_IGU_DEF_H_

/* Fields of IGU PF CONFIGRATION REGISTER */
#define IGU_PF_CONF_FUNC_EN       (0x1<<0)  /* function enable        */
#define IGU_PF_CONF_MSI_MSIX_EN   (0x1<<1)  /* MSI/MSIX enable        */
#define IGU_PF_CONF_INT_LINE_EN   (0x1<<2)  /* INT enable             */
#define IGU_PF_CONF_ATTN_BIT_EN   (0x1<<3)  /* attention enable       */
#define IGU_PF_CONF_SINGLE_ISR_EN (0x1<<4)  /* single ISR mode enable */
#define IGU_PF_CONF_SIMD_MODE     (0x1<<5)  /* simd all ones mode     */

/* Fields of IGU VF CONFIGRATION REGISTER */
#define IGU_VF_CONF_FUNC_EN        (0x1<<0)  /* function enable        */
#define IGU_VF_CONF_MSI_MSIX_EN    (0x1<<1)  /* MSI/MSIX enable        */
#define IGU_VF_CONF_SINGLE_ISR_EN  (0x1<<4)  /* single ISR mode enable */
#define IGU_VF_CONF_PARENT_MASK    (0xF)     /* Parent PF              */
#define IGU_VF_CONF_PARENT_SHIFT   5         /* Parent PF              */

/* Igu control commands
 */
enum igu_ctrl_cmd
{
	IGU_CTRL_CMD_TYPE_RD,
	IGU_CTRL_CMD_TYPE_WR,
	MAX_IGU_CTRL_CMD
};

/* Control register for the IGU command register
 */
struct igu_ctrl_reg
{
	u32 ctrl_data;
#define IGU_CTRL_REG_FID_MASK		0xFFFF /* Opaque_FID	 */
#define IGU_CTRL_REG_FID_SHIFT		0
#define IGU_CTRL_REG_PXP_ADDR_MASK	0xFFF /* Command address */
#define IGU_CTRL_REG_PXP_ADDR_SHIFT	16
#define IGU_CTRL_REG_RESERVED_MASK	0x1
#define IGU_CTRL_REG_RESERVED_SHIFT	28
#define IGU_CTRL_REG_TYPE_MASK		0x1 /* use enum igu_ctrl_cmd */
#define IGU_CTRL_REG_TYPE_SHIFT		31
};

#endif
