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


/******************************************************************************
 @File          fm_plcr.c

 @Description   FM PCD POLICER...
*//***************************************************************************/
#include <linux/math64.h>
#include "std_ext.h"
#include "error_ext.h"
#include "string_ext.h"
#include "debug_ext.h"
#include "net_ext.h"
#include "fm_ext.h"

#include "fm_common.h"
#include "fm_pcd.h"
#include "fm_hc.h"
#include "fm_pcd_ipc.h"
#include "fm_plcr.h"


/****************************************/
/*       static functions               */
/****************************************/

static uint32_t PlcrProfileLock(t_Handle h_Profile)
{
    ASSERT_COND(h_Profile);
    return FmPcdLockSpinlock(((t_FmPcdPlcrProfile *)h_Profile)->p_Lock);
}

static void PlcrProfileUnlock(t_Handle h_Profile, uint32_t intFlags)
{
    ASSERT_COND(h_Profile);
    FmPcdUnlockSpinlock(((t_FmPcdPlcrProfile *)h_Profile)->p_Lock, intFlags);
}

static bool PlcrProfileFlagTryLock(t_Handle h_Profile)
{
    ASSERT_COND(h_Profile);
    return FmPcdLockTryLock(((t_FmPcdPlcrProfile *)h_Profile)->p_Lock);
}

static void PlcrProfileFlagUnlock(t_Handle h_Profile)
{
    ASSERT_COND(h_Profile);
    FmPcdLockUnlock(((t_FmPcdPlcrProfile *)h_Profile)->p_Lock);
}

static uint32_t PlcrHwLock(t_Handle h_FmPcdPlcr)
{
    ASSERT_COND(h_FmPcdPlcr);
    return XX_LockIntrSpinlock(((t_FmPcdPlcr*)h_FmPcdPlcr)->h_HwSpinlock);
}

static void PlcrHwUnlock(t_Handle h_FmPcdPlcr, uint32_t intFlags)
{
    ASSERT_COND(h_FmPcdPlcr);
    XX_UnlockIntrSpinlock(((t_FmPcdPlcr*)h_FmPcdPlcr)->h_HwSpinlock, intFlags);
}

static uint32_t PlcrSwLock(t_Handle h_FmPcdPlcr)
{
    ASSERT_COND(h_FmPcdPlcr);
    return XX_LockIntrSpinlock(((t_FmPcdPlcr*)h_FmPcdPlcr)->h_SwSpinlock);
}

static void PlcrSwUnlock(t_Handle h_FmPcdPlcr, uint32_t intFlags)
{
    ASSERT_COND(h_FmPcdPlcr);
    XX_UnlockIntrSpinlock(((t_FmPcdPlcr*)h_FmPcdPlcr)->h_SwSpinlock, intFlags);
}

static bool IsProfileShared(t_Handle h_FmPcd, uint16_t absoluteProfileId)
{
    t_FmPcd         *p_FmPcd = (t_FmPcd*)h_FmPcd;
    uint16_t        i;

    SANITY_CHECK_RETURN_VALUE(p_FmPcd, E_INVALID_HANDLE, FALSE);

    for (i=0;i<p_FmPcd->p_FmPcdPlcr->numOfSharedProfiles;i++)
        if (p_FmPcd->p_FmPcdPlcr->sharedProfilesIds[i] == absoluteProfileId)
            return TRUE;
    return FALSE;
}

static t_Error SetProfileNia(t_FmPcd *p_FmPcd, e_FmPcdEngine nextEngine, u_FmPcdPlcrNextEngineParams *p_NextEngineParams, uint32_t *nextAction)
{
    uint32_t    nia;
    uint16_t    absoluteProfileId;
    uint8_t     relativeSchemeId, physicalSchemeId;

    nia = FM_PCD_PLCR_NIA_VALID;

    switch (nextEngine)
    {
        case e_FM_PCD_DONE :
            switch (p_NextEngineParams->action)
            {
                case e_FM_PCD_DROP_FRAME :
                    nia |= GET_NIA_BMI_AC_DISCARD_FRAME(p_FmPcd);
                    break;
                case e_FM_PCD_ENQ_FRAME:
                    nia |= GET_NIA_BMI_AC_ENQ_FRAME(p_FmPcd);
                    break;
                default:
                    RETURN_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
            }
            break;
        case e_FM_PCD_KG:
            physicalSchemeId = FmPcdKgGetSchemeId(p_NextEngineParams->h_DirectScheme);
            relativeSchemeId = FmPcdKgGetRelativeSchemeId(p_FmPcd, physicalSchemeId);
            if (relativeSchemeId >= FM_PCD_KG_NUM_OF_SCHEMES)
                RETURN_ERROR(MAJOR, E_NOT_IN_RANGE, NO_MSG);
            if (!FmPcdKgIsSchemeValidSw(p_NextEngineParams->h_DirectScheme))
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Invalid direct scheme."));
            if (!KgIsSchemeAlwaysDirect(p_FmPcd, relativeSchemeId))
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Policer Profile may point only to a scheme that is always direct."));
            nia |= NIA_ENG_KG | NIA_KG_DIRECT | physicalSchemeId;
            break;
        case e_FM_PCD_PLCR:
            absoluteProfileId = ((t_FmPcdPlcrProfile *)p_NextEngineParams->h_Profile)->absoluteProfileId;
            if (!IsProfileShared(p_FmPcd, absoluteProfileId))
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Next profile must be a shared profile"));
            if (!FmPcdPlcrIsProfileValid(p_FmPcd, absoluteProfileId))
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Invalid profile "));
            nia |= NIA_ENG_PLCR | NIA_PLCR_ABSOLUTE | absoluteProfileId;
            break;
        default:
            RETURN_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
    }

    *nextAction =  nia;

    return E_OK;
}

static uint32_t CalcFPP(uint32_t fpp)
{
    if (fpp > 15)
        return 15 - (0x1f - fpp);
    else
        return 16 + fpp;
}

static void GetInfoRateReg(e_FmPcdPlcrRateMode  rateMode,
                           uint32_t             rate,
                           uint64_t             tsuInTenthNano,
                           uint32_t             fppShift,
                           uint64_t             *p_Integer,
                           uint64_t             *p_Fraction)
{
    uint64_t tmp, div;

    if (rateMode == e_FM_PCD_PLCR_BYTE_MODE)
    {
        /* now we calculate the initial integer for the bigger rate */
        /* from Kbps to Bytes/TSU */
        tmp = (uint64_t)rate;
        tmp *= 1000; /* kb --> b */
        tmp *= tsuInTenthNano; /* bps --> bpTsu(in 10nano) */

        div = 1000000000;   /* nano */
        div *= 10;          /* 10 nano */
        div *= 8;           /* bit to byte */
    }
    else
    {
        /* now we calculate the initial integer for the bigger rate */
        /* from Kbps to Bytes/TSU */
        tmp = (uint64_t)rate;
        tmp *= tsuInTenthNano; /* bps --> bpTsu(in 10nano) */

        div = 1000000000;   /* nano */
        div *= 10;          /* 10 nano */
    }
    *p_Integer = (tmp<<fppShift) / div;

    /* for calculating the fraction, we will recalculate cir and deduct the integer.
     * For precision, we will multiply by 2^16. we do not divid back, since we write
     * this value as fraction - see spec.
     */
    *p_Fraction = (((tmp<<fppShift)<<16) - ((*p_Integer<<16)*div)) / div;
}

/* .......... */

static void CalcRates(uint32_t                              bitFor1Micro,
                      t_FmPcdPlcrNonPassthroughAlgParams    *p_NonPassthroughAlgParam,
                      uint32_t                              *cir,
                      uint32_t                              *cbs,
                      uint32_t                              *pir_eir,
                      uint32_t                              *pbs_ebs,
                      uint32_t                              *fpp)
{
    uint64_t    integer, fraction;
    uint32_t    temp, tsuInTenthNanos;
    uint8_t     fppShift=0;

    /* we want the tsu to count 10 nano for better precision normally tsu is 3.9 nano, now we will get 39 */
    tsuInTenthNanos = (uint32_t)(1000*10/(1 << bitFor1Micro));

    /* we choose the faster rate to calibrate fpp */
    /* The meaning of this step:
     * when fppShift is 0 it means all TS bits are treated as integer and TSU is the TS LSB count.
     * In this configuration we calculate the integer and fraction that represent the higher infoRate
     * When this is done, we can tell where we have "spare" unused bits and optimize the division of TS
     * into "integer" and "fraction" where the logic is - as many bits as possible for integer at
     * high rate, as many bits as possible for fraction at low rate.
     */
    if (p_NonPassthroughAlgParam->committedInfoRate > p_NonPassthroughAlgParam->peakOrExcessInfoRate)
        GetInfoRateReg(p_NonPassthroughAlgParam->rateMode, p_NonPassthroughAlgParam->committedInfoRate, tsuInTenthNanos, 0, &integer, &fraction);
    else
        GetInfoRateReg(p_NonPassthroughAlgParam->rateMode, p_NonPassthroughAlgParam->peakOrExcessInfoRate, tsuInTenthNanos, 0, &integer, &fraction);

    /* we shift integer, as in cir/pir it is represented by the MSB 16 bits, and
     * the LSB bits are for the fraction */
    temp = (uint32_t)((integer<<16) & 0x00000000FFFFFFFF);
    /* temp is effected by the rate. For low rates it may be as low as 0, and then we'll
     * take max FP = 31.
     * For high rates it will never exceed the 32 bit reg (after the 16 shift), as it is
     * limited by the 10G physical port.
     */
    if (temp != 0)
    {
        /* In this case, the largest rate integer is non 0, if it does not occupy all (high) 16
         * bits of the PIR_EIR we can use this fact and enlarge it to occupy all 16 bits.
         * The logic is to have as many bits for integer in the higher rates, but if we have "0"s
         * in the integer part of the cir/pir register, than these bits are wasted. So we want
         * to use these bits for the fraction. in this way we will have for fraction - the number
         * of "0" bits and the rest - for integer.
         * In other words: For each bit we shift it in PIR_EIR, we move the FP in the TS
         * one bit to the left - preserving the relationship and achieving more bits
         * for integer in the TS.
         */

        /* count zeroes left of the higher used bit (in order to shift the value such that
         * unused bits may be used for fraction).
         */
        while ((temp & 0x80000000) == 0)
        {
            temp = temp << 1;
            fppShift++;
        }
        if (fppShift > 15)
        {
            REPORT_ERROR(MAJOR, E_INVALID_SELECTION, ("timeStampPeriod to Information rate ratio is too small"));
            return;
        }
    }
    else
    {
        temp = (uint32_t)fraction; /* fraction will alyas be smaller than 2^16 */
        if (!temp)
            /* integer and fraction are 0, we set FP to its max val */
            fppShift = 31;
        else
        {
            /* integer was 0 but fraction is not. FP is 16 for the fraction,
             * + all left zeroes of the fraction. */
            fppShift=16;
            /* count zeroes left of the higher used bit (in order to shift the value such that
             * unused bits may be used for fraction).
             */
            while ((temp & 0x8000) == 0)
            {
                temp = temp << 1;
                fppShift++;
            }
        }
    }

    /*
     * This means that the FM TS register will now be used so that 'fppShift' bits are for
     * fraction and the rest for integer */
    /* now we re-calculate cir and pir_eir with the calculated FP */
    GetInfoRateReg(p_NonPassthroughAlgParam->rateMode, p_NonPassthroughAlgParam->committedInfoRate, tsuInTenthNanos, fppShift, &integer, &fraction);
    *cir = (uint32_t)(integer << 16 | (fraction & 0xFFFF));
    GetInfoRateReg(p_NonPassthroughAlgParam->rateMode, p_NonPassthroughAlgParam->peakOrExcessInfoRate, tsuInTenthNanos, fppShift, &integer, &fraction);
    *pir_eir = (uint32_t)(integer << 16 | (fraction & 0xFFFF));

    *cbs     =  p_NonPassthroughAlgParam->committedBurstSize;
    *pbs_ebs =  p_NonPassthroughAlgParam->peakOrExcessBurstSize;

    /* convert FP as it should be written to reg.
     * 0-15 --> 16-31
     * 16-31 --> 0-15
     */
    *fpp = CalcFPP(fppShift);
}

