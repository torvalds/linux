/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef __MEMAC_MII_ACC_H
#define __MEMAC_MII_ACC_H

#include "std_ext.h"


/* MII Management Registers */
#define MDIO_CFG_CLK_DIV_MASK       0x0080ff80
#define MDIO_CFG_CLK_DIV_SHIFT      7
#define MDIO_CFG_HOLD_MASK          0x0000001c
#define MDIO_CFG_ENC45              0x00000040
#define MDIO_CFG_READ_ERR           0x00000002
#define MDIO_CFG_BSY                0x00000001

#define MDIO_CTL_PHY_ADDR_SHIFT     5
#define MDIO_CTL_READ               0x00008000

#define MDIO_DATA_BSY               0x80000000

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */

/*----------------------------------------------------*/
/* MII Configuration Control Memory Map Registers     */
/*----------------------------------------------------*/
typedef struct t_MemacMiiAccessMemMap
{
    volatile uint32_t   mdio_cfg;       /* 0x030  */
    volatile uint32_t   mdio_ctrl;      /* 0x034  */
    volatile uint32_t   mdio_data;      /* 0x038  */
    volatile uint32_t   mdio_addr;      /* 0x03c  */
} t_MemacMiiAccessMemMap ;

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */


#endif /* __MEMAC_MII_ACC_H */
