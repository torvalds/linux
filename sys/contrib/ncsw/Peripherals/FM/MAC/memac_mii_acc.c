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
#include "memac.h"
#include "xx_ext.h"

#include "fm_common.h"
#include "memac_mii_acc.h"


/*****************************************************************************/
t_Error MEMAC_MII_WritePhyReg(t_Handle  h_Memac,
                             uint8_t    phyAddr,
                             uint8_t    reg,
                             uint16_t   data)
{
    t_Memac                 *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Memac->p_MiiMemMap, E_INVALID_HANDLE);

    return (t_Error)fman_memac_mii_write_phy_reg(p_Memac->p_MiiMemMap,
                                                 phyAddr,
                                                 reg,
                                                 data,
                                                 (enum enet_speed)ENET_SPEED_FROM_MODE(p_Memac->enetMode));
}

/*****************************************************************************/
t_Error MEMAC_MII_ReadPhyReg(t_Handle h_Memac,
                            uint8_t   phyAddr,
                            uint8_t   reg,
                            uint16_t  *p_Data)
{
    t_Memac                 *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Memac->p_MiiMemMap, E_INVALID_HANDLE);

    return fman_memac_mii_read_phy_reg(p_Memac->p_MiiMemMap,
                                       phyAddr,
                                       reg,
                                       p_Data,
                                       (enum enet_speed)ENET_SPEED_FROM_MODE(p_Memac->enetMode));
}