static void WritePar(t_FmPcd *p_FmPcd, uint32_t par)
{
    t_FmPcdPlcrRegs *p_FmPcdPlcrRegs    = p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs;

    ASSERT_COND(FmIsMaster(p_FmPcd->h_Fm));
    WRITE_UINT32(p_FmPcdPlcrRegs->fmpl_par, par);

    while (GET_UINT32(p_FmPcdPlcrRegs->fmpl_par) & FM_PCD_PLCR_PAR_GO) ;
}

static t_Error BuildProfileRegs(t_FmPcd                     *p_FmPcd,
                                t_FmPcdPlcrProfileParams    *p_ProfileParams,
                                t_FmPcdPlcrProfileRegs      *p_PlcrRegs)
{
    t_Error                 err = E_OK;
    uint32_t                pemode, gnia, ynia, rnia, bitFor1Micro;

    ASSERT_COND(p_FmPcd);

    bitFor1Micro = FmGetTimeStampScale(p_FmPcd->h_Fm);
    if (bitFor1Micro == 0)
    RETURN_ERROR(MAJOR, E_NOT_AVAILABLE, ("Timestamp scale"));

/* Set G, Y, R Nia */
    err = SetProfileNia(p_FmPcd, p_ProfileParams->nextEngineOnGreen,  &(p_ProfileParams->paramsOnGreen), &gnia);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);
    err = SetProfileNia(p_FmPcd, p_ProfileParams->nextEngineOnYellow, &(p_ProfileParams->paramsOnYellow), &ynia);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);
    err = SetProfileNia(p_FmPcd, p_ProfileParams->nextEngineOnRed,    &(p_ProfileParams->paramsOnRed), &rnia);
   if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

/* Mode fmpl_pemode */
    pemode = FM_PCD_PLCR_PEMODE_PI;

    switch (p_ProfileParams->algSelection)
    {
        case    e_FM_PCD_PLCR_PASS_THROUGH:
            p_PlcrRegs->fmpl_pecir         = 0;
            p_PlcrRegs->fmpl_pecbs         = 0;
            p_PlcrRegs->fmpl_pepepir_eir   = 0;
            p_PlcrRegs->fmpl_pepbs_ebs     = 0;
            p_PlcrRegs->fmpl_pelts         = 0;
            p_PlcrRegs->fmpl_pects         = 0;
            p_PlcrRegs->fmpl_pepts_ets     = 0;
            pemode &= ~FM_PCD_PLCR_PEMODE_ALG_MASK;
            switch (p_ProfileParams->colorMode)
            {
                case    e_FM_PCD_PLCR_COLOR_BLIND:
                    pemode |= FM_PCD_PLCR_PEMODE_CBLND;
                    switch (p_ProfileParams->color.dfltColor)
                    {
                        case e_FM_PCD_PLCR_GREEN:
                            pemode &= ~FM_PCD_PLCR_PEMODE_DEFC_MASK;
                            break;
                        case e_FM_PCD_PLCR_YELLOW:
                            pemode |= FM_PCD_PLCR_PEMODE_DEFC_Y;
                            break;
                        case e_FM_PCD_PLCR_RED:
                            pemode |= FM_PCD_PLCR_PEMODE_DEFC_R;
                            break;
                        case e_FM_PCD_PLCR_OVERRIDE:
                            pemode |= FM_PCD_PLCR_PEMODE_DEFC_OVERRIDE;
                            break;
                        default:
                            RETURN_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
                    }

                    break;
                case    e_FM_PCD_PLCR_COLOR_AWARE:
                    pemode &= ~FM_PCD_PLCR_PEMODE_CBLND;
                    break;
                default:
                    RETURN_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
            }
            break;

        case    e_FM_PCD_PLCR_RFC_2698:
            /* Select algorithm MODE[ALG] = "01" */
            pemode |= FM_PCD_PLCR_PEMODE_ALG_RFC2698;
            if (p_ProfileParams->nonPassthroughAlgParams.committedInfoRate > p_ProfileParams->nonPassthroughAlgParams.peakOrExcessInfoRate)
                RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("in RFC2698 Peak rate must be equal or larger than committedInfoRate."));
            goto cont_rfc;
        case    e_FM_PCD_PLCR_RFC_4115:
            /* Select algorithm MODE[ALG] = "10" */
            pemode |= FM_PCD_PLCR_PEMODE_ALG_RFC4115;
cont_rfc:
            /* Select Color-Blind / Color-Aware operation (MODE[CBLND]) */
            switch (p_ProfileParams->colorMode)
            {
                case    e_FM_PCD_PLCR_COLOR_BLIND:
                    pemode |= FM_PCD_PLCR_PEMODE_CBLND;
                    break;
                case    e_FM_PCD_PLCR_COLOR_AWARE:
                    pemode &= ~FM_PCD_PLCR_PEMODE_CBLND;
                    /*In color aware more select override color interpretation (MODE[OVCLR]) */
                    switch (p_ProfileParams->color.override)
                    {
                        case e_FM_PCD_PLCR_GREEN:
                            pemode &= ~FM_PCD_PLCR_PEMODE_OVCLR_MASK;
                            break;
                        case e_FM_PCD_PLCR_YELLOW:
                            pemode |= FM_PCD_PLCR_PEMODE_OVCLR_Y;
                            break;
                        case e_FM_PCD_PLCR_RED:
                            pemode |= FM_PCD_PLCR_PEMODE_OVCLR_R;
                            break;
                        case e_FM_PCD_PLCR_OVERRIDE:
                            pemode |= FM_PCD_PLCR_PEMODE_OVCLR_G_NC;
                            break;
                        default:
                            RETURN_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
                    }
                    break;
                default:
                    RETURN_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
            }
            /* Select Measurement Unit Mode to BYTE or PACKET (MODE[PKT]) */
            switch (p_ProfileParams->nonPassthroughAlgParams.rateMode)
            {
                case e_FM_PCD_PLCR_BYTE_MODE :
                    pemode &= ~FM_PCD_PLCR_PEMODE_PKT;
                        switch (p_ProfileParams->nonPassthroughAlgParams.byteModeParams.frameLengthSelection)
                        {
                            case e_FM_PCD_PLCR_L2_FRM_LEN:
                                pemode |= FM_PCD_PLCR_PEMODE_FLS_L2;
                                break;
                            case e_FM_PCD_PLCR_L3_FRM_LEN:
                                pemode |= FM_PCD_PLCR_PEMODE_FLS_L3;
                                break;
                            case e_FM_PCD_PLCR_L4_FRM_LEN:
                                pemode |= FM_PCD_PLCR_PEMODE_FLS_L4;
                                break;
                            case e_FM_PCD_PLCR_FULL_FRM_LEN:
                                pemode |= FM_PCD_PLCR_PEMODE_FLS_FULL;
                                break;
                            default:
                                RETURN_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
                        }
                        switch (p_ProfileParams->nonPassthroughAlgParams.byteModeParams.rollBackFrameSelection)
                        {
                            case e_FM_PCD_PLCR_ROLLBACK_L2_FRM_LEN:
                                pemode &= ~FM_PCD_PLCR_PEMODE_RBFLS;
                                break;
                            case e_FM_PCD_PLCR_ROLLBACK_FULL_FRM_LEN:
                                pemode |= FM_PCD_PLCR_PEMODE_RBFLS;
                                break;
                            default:
                                RETURN_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
                        }
                    break;
                case e_FM_PCD_PLCR_PACKET_MODE :
                    pemode |= FM_PCD_PLCR_PEMODE_PKT;
                    break;
                default:
                    RETURN_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
            }
            /* Select timeStamp floating point position (MODE[FPP]) to fit the actual traffic rates. For PACKET
               mode with low traffic rates move the fixed point to the left to increase fraction accuracy. For BYTE
               mode with high traffic rates move the fixed point to the right to increase integer accuracy. */

            /* Configure Traffic Parameters*/
            {
                uint32_t cir=0, cbs=0, pir_eir=0, pbs_ebs=0, fpp=0;

                CalcRates(bitFor1Micro, &p_ProfileParams->nonPassthroughAlgParams, &cir, &cbs, &pir_eir, &pbs_ebs, &fpp);

                /*  Set Committed Information Rate (CIR) */
                p_PlcrRegs->fmpl_pecir = cir;
                /*  Set Committed Burst Size (CBS). */
                p_PlcrRegs->fmpl_pecbs =  cbs;
                /*  Set Peak Information Rate (PIR_EIR used as PIR) */
                p_PlcrRegs->fmpl_pepepir_eir = pir_eir;
                /*   Set Peak Burst Size (PBS_EBS used as PBS) */
                p_PlcrRegs->fmpl_pepbs_ebs = pbs_ebs;

                /* Initialize the Metering Buckets to be full (write them with 0xFFFFFFFF. */
                /* Peak Rate Token Bucket Size (PTS_ETS used as PTS) */
                p_PlcrRegs->fmpl_pepts_ets = 0xFFFFFFFF;
                /* Committed Rate Token Bucket Size (CTS) */
                p_PlcrRegs->fmpl_pects = 0xFFFFFFFF;

                /* Set the FPP based on calculation */
                pemode |= (fpp << FM_PCD_PLCR_PEMODE_FPP_SHIFT);
            }
            break;  /* FM_PCD_PLCR_PEMODE_ALG_RFC2698 , FM_PCD_PLCR_PEMODE_ALG_RFC4115 */
        default:
            RETURN_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
    }

    p_PlcrRegs->fmpl_pemode = pemode;

    p_PlcrRegs->fmpl_pegnia = gnia;
    p_PlcrRegs->fmpl_peynia = ynia;
    p_PlcrRegs->fmpl_pernia = rnia;

    /* Zero Counters */
    p_PlcrRegs->fmpl_pegpc     = 0;
    p_PlcrRegs->fmpl_peypc     = 0;
    p_PlcrRegs->fmpl_perpc     = 0;
    p_PlcrRegs->fmpl_perypc    = 0;
    p_PlcrRegs->fmpl_perrpc    = 0;

    return E_OK;
}

