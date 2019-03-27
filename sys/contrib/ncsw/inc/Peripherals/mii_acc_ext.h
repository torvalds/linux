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



#ifndef __MII_ACC_EXT_H
#define __MII_ACC_EXT_H


/**************************************************************************//**
 @Function      MII_ReadPhyReg

 @Description   This routine is called to read a specified PHY
                register value.

 @Param[in]     h_MiiAccess - Handle to MII configuration access registers
 @Param[in]     phyAddr     - PHY address (0-31).
 @Param[in]     reg         - PHY register to read
 @Param[out]    p_Data      - Gets the register value.

 @Return        Always zero (success).
*//***************************************************************************/
int MII_ReadPhyReg(t_Handle h_MiiAccess,
                   uint8_t  phyAddr,
                   uint8_t  reg,
                   uint16_t *p_Data);

/**************************************************************************//**
 @Function      MII_WritePhyReg

 @Description   This routine is called to write data to a specified PHY
                   register.

 @Param[in]     h_MiiAccess - Handle to MII configuration access registers
 @Param[in]     phyAddr     - PHY address (0-31).
 @Param[in]     reg         - PHY register to write
 @Param[in]     data        - Data to write in register.

 @Return        Always zero (success).
*//***************************************************************************/
int MII_WritePhyReg(t_Handle    h_MiiAccess,
                    uint8_t     phyAddr,
                    uint8_t     reg,
                    uint16_t    data);


#endif /* __MII_ACC_EXT_H */
