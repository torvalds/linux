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


#ifndef __FM_HC_H
#define __FM_HC_H

#include "std_ext.h"
#include "error_ext.h"
#include "fsl_fman_kg.h"

#define __ERR_MODULE__  MODULE_FM_PCD


typedef struct t_FmHcParams {
    t_Handle        h_Fm;
    t_Handle        h_FmPcd;
    t_FmPcdHcParams params;
} t_FmHcParams;


t_Handle    FmHcConfigAndInit(t_FmHcParams *p_FmHcParams);
void        FmHcFree(t_Handle h_FmHc);
t_Error     FmHcSetFramesDataMemory(t_Handle h_FmHc,
                                    uint8_t  memId);
t_Error     FmHcDumpRegs(t_Handle h_FmHc);

void        FmHcTxConf(t_Handle h_FmHc, t_DpaaFD *p_Fd);

t_Error     FmHcPcdKgSetScheme(t_Handle                   h_FmHc,
                               t_Handle                   h_Scheme,
                               struct fman_kg_scheme_regs *p_SchemeRegs,
                               bool                       updateCounter);
t_Error     FmHcPcdKgDeleteScheme(t_Handle h_FmHc, t_Handle h_Scheme);
t_Error     FmHcPcdCcCapwapTimeoutReassm(t_Handle h_FmHc, t_FmPcdCcCapwapReassmTimeoutParams *p_CcCapwapReassmTimeoutParams );
t_Error     FmHcPcdCcIpFragScratchPollCmd(t_Handle h_FmHc, bool fill, t_FmPcdCcFragScratchPoolCmdParams *p_FmPcdCcFragScratchPoolCmdParams);
t_Error     FmHcPcdCcTimeoutReassm(t_Handle h_FmHc, t_FmPcdCcReassmTimeoutParams *p_CcReassmTimeoutParams, uint8_t *p_Result);
t_Error     FmHcPcdKgSetClsPlan(t_Handle h_FmHc, t_FmPcdKgInterModuleClsPlanSet *p_Set);
t_Error     FmHcPcdKgDeleteClsPlan(t_Handle h_FmHc, uint8_t clsPlanGrpId);

t_Error     FmHcPcdKgSetSchemeCounter(t_Handle h_FmHc, t_Handle h_Scheme, uint32_t value);
uint32_t    FmHcPcdKgGetSchemeCounter(t_Handle h_FmHc, t_Handle h_Scheme);

t_Error     FmHcPcdCcDoDynamicChange(t_Handle h_FmHc, uint32_t oldAdAddrOffset, uint32_t newAdAddrOffset);

t_Error     FmHcPcdPlcrSetProfile(t_Handle h_FmHc, t_Handle h_Profile, t_FmPcdPlcrProfileRegs *p_PlcrRegs);
t_Error     FmHcPcdPlcrDeleteProfile(t_Handle h_FmHc, t_Handle h_Profile);

t_Error     FmHcPcdPlcrSetProfileCounter(t_Handle h_FmHc, t_Handle h_Profile, e_FmPcdPlcrProfileCounters counter, uint32_t value);
uint32_t    FmHcPcdPlcrGetProfileCounter(t_Handle h_FmHc, t_Handle h_Profile, e_FmPcdPlcrProfileCounters counter);

t_Error     FmHcKgWriteSp(t_Handle h_FmHc, uint8_t hardwarePortId, uint32_t spReg, bool add);
t_Error     FmHcKgWriteCpp(t_Handle h_FmHc, uint8_t hardwarePortId, uint32_t cppReg);

t_Error     FmHcPcdKgCcGetSetParams(t_Handle h_FmHc, t_Handle  h_Scheme, uint32_t requiredAction, uint32_t value);
t_Error     FmHcPcdPlcrCcGetSetParams(t_Handle h_FmHc,uint16_t absoluteProfileId, uint32_t requiredAction);

t_Error     FmHcPcdSync(t_Handle h_FmHc);
t_Handle    FmHcGetPort(t_Handle h_FmHc);




#endif /* __FM_HC_H */