static t_Error AllocSharedProfiles(t_FmPcd *p_FmPcd, uint16_t numOfProfiles, uint16_t *profilesIds)
{
    uint32_t        profilesFound;
    uint16_t        i, k=0;
    uint32_t        intFlags;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);

    if (!numOfProfiles)
        return E_OK;

    if (numOfProfiles>FM_PCD_PLCR_NUM_ENTRIES)
        RETURN_ERROR(MINOR, E_INVALID_VALUE, ("numProfiles is too big."));

    intFlags = PlcrSwLock(p_FmPcd->p_FmPcdPlcr);
    /* Find numOfProfiles free profiles (may be spread) */
    profilesFound = 0;
    for (i=0;i<FM_PCD_PLCR_NUM_ENTRIES; i++)
        if (!p_FmPcd->p_FmPcdPlcr->profiles[i].profilesMng.allocated)
        {
            profilesFound++;
            profilesIds[k] = i;
            k++;
            if (profilesFound == numOfProfiles)
                break;
        }

    if (profilesFound != numOfProfiles)
    {
        PlcrSwUnlock(p_FmPcd->p_FmPcdPlcr, intFlags);
        RETURN_ERROR(MAJOR, E_INVALID_STATE,NO_MSG);
    }

    for (i = 0;i<k;i++)
    {
        p_FmPcd->p_FmPcdPlcr->profiles[profilesIds[i]].profilesMng.allocated = TRUE;
        p_FmPcd->p_FmPcdPlcr->profiles[profilesIds[i]].profilesMng.ownerId = 0;
    }
    PlcrSwUnlock(p_FmPcd->p_FmPcdPlcr, intFlags);

    return E_OK;
}

static void FreeSharedProfiles(t_FmPcd *p_FmPcd, uint16_t numOfProfiles, uint16_t *profilesIds)
{
    uint16_t        i;

    SANITY_CHECK_RETURN(p_FmPcd, E_INVALID_HANDLE);

    ASSERT_COND(numOfProfiles);

    for (i=0; i < numOfProfiles; i++)
    {
        ASSERT_COND(p_FmPcd->p_FmPcdPlcr->profiles[profilesIds[i]].profilesMng.allocated);
        p_FmPcd->p_FmPcdPlcr->profiles[profilesIds[i]].profilesMng.allocated = FALSE;
        p_FmPcd->p_FmPcdPlcr->profiles[profilesIds[i]].profilesMng.ownerId = p_FmPcd->guestId;
    }
}

static void UpdateRequiredActionFlag(t_Handle h_FmPcd, uint16_t absoluteProfileId, bool set)
{
    t_FmPcd     *p_FmPcd = (t_FmPcd*)h_FmPcd;

    /* this routine is protected by calling routine */

    ASSERT_COND(p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId].valid);

    if (set)
        p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId].requiredActionFlag = TRUE;
    else
    {
        p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId].requiredAction = 0;
        p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId].requiredActionFlag = FALSE;
    }
}

/*********************************************/
/*............Policer Exception..............*/
/*********************************************/
static void EventsCB(t_Handle h_FmPcd)
{
    t_FmPcd *p_FmPcd = (t_FmPcd *)h_FmPcd;
    uint32_t event, mask, force;

    ASSERT_COND(FmIsMaster(p_FmPcd->h_Fm));
    event = GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_evr);
    mask = GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_ier);

    event &= mask;

    /* clear the forced events */
    force = GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_ifr);
    if (force & event)
        WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_ifr, force & ~event);


    WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_evr, event);

    if (event & FM_PCD_PLCR_PRAM_SELF_INIT_COMPLETE)
        p_FmPcd->f_Exception(p_FmPcd->h_App,e_FM_PCD_PLCR_EXCEPTION_PRAM_SELF_INIT_COMPLETE);
    if (event & FM_PCD_PLCR_ATOMIC_ACTION_COMPLETE)
        p_FmPcd->f_Exception(p_FmPcd->h_App,e_FM_PCD_PLCR_EXCEPTION_ATOMIC_ACTION_COMPLETE);
}

/* ..... */

static void ErrorExceptionsCB(t_Handle h_FmPcd)
{
    t_FmPcd             *p_FmPcd = (t_FmPcd *)h_FmPcd;
    uint32_t            event, force, captureReg, mask;

    ASSERT_COND(FmIsMaster(p_FmPcd->h_Fm));
    event = GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_eevr);
    mask = GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_eier);

    event &= mask;

    /* clear the forced events */
    force = GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_eifr);
    if (force & event)
        WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_eifr, force & ~event);

    WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_eevr, event);

    if (event & FM_PCD_PLCR_DOUBLE_ECC)
        p_FmPcd->f_Exception(p_FmPcd->h_App,e_FM_PCD_PLCR_EXCEPTION_DOUBLE_ECC);
    if (event & FM_PCD_PLCR_INIT_ENTRY_ERROR)
    {
        captureReg = GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_upcr);
        /*ASSERT_COND(captureReg & PLCR_ERR_UNINIT_CAP);
        p_UnInitCapt->profileNum = (uint8_t)(captureReg & PLCR_ERR_UNINIT_NUM_MASK);
        p_UnInitCapt->portId = (uint8_t)((captureReg & PLCR_ERR_UNINIT_PID_MASK) >>PLCR_ERR_UNINIT_PID_SHIFT) ;
        p_UnInitCapt->absolute = (bool)(captureReg & PLCR_ERR_UNINIT_ABSOLUTE_MASK);*/
        p_FmPcd->f_FmPcdIndexedException(p_FmPcd->h_App,e_FM_PCD_PLCR_EXCEPTION_INIT_ENTRY_ERROR,(uint16_t)(captureReg & PLCR_ERR_UNINIT_NUM_MASK));
        WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_upcr, PLCR_ERR_UNINIT_CAP);
    }
}


/*****************************************************************************/
/*              Inter-module API routines                                    */
/*****************************************************************************/

t_Handle PlcrConfig(t_FmPcd *p_FmPcd, t_FmPcdParams *p_FmPcdParams)
{
    t_FmPcdPlcr *p_FmPcdPlcr;
    uint16_t    i=0;

    UNUSED(p_FmPcd);
    UNUSED(p_FmPcdParams);

    p_FmPcdPlcr = (t_FmPcdPlcr *) XX_Malloc(sizeof(t_FmPcdPlcr));
    if (!p_FmPcdPlcr)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM Policer structure allocation FAILED"));
        return NULL;
    }
    memset(p_FmPcdPlcr, 0, sizeof(t_FmPcdPlcr));
    if (p_FmPcd->guestId == NCSW_MASTER_ID)
    {
        p_FmPcdPlcr->p_FmPcdPlcrRegs  = (t_FmPcdPlcrRegs *)UINT_TO_PTR(FmGetPcdPlcrBaseAddr(p_FmPcdParams->h_Fm));
        p_FmPcd->p_FmPcdDriverParam->plcrAutoRefresh    = DEFAULT_plcrAutoRefresh;
        p_FmPcd->exceptions |= (DEFAULT_fmPcdPlcrExceptions | DEFAULT_fmPcdPlcrErrorExceptions);
    }

    p_FmPcdPlcr->numOfSharedProfiles    = DEFAULT_numOfSharedPlcrProfiles;

    p_FmPcdPlcr->partPlcrProfilesBase   = p_FmPcdParams->partPlcrProfilesBase;
    p_FmPcdPlcr->partNumOfPlcrProfiles  = p_FmPcdParams->partNumOfPlcrProfiles;
    /* for backward compatabilty. if no policer profile, will set automatically to the max */
    if ((p_FmPcd->guestId == NCSW_MASTER_ID) &&
        (p_FmPcdPlcr->partNumOfPlcrProfiles == 0))
        p_FmPcdPlcr->partNumOfPlcrProfiles = FM_PCD_PLCR_NUM_ENTRIES;

    for (i=0; i<FM_PCD_PLCR_NUM_ENTRIES; i++)
        p_FmPcdPlcr->profiles[i].profilesMng.ownerId = (uint8_t)ILLEGAL_BASE;

    return p_FmPcdPlcr;
}

