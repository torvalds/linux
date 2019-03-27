/*
 * Copyright 2008-2013 Freescale Semiconductor Inc.
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


/******************************************************************************
 @File          dtsec_mii_acc.c

 @Description   FM dtsec MII register access MAC ...
*//***************************************************************************/

#include "error_ext.h"
#include "std_ext.h"
#include "fm_mac.h"
#include "dtsec.h"
#include "fsl_fman_dtsec_mii_acc.h"


/*****************************************************************************/
t_Error DTSEC_MII_WritePhyReg(t_Handle    h_Dtsec,
                              uint8_t     phyAddr,
                              uint8_t     reg,
                              uint16_t    data)
{
    t_Dtsec              *p_Dtsec = (t_Dtsec *)h_Dtsec;
    struct dtsec_mii_reg *miiregs;
    uint16_t              dtsec_freq;
    t_Error                   err;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Dtsec->p_MiiMemMap, E_INVALID_HANDLE);

    dtsec_freq = (uint16_t)(p_Dtsec->fmMacControllerDriver.clkFreq >> 1);
    miiregs = p_Dtsec->p_MiiMemMap;

    err = (t_Error)fman_dtsec_mii_write_reg(miiregs, phyAddr, reg, data, dtsec_freq);

    return err;
}

/*****************************************************************************/
t_Error DTSEC_MII_ReadPhyReg(t_Handle h_Dtsec,
                             uint8_t  phyAddr,
                             uint8_t  reg,
                             uint16_t *p_Data)
{
    t_Dtsec               *p_Dtsec = (t_Dtsec *)h_Dtsec;
    struct dtsec_mii_reg  *miiregs;
    uint16_t               dtsec_freq;
    t_Error                    err;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Dtsec->p_MiiMemMap, E_INVALID_HANDLE);

    dtsec_freq = (uint16_t)(p_Dtsec->fmMacControllerDriver.clkFreq >> 1);
    miiregs = p_Dtsec->p_MiiMemMap;

    err = fman_dtsec_mii_read_reg(miiregs, phyAddr, reg, p_Data, dtsec_freq);

    if (*p_Data == 0xffff)
        RETURN_ERROR(MINOR, E_NO_DEVICE,
                     ("Read wrong data (0xffff): phyAddr 0x%x, reg 0x%x",
                      phyAddr, reg));
    if (err)
        RETURN_ERROR(MINOR, (t_Error)err, NO_MSG);

    return E_OK;
}

