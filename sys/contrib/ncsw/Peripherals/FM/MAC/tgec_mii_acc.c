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



#include "error_ext.h"
#include "std_ext.h"
#include "fm_mac.h"
#include "tgec.h"
#include "xx_ext.h"

#include "fm_common.h"


/*****************************************************************************/
t_Error TGEC_MII_WritePhyReg(t_Handle   h_Tgec,
                             uint8_t    phyAddr,
                             uint8_t    reg,
                             uint16_t   data)
{
    t_Tgec                  *p_Tgec = (t_Tgec *)h_Tgec;
    t_TgecMiiAccessMemMap   *p_MiiAccess;
    uint32_t                cfgStatusReg;

    SANITY_CHECK_RETURN_ERROR(p_Tgec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Tgec->p_MiiMemMap, E_INVALID_HANDLE);

    p_MiiAccess = p_Tgec->p_MiiMemMap;

    /* Configure MII */
    cfgStatusReg  = GET_UINT32(p_MiiAccess->mdio_cfg_status);
    cfgStatusReg &= ~MIIMCOM_DIV_MASK;
     /* (one half of fm clock => 2.5Mhz) */
    cfgStatusReg |=((((p_Tgec->fmMacControllerDriver.clkFreq*10)/2)/25) << MIIMCOM_DIV_SHIFT);
    WRITE_UINT32(p_MiiAccess->mdio_cfg_status, cfgStatusReg);

    while ((GET_UINT32(p_MiiAccess->mdio_cfg_status)) & MIIMIND_BUSY)
        XX_UDelay (1);

    WRITE_UINT32(p_MiiAccess->mdio_command, phyAddr);

    WRITE_UINT32(p_MiiAccess->mdio_regaddr, reg);

    CORE_MemoryBarrier();

    while ((GET_UINT32(p_MiiAccess->mdio_cfg_status)) & MIIMIND_BUSY)
        XX_UDelay (1);

    WRITE_UINT32(p_MiiAccess->mdio_data, data);

    CORE_MemoryBarrier();

    while ((GET_UINT32(p_MiiAccess->mdio_data)) & MIIDATA_BUSY)
        XX_UDelay (1);

    return E_OK;
}

/*****************************************************************************/
t_Error TGEC_MII_ReadPhyReg(t_Handle h_Tgec,
                            uint8_t  phyAddr,
                            uint8_t  reg,
                            uint16_t *p_Data)
{
    t_Tgec                  *p_Tgec = (t_Tgec *)h_Tgec;
    t_TgecMiiAccessMemMap   *p_MiiAccess;
    uint32_t                cfgStatusReg;

    SANITY_CHECK_RETURN_ERROR(p_Tgec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Tgec->p_MiiMemMap, E_INVALID_HANDLE);

    p_MiiAccess = p_Tgec->p_MiiMemMap;

    /* Configure MII */
    cfgStatusReg  = GET_UINT32(p_MiiAccess->mdio_cfg_status);
    cfgStatusReg &= ~MIIMCOM_DIV_MASK;
     /* (one half of fm clock => 2.5Mhz) */
    cfgStatusReg |=((((p_Tgec->fmMacControllerDriver.clkFreq*10)/2)/25) << MIIMCOM_DIV_SHIFT);
    WRITE_UINT32(p_MiiAccess->mdio_cfg_status, cfgStatusReg);

    while ((GET_UINT32(p_MiiAccess->mdio_cfg_status)) & MIIMIND_BUSY)
        XX_UDelay (1);

    WRITE_UINT32(p_MiiAccess->mdio_command, phyAddr);

    WRITE_UINT32(p_MiiAccess->mdio_regaddr, reg);

    CORE_MemoryBarrier();

    while ((GET_UINT32(p_MiiAccess->mdio_cfg_status)) & MIIMIND_BUSY)
        XX_UDelay (1);

    WRITE_UINT32(p_MiiAccess->mdio_command, (uint32_t)(phyAddr | MIIMCOM_READ_CYCLE));

    CORE_MemoryBarrier();

    while ((GET_UINT32(p_MiiAccess->mdio_data)) & MIIDATA_BUSY)
        XX_UDelay (1);

    *p_Data =  (uint16_t)GET_UINT32(p_MiiAccess->mdio_data);

    cfgStatusReg  = GET_UINT32(p_MiiAccess->mdio_cfg_status);

    if (cfgStatusReg & MIIMIND_READ_ERROR)
        RETURN_ERROR(MINOR, E_INVALID_VALUE,
                     ("Read Error: phyAddr 0x%x, dev 0x%x, reg 0x%x, cfgStatusReg 0x%x",
                      ((phyAddr & 0xe0)>>5), (phyAddr & 0x1f), reg, cfgStatusReg));

    return E_OK;
}