t_Error PlcrInit(t_FmPcd *p_FmPcd)
{
    t_FmPcdDriverParam              *p_Param = p_FmPcd->p_FmPcdDriverParam;
    t_FmPcdPlcr                     *p_FmPcdPlcr = p_FmPcd->p_FmPcdPlcr;
    t_FmPcdPlcrRegs                 *p_Regs = p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs;
    t_Error                         err = E_OK;
    uint32_t                        tmpReg32 = 0;
    uint16_t                        base;

    if ((p_FmPcdPlcr->partPlcrProfilesBase + p_FmPcdPlcr->partNumOfPlcrProfiles) > FM_PCD_PLCR_NUM_ENTRIES)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("partPlcrProfilesBase+partNumOfPlcrProfiles out of range!!!"));

    p_FmPcdPlcr->h_HwSpinlock = XX_InitSpinlock();
    if (!p_FmPcdPlcr->h_HwSpinlock)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("FM Policer HW spinlock"));

    p_FmPcdPlcr->h_SwSpinlock = XX_InitSpinlock();
    if (!p_FmPcdPlcr->h_SwSpinlock)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("FM Policer SW spinlock"));

    base = PlcrAllocProfilesForPartition(p_FmPcd,
                                         p_FmPcdPlcr->partPlcrProfilesBase,
                                         p_FmPcdPlcr->partNumOfPlcrProfiles,
                                         p_FmPcd->guestId);
    if (base == (uint16_t)ILLEGAL_BASE)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);

    if (p_FmPcdPlcr->numOfSharedProfiles)
    {
        err = AllocSharedProfiles(p_FmPcd,
                                  p_FmPcdPlcr->numOfSharedProfiles,
                                  p_FmPcdPlcr->sharedProfilesIds);
        if (err)
            RETURN_ERROR(MAJOR, err,NO_MSG);
    }

    if (p_FmPcd->guestId != NCSW_MASTER_ID)
        return E_OK;

    /**********************FMPL_GCR******************/
    tmpReg32 = 0;
    tmpReg32 |= FM_PCD_PLCR_GCR_STEN;
    if (p_Param->plcrAutoRefresh)
        tmpReg32 |= FM_PCD_PLCR_GCR_DAR;
    tmpReg32 |= GET_NIA_BMI_AC_ENQ_FRAME(p_FmPcd);

    WRITE_UINT32(p_Regs->fmpl_gcr, tmpReg32);
    /**********************FMPL_GCR******************/

    /**********************FMPL_EEVR******************/
    WRITE_UINT32(p_Regs->fmpl_eevr, (FM_PCD_PLCR_DOUBLE_ECC | FM_PCD_PLCR_INIT_ENTRY_ERROR));
    /**********************FMPL_EEVR******************/
    /**********************FMPL_EIER******************/
    tmpReg32 = 0;
    if (p_FmPcd->exceptions & FM_PCD_EX_PLCR_DOUBLE_ECC)
    {
        FmEnableRamsEcc(p_FmPcd->h_Fm);
        tmpReg32 |= FM_PCD_PLCR_DOUBLE_ECC;
    }
    if (p_FmPcd->exceptions & FM_PCD_EX_PLCR_INIT_ENTRY_ERROR)
        tmpReg32 |= FM_PCD_PLCR_INIT_ENTRY_ERROR;
    WRITE_UINT32(p_Regs->fmpl_eier, tmpReg32);
    /**********************FMPL_EIER******************/

    /**********************FMPL_EVR******************/
    WRITE_UINT32(p_Regs->fmpl_evr, (FM_PCD_PLCR_PRAM_SELF_INIT_COMPLETE | FM_PCD_PLCR_ATOMIC_ACTION_COMPLETE));
    /**********************FMPL_EVR******************/
    /**********************FMPL_IER******************/
    tmpReg32 = 0;
    if (p_FmPcd->exceptions & FM_PCD_EX_PLCR_PRAM_SELF_INIT_COMPLETE)
        tmpReg32 |= FM_PCD_PLCR_PRAM_SELF_INIT_COMPLETE;
    if (p_FmPcd->exceptions & FM_PCD_EX_PLCR_ATOMIC_ACTION_COMPLETE)
        tmpReg32 |= FM_PCD_PLCR_ATOMIC_ACTION_COMPLETE;
    WRITE_UINT32(p_Regs->fmpl_ier, tmpReg32);
    /**********************FMPL_IER******************/

    /* register even if no interrupts enabled, to allow future enablement */
    FmRegisterIntr(p_FmPcd->h_Fm,
                   e_FM_MOD_PLCR,
                   0,
                   e_FM_INTR_TYPE_ERR,
                   ErrorExceptionsCB,
                   p_FmPcd);
    FmRegisterIntr(p_FmPcd->h_Fm,
                   e_FM_MOD_PLCR,
                   0,
                   e_FM_INTR_TYPE_NORMAL,
                   EventsCB,
                   p_FmPcd);

    /* driver initializes one DFLT profile at the last entry*/
    /**********************FMPL_DPMR******************/
    tmpReg32 = 0;
    WRITE_UINT32(p_Regs->fmpl_dpmr, tmpReg32);
    p_FmPcd->p_FmPcdPlcr->profiles[0].profilesMng.allocated = TRUE;

    return E_OK;
}

t_Error PlcrFree(t_FmPcd *p_FmPcd)
{
    FmUnregisterIntr(p_FmPcd->h_Fm, e_FM_MOD_PLCR, 0, e_FM_INTR_TYPE_ERR);
    FmUnregisterIntr(p_FmPcd->h_Fm, e_FM_MOD_PLCR, 0, e_FM_INTR_TYPE_NORMAL);

    if (p_FmPcd->p_FmPcdPlcr->numOfSharedProfiles)
        FreeSharedProfiles(p_FmPcd,
                           p_FmPcd->p_FmPcdPlcr->numOfSharedProfiles,
                           p_FmPcd->p_FmPcdPlcr->sharedProfilesIds);

    if (p_FmPcd->p_FmPcdPlcr->partNumOfPlcrProfiles)
        PlcrFreeProfilesForPartition(p_FmPcd,
                                     p_FmPcd->p_FmPcdPlcr->partPlcrProfilesBase,
                                     p_FmPcd->p_FmPcdPlcr->partNumOfPlcrProfiles,
                                     p_FmPcd->guestId);

    if (p_FmPcd->p_FmPcdPlcr->h_SwSpinlock)
        XX_FreeSpinlock(p_FmPcd->p_FmPcdPlcr->h_SwSpinlock);

    if (p_FmPcd->p_FmPcdPlcr->h_HwSpinlock)
        XX_FreeSpinlock(p_FmPcd->p_FmPcdPlcr->h_HwSpinlock);

    return E_OK;
}

void PlcrEnable(t_FmPcd *p_FmPcd)
{
    t_FmPcdPlcrRegs             *p_Regs = p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs;

    WRITE_UINT32(p_Regs->fmpl_gcr, GET_UINT32(p_Regs->fmpl_gcr) | FM_PCD_PLCR_GCR_EN);
}

void PlcrDisable(t_FmPcd *p_FmPcd)
{
    t_FmPcdPlcrRegs             *p_Regs = p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs;

    WRITE_UINT32(p_Regs->fmpl_gcr, GET_UINT32(p_Regs->fmpl_gcr) & ~FM_PCD_PLCR_GCR_EN);
}

uint16_t PlcrAllocProfilesForPartition(t_FmPcd *p_FmPcd, uint16_t base, uint16_t numOfProfiles, uint8_t guestId)
{
    uint32_t    intFlags;
    uint16_t    profilesFound = 0;
    int         i = 0;

    ASSERT_COND(p_FmPcd);
    ASSERT_COND(p_FmPcd->p_FmPcdPlcr);

    if (!numOfProfiles)
        return 0;

    if ((numOfProfiles > FM_PCD_PLCR_NUM_ENTRIES) ||
        (base + numOfProfiles > FM_PCD_PLCR_NUM_ENTRIES))
        return (uint16_t)ILLEGAL_BASE;

    if (p_FmPcd->h_IpcSession)
    {
        t_FmIpcResourceAllocParams      ipcAllocParams;
        t_FmPcdIpcMsg                   msg;
        t_FmPcdIpcReply                 reply;
        t_Error                         err;
        uint32_t                        replyLength;

        memset(&msg, 0, sizeof(msg));
        memset(&reply, 0, sizeof(reply));
        memset(&ipcAllocParams, 0, sizeof(t_FmIpcResourceAllocParams));
        ipcAllocParams.guestId         = p_FmPcd->guestId;
        ipcAllocParams.num             = p_FmPcd->p_FmPcdPlcr->partNumOfPlcrProfiles;
        ipcAllocParams.base            = p_FmPcd->p_FmPcdPlcr->partPlcrProfilesBase;
        msg.msgId                      = FM_PCD_ALLOC_PROFILES;
        memcpy(msg.msgBody, &ipcAllocParams, sizeof(t_FmIpcResourceAllocParams));
        replyLength = sizeof(uint32_t) + sizeof(uint16_t);
        err = XX_IpcSendMessage(p_FmPcd->h_IpcSession,
                                (uint8_t*)&msg,
                                sizeof(msg.msgId) + sizeof(t_FmIpcResourceAllocParams),
                                (uint8_t*)&reply,
                                &replyLength,
                                NULL,
                                NULL);
        if ((err != E_OK) ||
            (replyLength != (sizeof(uint32_t) + sizeof(uint16_t))))
        {
            REPORT_ERROR(MAJOR, err, NO_MSG);
            return (uint16_t)ILLEGAL_BASE;
        }
        else
            memcpy((uint8_t*)&p_FmPcd->p_FmPcdPlcr->partPlcrProfilesBase, reply.replyBody, sizeof(uint16_t));
        if (p_FmPcd->p_FmPcdPlcr->partPlcrProfilesBase == (uint16_t)ILLEGAL_BASE)
        {
            REPORT_ERROR(MAJOR, err, NO_MSG);
            return (uint16_t)ILLEGAL_BASE;
        }
    }
    else if (p_FmPcd->guestId != NCSW_MASTER_ID)
    {
        DBG(WARNING, ("FM Guest mode, without IPC - can't validate Policer-profiles range!"));
        return (uint16_t)ILLEGAL_BASE;
    }

    intFlags = XX_LockIntrSpinlock(p_FmPcd->h_Spinlock);
    for (i=base; i<(base+numOfProfiles); i++)
        if (p_FmPcd->p_FmPcdPlcr->profiles[i].profilesMng.ownerId == (uint8_t)ILLEGAL_BASE)
            profilesFound++;
        else
            break;

    if (profilesFound == numOfProfiles)
        for (i=base; i<(base+numOfProfiles); i++)
            p_FmPcd->p_FmPcdPlcr->profiles[i].profilesMng.ownerId = guestId;
    else
    {
        XX_UnlockIntrSpinlock(p_FmPcd->h_Spinlock, intFlags);
        return (uint16_t)ILLEGAL_BASE;
    }
    XX_UnlockIntrSpinlock(p_FmPcd->h_Spinlock, intFlags);

    return base;
}

void PlcrFreeProfilesForPartition(t_FmPcd *p_FmPcd, uint16_t base, uint16_t numOfProfiles, uint8_t guestId)
{
    int     i = 0;

    ASSERT_COND(p_FmPcd);
    ASSERT_COND(p_FmPcd->p_FmPcdPlcr);

    if (p_FmPcd->h_IpcSession)
    {
        t_FmIpcResourceAllocParams      ipcAllocParams;
        t_FmPcdIpcMsg                   msg;
        t_Error                         err;

        memset(&msg, 0, sizeof(msg));
        memset(&ipcAllocParams, 0, sizeof(t_FmIpcResourceAllocParams));
        ipcAllocParams.guestId         = p_FmPcd->guestId;
        ipcAllocParams.num             = p_FmPcd->p_FmPcdPlcr->partNumOfPlcrProfiles;
        ipcAllocParams.base            = p_FmPcd->p_FmPcdPlcr->partPlcrProfilesBase;
        msg.msgId                      = FM_PCD_FREE_PROFILES;
        memcpy(msg.msgBody, &ipcAllocParams, sizeof(t_FmIpcResourceAllocParams));
        err = XX_IpcSendMessage(p_FmPcd->h_IpcSession,
                                (uint8_t*)&msg,
                                sizeof(msg.msgId) + sizeof(t_FmIpcResourceAllocParams),
                                NULL,
                                NULL,
                                NULL,
                                NULL);
        if (err != E_OK)
            REPORT_ERROR(MAJOR, err, NO_MSG);
        return;
    }
    else if (p_FmPcd->guestId != NCSW_MASTER_ID)
    {
        DBG(WARNING, ("FM Guest mode, without IPC - can't validate Policer-profiles range!"));
        return;
    }

    for (i=base; i<(base+numOfProfiles); i++)
    {
        if (p_FmPcd->p_FmPcdPlcr->profiles[i].profilesMng.ownerId == guestId)
           p_FmPcd->p_FmPcdPlcr->profiles[i].profilesMng.ownerId = (uint8_t)ILLEGAL_BASE;
        else
            DBG(WARNING, ("Request for freeing storage profile window which wasn't allocated to this partition"));
    }
}

t_Error PlcrSetPortProfiles(t_FmPcd    *p_FmPcd,
                            uint8_t    hardwarePortId,
                            uint16_t   numOfProfiles,
                            uint16_t   base)
{
    t_FmPcdPlcrRegs *p_Regs = p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs;
    uint32_t        log2Num, tmpReg32;

    if ((p_FmPcd->guestId != NCSW_MASTER_ID) &&
        !p_Regs &&
        p_FmPcd->h_IpcSession)
    {
        t_FmIpcResourceAllocParams      ipcAllocParams;
        t_FmPcdIpcMsg                   msg;
        t_Error                         err;

        memset(&msg, 0, sizeof(msg));
        memset(&ipcAllocParams, 0, sizeof(t_FmIpcResourceAllocParams));
        ipcAllocParams.guestId         = hardwarePortId;
        ipcAllocParams.num             = numOfProfiles;
        ipcAllocParams.base            = base;
        msg.msgId                              = FM_PCD_SET_PORT_PROFILES;
        memcpy(msg.msgBody, &ipcAllocParams, sizeof(t_FmIpcResourceAllocParams));
        err = XX_IpcSendMessage(p_FmPcd->h_IpcSession,
                                (uint8_t*)&msg,
                                sizeof(msg.msgId) + sizeof(t_FmIpcResourceAllocParams),
                                NULL,
                                NULL,
                                NULL,
                                NULL);
        if (err != E_OK)
            RETURN_ERROR(MAJOR, err, NO_MSG);
        return E_OK;
    }
    else if (!p_Regs)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("Either IPC or 'baseAddress' is required!"));

    ASSERT_COND(IN_RANGE(1, hardwarePortId, 63));

    if (GET_UINT32(p_Regs->fmpl_pmr[hardwarePortId-1]) & FM_PCD_PLCR_PMR_V)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("The requesting port has already an allocated profiles window."));

    /**********************FMPL_PMRx******************/
    LOG2((uint64_t)numOfProfiles, log2Num);
    tmpReg32 = base;
    tmpReg32 |= log2Num << 16;
    tmpReg32 |= FM_PCD_PLCR_PMR_V;
    WRITE_UINT32(p_Regs->fmpl_pmr[hardwarePortId-1], tmpReg32);

    return E_OK;
}

t_Error PlcrClearPortProfiles(t_FmPcd *p_FmPcd, uint8_t hardwarePortId)
{
    t_FmPcdPlcrRegs *p_Regs = p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs;

    if ((p_FmPcd->guestId != NCSW_MASTER_ID) &&
        !p_Regs &&
        p_FmPcd->h_IpcSession)
    {
        t_FmIpcResourceAllocParams      ipcAllocParams;
        t_FmPcdIpcMsg                   msg;
        t_Error                         err;

        memset(&msg, 0, sizeof(msg));
        memset(&ipcAllocParams, 0, sizeof(t_FmIpcResourceAllocParams));
        ipcAllocParams.guestId         = hardwarePortId;
        msg.msgId                              = FM_PCD_CLEAR_PORT_PROFILES;
        memcpy(msg.msgBody, &ipcAllocParams, sizeof(t_FmIpcResourceAllocParams));
        err = XX_IpcSendMessage(p_FmPcd->h_IpcSession,
                                (uint8_t*)&msg,
                                sizeof(msg.msgId) + sizeof(t_FmIpcResourceAllocParams),
                                NULL,
                                NULL,
                                NULL,
                                NULL);
        if (err != E_OK)
            RETURN_ERROR(MAJOR, err, NO_MSG);
        return E_OK;
    }
    else if (!p_Regs)
        RETURN_ERROR(MINOR, E_NOT_SUPPORTED,
                     ("Either IPC or 'baseAddress' is required!"));

    ASSERT_COND(IN_RANGE(1, hardwarePortId, 63));
    WRITE_UINT32(p_Regs->fmpl_pmr[hardwarePortId-1], 0);

    return E_OK;
}

t_Error FmPcdPlcrAllocProfiles(t_Handle h_FmPcd, uint8_t hardwarePortId, uint16_t numOfProfiles)
{
    t_FmPcd                     *p_FmPcd = (t_FmPcd*)h_FmPcd;
    t_Error                     err = E_OK;
    uint32_t                    profilesFound;
    uint32_t                    intFlags;
    uint16_t                    i, first, swPortIndex = 0;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);

    if (!numOfProfiles)
        return E_OK;

    ASSERT_COND(hardwarePortId);

    if (numOfProfiles>FM_PCD_PLCR_NUM_ENTRIES)
        RETURN_ERROR(MINOR, E_INVALID_VALUE, ("numProfiles is too big."));

    if (!POWER_OF_2(numOfProfiles))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("numProfiles must be a power of 2."));

    first = 0;
    profilesFound = 0;
    intFlags = PlcrSwLock(p_FmPcd->p_FmPcdPlcr);

    for (i=0; i<FM_PCD_PLCR_NUM_ENTRIES; )
    {
        if (!p_FmPcd->p_FmPcdPlcr->profiles[i].profilesMng.allocated)
        {
            profilesFound++;
            i++;
            if (profilesFound == numOfProfiles)
                break;
        }
        else
        {
            profilesFound = 0;
            /* advance i to the next aligned address */
            i = first = (uint16_t)(first + numOfProfiles);
        }
    }

    if (profilesFound == numOfProfiles)
    {
        for (i=first; i<first + numOfProfiles; i++)
        {
            p_FmPcd->p_FmPcdPlcr->profiles[i].profilesMng.allocated = TRUE;
            p_FmPcd->p_FmPcdPlcr->profiles[i].profilesMng.ownerId = hardwarePortId;
        }
    }
    else
    {
        PlcrSwUnlock(p_FmPcd->p_FmPcdPlcr, intFlags);
        RETURN_ERROR(MINOR, E_FULL, ("No profiles."));
    }
    PlcrSwUnlock(p_FmPcd->p_FmPcdPlcr, intFlags);

    err = PlcrSetPortProfiles(p_FmPcd, hardwarePortId, numOfProfiles, first);
    if (err)
    {
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    HW_PORT_ID_TO_SW_PORT_INDX(swPortIndex, hardwarePortId);

    p_FmPcd->p_FmPcdPlcr->portsMapping[swPortIndex].numOfProfiles = numOfProfiles;
    p_FmPcd->p_FmPcdPlcr->portsMapping[swPortIndex].profilesBase = first;

    return E_OK;
}

t_Error FmPcdPlcrFreeProfiles(t_Handle h_FmPcd, uint8_t hardwarePortId)
{
    t_FmPcd                     *p_FmPcd = (t_FmPcd*)h_FmPcd;
    t_Error                     err = E_OK;
    uint32_t                    intFlags;
    uint16_t                    i, swPortIndex = 0;

    ASSERT_COND(IN_RANGE(1, hardwarePortId, 63));

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPcd->p_FmPcdDriverParam, E_INVALID_HANDLE);

    HW_PORT_ID_TO_SW_PORT_INDX(swPortIndex, hardwarePortId);

    err = PlcrClearPortProfiles(p_FmPcd, hardwarePortId);
    if (err)
        RETURN_ERROR(MAJOR, err,NO_MSG);

    intFlags = PlcrSwLock(p_FmPcd->p_FmPcdPlcr);
    for (i=p_FmPcd->p_FmPcdPlcr->portsMapping[swPortIndex].profilesBase;
         i<(p_FmPcd->p_FmPcdPlcr->portsMapping[swPortIndex].profilesBase +
            p_FmPcd->p_FmPcdPlcr->portsMapping[swPortIndex].numOfProfiles);
         i++)
    {
        ASSERT_COND(p_FmPcd->p_FmPcdPlcr->profiles[i].profilesMng.ownerId == hardwarePortId);
        ASSERT_COND(p_FmPcd->p_FmPcdPlcr->profiles[i].profilesMng.allocated);

        p_FmPcd->p_FmPcdPlcr->profiles[i].profilesMng.allocated = FALSE;
        p_FmPcd->p_FmPcdPlcr->profiles[i].profilesMng.ownerId = p_FmPcd->guestId;
    }
    PlcrSwUnlock(p_FmPcd->p_FmPcdPlcr, intFlags);

    p_FmPcd->p_FmPcdPlcr->portsMapping[swPortIndex].numOfProfiles = 0;
    p_FmPcd->p_FmPcdPlcr->portsMapping[swPortIndex].profilesBase = 0;

    return E_OK;
}

t_Error FmPcdPlcrCcGetSetParams(t_Handle h_FmPcd, uint16_t profileIndx ,uint32_t requiredAction)
{
    t_FmPcd         *p_FmPcd           = (t_FmPcd *)h_FmPcd;
    t_FmPcdPlcr     *p_FmPcdPlcr        = p_FmPcd->p_FmPcdPlcr;
    t_FmPcdPlcrRegs *p_FmPcdPlcrRegs    = p_FmPcdPlcr->p_FmPcdPlcrRegs;
    uint32_t        tmpReg32, intFlags;
    t_Error         err;

    /* Calling function locked all PCD modules, so no need to lock here */

    if (profileIndx >= FM_PCD_PLCR_NUM_ENTRIES)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,("Policer profile out of range"));

    if (!FmPcdPlcrIsProfileValid(p_FmPcd, profileIndx))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,("Policer profile is not valid"));

    /*intFlags = PlcrProfileLock(&p_FmPcd->p_FmPcdPlcr->profiles[profileIndx]);*/

    if (p_FmPcd->h_Hc)
    {
        err = FmHcPcdPlcrCcGetSetParams(p_FmPcd->h_Hc, profileIndx, requiredAction);

        UpdateRequiredActionFlag(p_FmPcd, profileIndx, TRUE);
        FmPcdPlcrUpdateRequiredAction(p_FmPcd, profileIndx, requiredAction);

        /*PlcrProfileUnlock(&p_FmPcd->p_FmPcdPlcr->profiles[profileIndx], intFlags);*/
        return err;
    }

    /* lock the HW because once we read the registers we don't want them to be changed
     * by another access. (We can copy to a tmp location and release the lock!) */

    intFlags = PlcrHwLock(p_FmPcdPlcr);
    WritePar(p_FmPcd, FmPcdPlcrBuildReadPlcrActionReg(profileIndx));

    if (!p_FmPcd->p_FmPcdPlcr->profiles[profileIndx].requiredActionFlag ||
       !(p_FmPcd->p_FmPcdPlcr->profiles[profileIndx].requiredAction & requiredAction))
    {
        if (requiredAction & UPDATE_NIA_ENQ_WITHOUT_DMA)
        {
            if ((p_FmPcd->p_FmPcdPlcr->profiles[profileIndx].nextEngineOnGreen!= e_FM_PCD_DONE) ||
               (p_FmPcd->p_FmPcdPlcr->profiles[profileIndx].nextEngineOnYellow!= e_FM_PCD_DONE) ||
               (p_FmPcd->p_FmPcdPlcr->profiles[profileIndx].nextEngineOnRed!= e_FM_PCD_DONE))
            {
                PlcrHwUnlock(p_FmPcdPlcr, intFlags);
                /*PlcrProfileUnlock(&p_FmPcd->p_FmPcdPlcr->profiles[profileIndx], intFlags);*/
                RETURN_ERROR (MAJOR, E_OK, ("In this case the next engine can be e_FM_PCD_DONE"));
            }

            if (p_FmPcd->p_FmPcdPlcr->profiles[profileIndx].paramsOnGreen.action == e_FM_PCD_ENQ_FRAME)
            {
                tmpReg32 = GET_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pegnia);
                if (!(tmpReg32 & (NIA_ENG_BMI | NIA_BMI_AC_ENQ_FRAME)))
                {
                    PlcrHwUnlock(p_FmPcdPlcr, intFlags);
                    /*PlcrProfileUnlock(&p_FmPcd->p_FmPcdPlcr->profiles[profileIndx], intFlags);*/
                    RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Next engine of this policer profile has to be assigned to FM_PCD_DONE"));
                }
                tmpReg32 |= NIA_BMI_AC_ENQ_FRAME_WITHOUT_DMA;
                WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pegnia, tmpReg32);
                tmpReg32 = FmPcdPlcrBuildWritePlcrActionReg(profileIndx);
                tmpReg32 |= FM_PCD_PLCR_PAR_PWSEL_PEGNIA;
                WritePar(p_FmPcd, tmpReg32);
            }

            if (p_FmPcd->p_FmPcdPlcr->profiles[profileIndx].paramsOnYellow.action == e_FM_PCD_ENQ_FRAME)
            {
                tmpReg32 = GET_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_peynia);
                if (!(tmpReg32 & (NIA_ENG_BMI | NIA_BMI_AC_ENQ_FRAME)))
                {
                    PlcrHwUnlock(p_FmPcdPlcr, intFlags);
                    /*PlcrProfileUnlock(&p_FmPcd->p_FmPcdPlcr->profiles[profileIndx], intFlags);*/
                    RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Next engine of this policer profile has to be assigned to FM_PCD_DONE"));
                }
                tmpReg32 |= NIA_BMI_AC_ENQ_FRAME_WITHOUT_DMA;
                WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_peynia, tmpReg32);
                tmpReg32 = FmPcdPlcrBuildWritePlcrActionReg(profileIndx);
                tmpReg32 |= FM_PCD_PLCR_PAR_PWSEL_PEYNIA;
                WritePar(p_FmPcd, tmpReg32);
                PlcrHwUnlock(p_FmPcdPlcr, intFlags);
            }

            if (p_FmPcd->p_FmPcdPlcr->profiles[profileIndx].paramsOnRed.action == e_FM_PCD_ENQ_FRAME)
            {
                tmpReg32 = GET_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pernia);
                if (!(tmpReg32 & (NIA_ENG_BMI | NIA_BMI_AC_ENQ_FRAME)))
                {
                    PlcrHwUnlock(p_FmPcdPlcr, intFlags);
                    /*PlcrProfileUnlock(&p_FmPcd->p_FmPcdPlcr->profiles[profileIndx], intFlags);*/
                    RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Next engine of this policer profile has to be assigned to FM_PCD_DONE"));
                }
                tmpReg32 |= NIA_BMI_AC_ENQ_FRAME_WITHOUT_DMA;
                WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pernia, tmpReg32);
                tmpReg32 = FmPcdPlcrBuildWritePlcrActionReg(profileIndx);
                tmpReg32 |= FM_PCD_PLCR_PAR_PWSEL_PERNIA;
                WritePar(p_FmPcd, tmpReg32);

            }
        }
    }
    PlcrHwUnlock(p_FmPcdPlcr, intFlags);

    UpdateRequiredActionFlag(p_FmPcd, profileIndx, TRUE);
    FmPcdPlcrUpdateRequiredAction(p_FmPcd, profileIndx, requiredAction);

    /*PlcrProfileUnlock(&p_FmPcd->p_FmPcdPlcr->profiles[profileIndx], intFlags);*/

    return E_OK;
}

uint32_t FmPcdPlcrGetRequiredActionFlag(t_Handle h_FmPcd, uint16_t absoluteProfileId)
{
    t_FmPcd     *p_FmPcd = (t_FmPcd*)h_FmPcd;

   ASSERT_COND(p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId].valid);

    return p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId].requiredActionFlag;
}

uint32_t FmPcdPlcrGetRequiredAction(t_Handle h_FmPcd, uint16_t absoluteProfileId)
{
    t_FmPcd     *p_FmPcd = (t_FmPcd*)h_FmPcd;

   ASSERT_COND(p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId].valid);

    return p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId].requiredAction;
}

bool FmPcdPlcrIsProfileValid(t_Handle h_FmPcd, uint16_t absoluteProfileId)
{
    t_FmPcd         *p_FmPcd            = (t_FmPcd*)h_FmPcd;
    t_FmPcdPlcr     *p_FmPcdPlcr        = p_FmPcd->p_FmPcdPlcr;

    ASSERT_COND(absoluteProfileId < FM_PCD_PLCR_NUM_ENTRIES);

    return p_FmPcdPlcr->profiles[absoluteProfileId].valid;
}

void  FmPcdPlcrValidateProfileSw(t_Handle h_FmPcd, uint16_t absoluteProfileId)
{
    t_FmPcd     *p_FmPcd = (t_FmPcd*)h_FmPcd;
    uint32_t    intFlags;

    ASSERT_COND(!p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId].valid);

    intFlags = PlcrProfileLock(&p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId]);
    p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId].valid = TRUE;
    PlcrProfileUnlock(&p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId], intFlags);
}

void  FmPcdPlcrInvalidateProfileSw(t_Handle h_FmPcd, uint16_t absoluteProfileId)
{
    t_FmPcd     *p_FmPcd = (t_FmPcd*)h_FmPcd;
    uint32_t    intFlags;

    ASSERT_COND(p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId].valid);

    intFlags = PlcrProfileLock(&p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId]);
    p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId].valid = FALSE;
    PlcrProfileUnlock(&p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId], intFlags);
}

uint16_t     FmPcdPlcrProfileGetAbsoluteId(t_Handle h_Profile)
{
        return ((t_FmPcdPlcrProfile*)h_Profile)->absoluteProfileId;
}

t_Error FmPcdPlcrGetAbsoluteIdByProfileParams(t_Handle                      h_FmPcd,
                                              e_FmPcdProfileTypeSelection   profileType,
                                              t_Handle                      h_FmPort,
                                              uint16_t                      relativeProfile,
                                              uint16_t                      *p_AbsoluteId)
{
    t_FmPcd         *p_FmPcd            = (t_FmPcd*)h_FmPcd;
    t_FmPcdPlcr     *p_FmPcdPlcr        = p_FmPcd->p_FmPcdPlcr;
    uint8_t         i;

    switch (profileType)
    {
        case e_FM_PCD_PLCR_PORT_PRIVATE:
            /* get port PCD id from port handle */
            for (i=0;i<FM_MAX_NUM_OF_PORTS;i++)
                if (p_FmPcd->p_FmPcdPlcr->portsMapping[i].h_FmPort == h_FmPort)
                    break;
            if (i ==  FM_MAX_NUM_OF_PORTS)
                RETURN_ERROR(MAJOR, E_INVALID_STATE , ("Invalid port handle."));

            if (!p_FmPcd->p_FmPcdPlcr->portsMapping[i].numOfProfiles)
                RETURN_ERROR(MAJOR, E_INVALID_SELECTION , ("Port has no allocated profiles"));
            if (relativeProfile >= p_FmPcd->p_FmPcdPlcr->portsMapping[i].numOfProfiles)
                RETURN_ERROR(MAJOR, E_INVALID_SELECTION , ("Profile id is out of range"));
            *p_AbsoluteId = (uint16_t)(p_FmPcd->p_FmPcdPlcr->portsMapping[i].profilesBase + relativeProfile);
            break;
        case e_FM_PCD_PLCR_SHARED:
            if (relativeProfile >= p_FmPcdPlcr->numOfSharedProfiles)
                RETURN_ERROR(MAJOR, E_INVALID_SELECTION , ("Profile id is out of range"));
            *p_AbsoluteId = (uint16_t)(p_FmPcdPlcr->sharedProfilesIds[relativeProfile]);
            break;
        default:
            RETURN_ERROR(MAJOR, E_INVALID_SELECTION, ("Invalid policer profile type"));
    }

    return E_OK;
}

uint16_t FmPcdPlcrGetPortProfilesBase(t_Handle h_FmPcd, uint8_t hardwarePortId)
{
    t_FmPcd         *p_FmPcd = (t_FmPcd *)h_FmPcd;
    uint16_t        swPortIndex = 0;

    HW_PORT_ID_TO_SW_PORT_INDX(swPortIndex, hardwarePortId);

    return p_FmPcd->p_FmPcdPlcr->portsMapping[swPortIndex].profilesBase;
}

uint16_t FmPcdPlcrGetPortNumOfProfiles(t_Handle h_FmPcd, uint8_t hardwarePortId)
{
    t_FmPcd         *p_FmPcd = (t_FmPcd *)h_FmPcd;
    uint16_t        swPortIndex = 0;

    HW_PORT_ID_TO_SW_PORT_INDX(swPortIndex, hardwarePortId);

    return p_FmPcd->p_FmPcdPlcr->portsMapping[swPortIndex].numOfProfiles;

}
uint32_t FmPcdPlcrBuildWritePlcrActionReg(uint16_t absoluteProfileId)
{
    return (uint32_t)(FM_PCD_PLCR_PAR_GO |
                      ((uint32_t)absoluteProfileId << FM_PCD_PLCR_PAR_PNUM_SHIFT));
}

uint32_t FmPcdPlcrBuildWritePlcrActionRegs(uint16_t absoluteProfileId)
{
    return (uint32_t)(FM_PCD_PLCR_PAR_GO |
                      ((uint32_t)absoluteProfileId << FM_PCD_PLCR_PAR_PNUM_SHIFT) |
                      FM_PCD_PLCR_PAR_PWSEL_MASK);
}

bool    FmPcdPlcrHwProfileIsValid(uint32_t profileModeReg)
{

    if (profileModeReg & FM_PCD_PLCR_PEMODE_PI)
        return TRUE;
    else
        return FALSE;
}

uint32_t FmPcdPlcrBuildReadPlcrActionReg(uint16_t absoluteProfileId)
{
    return (uint32_t)(FM_PCD_PLCR_PAR_GO |
                      FM_PCD_PLCR_PAR_R |
                      ((uint32_t)absoluteProfileId << FM_PCD_PLCR_PAR_PNUM_SHIFT) |
                      FM_PCD_PLCR_PAR_PWSEL_MASK);
}

uint32_t FmPcdPlcrBuildCounterProfileReg(e_FmPcdPlcrProfileCounters counter)
{
    switch (counter)
    {
        case (e_FM_PCD_PLCR_PROFILE_GREEN_PACKET_TOTAL_COUNTER):
            return FM_PCD_PLCR_PAR_PWSEL_PEGPC;
        case (e_FM_PCD_PLCR_PROFILE_YELLOW_PACKET_TOTAL_COUNTER):
            return FM_PCD_PLCR_PAR_PWSEL_PEYPC;
        case (e_FM_PCD_PLCR_PROFILE_RED_PACKET_TOTAL_COUNTER) :
            return FM_PCD_PLCR_PAR_PWSEL_PERPC;
        case (e_FM_PCD_PLCR_PROFILE_RECOLOURED_YELLOW_PACKET_TOTAL_COUNTER) :
            return FM_PCD_PLCR_PAR_PWSEL_PERYPC;
        case (e_FM_PCD_PLCR_PROFILE_RECOLOURED_RED_PACKET_TOTAL_COUNTER) :
            return FM_PCD_PLCR_PAR_PWSEL_PERRPC;
       default:
            REPORT_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
            return 0;
    }
}

uint32_t FmPcdPlcrBuildNiaProfileReg(bool green, bool yellow, bool red)
{

    uint32_t tmpReg32 = 0;

    if (green)
        tmpReg32 |= FM_PCD_PLCR_PAR_PWSEL_PEGNIA;
    if (yellow)
        tmpReg32 |= FM_PCD_PLCR_PAR_PWSEL_PEYNIA;
    if (red)
        tmpReg32 |= FM_PCD_PLCR_PAR_PWSEL_PERNIA;

    return tmpReg32;
}

void FmPcdPlcrUpdateRequiredAction(t_Handle h_FmPcd, uint16_t absoluteProfileId, uint32_t requiredAction)
{
    t_FmPcd     *p_FmPcd = (t_FmPcd*)h_FmPcd;

    /* this routine is protected by calling routine */

    ASSERT_COND(p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId].valid);

    p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId].requiredAction |= requiredAction;
}

/*********************** End of inter-module routines ************************/


/**************************************************/
/*............Policer API.........................*/
/**************************************************/

t_Error FM_PCD_ConfigPlcrAutoRefreshMode(t_Handle h_FmPcd, bool enable)
{
   t_FmPcd *p_FmPcd = (t_FmPcd*)h_FmPcd;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->p_FmPcdDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->p_FmPcdPlcr, E_INVALID_HANDLE);

    if (!FmIsMaster(p_FmPcd->h_Fm))
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("FM_PCD_ConfigPlcrAutoRefreshMode - guest mode!"));

    p_FmPcd->p_FmPcdDriverParam->plcrAutoRefresh = enable;

    return E_OK;
}

t_Error FM_PCD_ConfigPlcrNumOfSharedProfiles(t_Handle h_FmPcd, uint16_t numOfSharedPlcrProfiles)
{
   t_FmPcd *p_FmPcd = (t_FmPcd*)h_FmPcd;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->p_FmPcdDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->p_FmPcdPlcr, E_INVALID_HANDLE);

    p_FmPcd->p_FmPcdPlcr->numOfSharedProfiles = numOfSharedPlcrProfiles;

    return E_OK;
}

t_Error FM_PCD_SetPlcrStatistics(t_Handle h_FmPcd, bool enable)
{
   t_FmPcd  *p_FmPcd = (t_FmPcd*)h_FmPcd;
   uint32_t tmpReg32;

    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPcd->p_FmPcdDriverParam, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPcd->p_FmPcdPlcr, E_INVALID_HANDLE);

    if (!FmIsMaster(p_FmPcd->h_Fm))
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("FM_PCD_SetPlcrStatistics - guest mode!"));

    tmpReg32 =  GET_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_gcr);
    if (enable)
        tmpReg32 |= FM_PCD_PLCR_GCR_STEN;
    else
        tmpReg32 &= ~FM_PCD_PLCR_GCR_STEN;

    WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->fmpl_gcr, tmpReg32);
    return E_OK;
}

t_Handle FM_PCD_PlcrProfileSet(t_Handle     h_FmPcd,
                               t_FmPcdPlcrProfileParams *p_ProfileParams)
{
    t_FmPcd                             *p_FmPcd;
    t_FmPcdPlcrRegs                     *p_FmPcdPlcrRegs;
    t_FmPcdPlcrProfileRegs              plcrProfileReg;
    uint32_t                            intFlags;
    uint16_t                            absoluteProfileId;
    t_Error                             err = E_OK;
    uint32_t                            tmpReg32;
    t_FmPcdPlcrProfile                  *p_Profile;

    SANITY_CHECK_RETURN_VALUE(h_FmPcd, E_INVALID_HANDLE, NULL);

    if (p_ProfileParams->modify)
    {
        p_Profile = (t_FmPcdPlcrProfile *)p_ProfileParams->id.h_Profile;
        p_FmPcd = p_Profile->h_FmPcd;
        absoluteProfileId = p_Profile->absoluteProfileId;
        if (absoluteProfileId >= FM_PCD_PLCR_NUM_ENTRIES)
        {
            REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("profileId too Big "));
            return NULL;
        }

        SANITY_CHECK_RETURN_VALUE(p_FmPcd->p_FmPcdPlcr, E_INVALID_HANDLE, NULL);

        /* Try lock profile using flag */
         if (!PlcrProfileFlagTryLock(p_Profile))
         {
             DBG(TRACE, ("Profile Try Lock - BUSY"));
             /* Signal to caller BUSY condition */
             p_ProfileParams->id.h_Profile = NULL;
             return NULL;
         }
   }
    else
    {
        p_FmPcd = (t_FmPcd*)h_FmPcd;

        SANITY_CHECK_RETURN_VALUE(p_FmPcd->p_FmPcdPlcr, E_INVALID_HANDLE, NULL);

        /* SMP: needs to be protected only if another core now changes the windows */
        err = FmPcdPlcrGetAbsoluteIdByProfileParams(h_FmPcd,
                                                    p_ProfileParams->id.newParams.profileType,
                                                    p_ProfileParams->id.newParams.h_FmPort,
                                                    p_ProfileParams->id.newParams.relativeProfileId,
                                                    &absoluteProfileId);
        if (err)
        {
             REPORT_ERROR(MAJOR, err, NO_MSG);
             return NULL;
        }

         if (absoluteProfileId >= FM_PCD_PLCR_NUM_ENTRIES)
         {
             REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("profileId too Big "));
             return NULL;
         }

         if (FmPcdPlcrIsProfileValid(p_FmPcd, absoluteProfileId))
         {
             REPORT_ERROR(MAJOR, E_ALREADY_EXISTS, ("Policer Profile is already used"));
             return NULL;
         }

         /* initialize profile struct */
         p_Profile = &p_FmPcd->p_FmPcdPlcr->profiles[absoluteProfileId];

         p_Profile->h_FmPcd = p_FmPcd;
         p_Profile->absoluteProfileId = absoluteProfileId;

         p_Profile->p_Lock = FmPcdAcquireLock(p_FmPcd);
         if (!p_Profile->p_Lock)
             REPORT_ERROR(MAJOR, E_NOT_AVAILABLE, ("FM Policer Profile lock obj!"));
    }

    SANITY_CHECK_RETURN_VALUE(!p_FmPcd->p_FmPcdDriverParam, E_INVALID_STATE, NULL);

    p_Profile->nextEngineOnGreen = p_ProfileParams->nextEngineOnGreen;
    memcpy(&p_Profile->paramsOnGreen, &(p_ProfileParams->paramsOnGreen), sizeof(u_FmPcdPlcrNextEngineParams));

    p_Profile->nextEngineOnYellow = p_ProfileParams->nextEngineOnYellow;
    memcpy(&p_Profile->paramsOnYellow, &(p_ProfileParams->paramsOnYellow), sizeof(u_FmPcdPlcrNextEngineParams));

    p_Profile->nextEngineOnRed = p_ProfileParams->nextEngineOnRed;
    memcpy(&p_Profile->paramsOnRed, &(p_ProfileParams->paramsOnRed), sizeof(u_FmPcdPlcrNextEngineParams));

    memset(&plcrProfileReg, 0, sizeof(t_FmPcdPlcrProfileRegs));

    /* build the policer profile registers */
    err =  BuildProfileRegs(h_FmPcd, p_ProfileParams, &plcrProfileReg);
    if (err)
    {
        REPORT_ERROR(MAJOR, err, NO_MSG);
        if (p_ProfileParams->modify)
            /* unlock */
            PlcrProfileFlagUnlock(p_Profile);
        if (!p_ProfileParams->modify &&
                p_Profile->p_Lock)
            /* release allocated Profile lock */
            FmPcdReleaseLock(p_FmPcd, p_Profile->p_Lock);
        return NULL;
    }

    if (p_FmPcd->h_Hc)
    {
         err = FmHcPcdPlcrSetProfile(p_FmPcd->h_Hc, (t_Handle)p_Profile, &plcrProfileReg);
         if (p_ProfileParams->modify)
             PlcrProfileFlagUnlock(p_Profile);
         if (err)
         {
             /* release the allocated scheme lock */
             if (!p_ProfileParams->modify &&
                     p_Profile->p_Lock)
                 FmPcdReleaseLock(p_FmPcd, p_Profile->p_Lock);

             return NULL;
         }
         if (!p_ProfileParams->modify)
             FmPcdPlcrValidateProfileSw(p_FmPcd,absoluteProfileId);
         return (t_Handle)p_Profile;
    }

    p_FmPcdPlcrRegs = p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs;
    SANITY_CHECK_RETURN_VALUE(p_FmPcdPlcrRegs, E_INVALID_HANDLE, NULL);

    intFlags = PlcrHwLock(p_FmPcd->p_FmPcdPlcr);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pemode , plcrProfileReg.fmpl_pemode);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pegnia , plcrProfileReg.fmpl_pegnia);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_peynia , plcrProfileReg.fmpl_peynia);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pernia , plcrProfileReg.fmpl_pernia);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pecir  , plcrProfileReg.fmpl_pecir);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pecbs  , plcrProfileReg.fmpl_pecbs);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pepepir_eir,plcrProfileReg.fmpl_pepepir_eir);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pepbs_ebs,plcrProfileReg.fmpl_pepbs_ebs);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pelts  , plcrProfileReg.fmpl_pelts);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pects  , plcrProfileReg.fmpl_pects);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pepts_ets,plcrProfileReg.fmpl_pepts_ets);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pegpc  , plcrProfileReg.fmpl_pegpc);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_peypc  , plcrProfileReg.fmpl_peypc);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_perpc  , plcrProfileReg.fmpl_perpc);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_perypc , plcrProfileReg.fmpl_perypc);
    WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_perrpc , plcrProfileReg.fmpl_perrpc);

    tmpReg32 = FmPcdPlcrBuildWritePlcrActionRegs(absoluteProfileId);
    WritePar(p_FmPcd, tmpReg32);

    PlcrHwUnlock(p_FmPcd->p_FmPcdPlcr, intFlags);

    if (!p_ProfileParams->modify)
        FmPcdPlcrValidateProfileSw(p_FmPcd,absoluteProfileId);
    else
        PlcrProfileFlagUnlock(p_Profile);

    return (t_Handle)p_Profile;
}

t_Error FM_PCD_PlcrProfileDelete(t_Handle h_Profile)
{
    t_FmPcdPlcrProfile  *p_Profile = (t_FmPcdPlcrProfile*)h_Profile;
    t_FmPcd             *p_FmPcd;
    uint16_t            profileIndx;
    uint32_t            tmpReg32, intFlags;
    t_Error             err;

    SANITY_CHECK_RETURN_ERROR(p_Profile, E_INVALID_HANDLE);
    p_FmPcd = p_Profile->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);

    profileIndx = p_Profile->absoluteProfileId;

    UpdateRequiredActionFlag(p_FmPcd, profileIndx, FALSE);

    FmPcdPlcrInvalidateProfileSw(p_FmPcd,profileIndx);

    if (p_FmPcd->h_Hc)
    {
        err = FmHcPcdPlcrDeleteProfile(p_FmPcd->h_Hc, h_Profile);
        if (p_Profile->p_Lock)
            /* release allocated Profile lock */
            FmPcdReleaseLock(p_FmPcd, p_Profile->p_Lock);

        return err;
    }

    intFlags = PlcrHwLock(p_FmPcd->p_FmPcdPlcr);
    WRITE_UINT32(p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs->profileRegs.fmpl_pemode, ~FM_PCD_PLCR_PEMODE_PI);

    tmpReg32 = FmPcdPlcrBuildWritePlcrActionRegs(profileIndx);
    WritePar(p_FmPcd, tmpReg32);
    PlcrHwUnlock(p_FmPcd->p_FmPcdPlcr, intFlags);


    if (p_Profile->p_Lock)
        /* release allocated Profile lock */
        FmPcdReleaseLock(p_FmPcd, p_Profile->p_Lock);

    /* we do not memset profile as all its fields are being re-initialized at "set",
     * plus its allocation information is still valid. */
    return E_OK;
}

/***************************************************/
/*............Policer Profile Counter..............*/
/***************************************************/
uint32_t FM_PCD_PlcrProfileGetCounter(t_Handle h_Profile, e_FmPcdPlcrProfileCounters counter)
{
    t_FmPcdPlcrProfile  *p_Profile = (t_FmPcdPlcrProfile*)h_Profile;
    t_FmPcd             *p_FmPcd;
    uint16_t            profileIndx;
    uint32_t            intFlags, counterVal = 0;
    t_FmPcdPlcrRegs     *p_FmPcdPlcrRegs;

    SANITY_CHECK_RETURN_ERROR(p_Profile, E_INVALID_HANDLE);
    p_FmPcd = p_Profile->h_FmPcd;
    SANITY_CHECK_RETURN_ERROR(p_FmPcd, E_INVALID_HANDLE);

    if (p_FmPcd->h_Hc)
        return FmHcPcdPlcrGetProfileCounter(p_FmPcd->h_Hc, h_Profile, counter);

    p_FmPcdPlcrRegs = p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs;
    SANITY_CHECK_RETURN_VALUE(p_FmPcdPlcrRegs, E_INVALID_HANDLE, 0);

    profileIndx = p_Profile->absoluteProfileId;

    if (profileIndx >= FM_PCD_PLCR_NUM_ENTRIES)
    {
        REPORT_ERROR(MAJOR, E_INVALID_VALUE, ("profileId too Big "));
        return 0;
    }
    intFlags = PlcrHwLock(p_FmPcd->p_FmPcdPlcr);
    WritePar(p_FmPcd, FmPcdPlcrBuildReadPlcrActionReg(profileIndx));

    switch (counter)
    {
        case e_FM_PCD_PLCR_PROFILE_GREEN_PACKET_TOTAL_COUNTER:
            counterVal = (GET_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pegpc));
            break;
        case e_FM_PCD_PLCR_PROFILE_YELLOW_PACKET_TOTAL_COUNTER:
            counterVal = GET_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_peypc);
            break;
        case e_FM_PCD_PLCR_PROFILE_RED_PACKET_TOTAL_COUNTER:
            counterVal = GET_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_perpc);
            break;
        case e_FM_PCD_PLCR_PROFILE_RECOLOURED_YELLOW_PACKET_TOTAL_COUNTER:
            counterVal = GET_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_perypc);
            break;
        case e_FM_PCD_PLCR_PROFILE_RECOLOURED_RED_PACKET_TOTAL_COUNTER:
            counterVal = GET_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_perrpc);
            break;
        default:
            REPORT_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
            break;
    }
    PlcrHwUnlock(p_FmPcd->p_FmPcdPlcr, intFlags);

    return counterVal;
}

t_Error FM_PCD_PlcrProfileSetCounter(t_Handle h_Profile, e_FmPcdPlcrProfileCounters counter, uint32_t value)
{
    t_FmPcdPlcrProfile  *p_Profile = (t_FmPcdPlcrProfile*)h_Profile;
    t_FmPcd             *p_FmPcd;
    uint16_t            profileIndx;
    uint32_t            tmpReg32, intFlags;
    t_FmPcdPlcrRegs     *p_FmPcdPlcrRegs;

    SANITY_CHECK_RETURN_ERROR(p_Profile, E_INVALID_HANDLE);

    p_FmPcd = p_Profile->h_FmPcd;
    profileIndx = p_Profile->absoluteProfileId;

    if (p_FmPcd->h_Hc)
        return FmHcPcdPlcrSetProfileCounter(p_FmPcd->h_Hc, h_Profile, counter, value);

    p_FmPcdPlcrRegs = p_FmPcd->p_FmPcdPlcr->p_FmPcdPlcrRegs;
    SANITY_CHECK_RETURN_ERROR(p_FmPcdPlcrRegs, E_INVALID_HANDLE);

    intFlags = PlcrHwLock(p_FmPcd->p_FmPcdPlcr);
    switch (counter)
    {
        case e_FM_PCD_PLCR_PROFILE_GREEN_PACKET_TOTAL_COUNTER:
             WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_pegpc, value);
             break;
        case e_FM_PCD_PLCR_PROFILE_YELLOW_PACKET_TOTAL_COUNTER:
             WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_peypc, value);
             break;
        case e_FM_PCD_PLCR_PROFILE_RED_PACKET_TOTAL_COUNTER:
             WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_perpc, value);
             break;
        case e_FM_PCD_PLCR_PROFILE_RECOLOURED_YELLOW_PACKET_TOTAL_COUNTER:
             WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_perypc ,value);
             break;
        case e_FM_PCD_PLCR_PROFILE_RECOLOURED_RED_PACKET_TOTAL_COUNTER:
             WRITE_UINT32(p_FmPcdPlcrRegs->profileRegs.fmpl_perrpc ,value);
             break;
        default:
            PlcrHwUnlock(p_FmPcd->p_FmPcdPlcr, intFlags);
            RETURN_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
    }

    /*  Activate the atomic write action by writing FMPL_PAR with: GO=1, RW=1, PSI=0, PNUM =
     *  Profile Number, PWSEL=0xFFFF (select all words).
     */
    tmpReg32 = FmPcdPlcrBuildWritePlcrActionReg(profileIndx);
    tmpReg32 |= FmPcdPlcrBuildCounterProfileReg(counter);
    WritePar(p_FmPcd, tmpReg32);
    PlcrHwUnlock(p_FmPcd->p_FmPcdPlcr, intFlags);

    return E_OK;
}
