/*
 * Copyright 2008-2015 Freescale Semiconductor Inc.
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
 @File          fm_macsec_secy.h

 @Description   FM MACSEC SecY internal structures and definitions.
*//***************************************************************************/
#ifndef __FM_MACSEC_SECY_H
#define __FM_MACSEC_SECY_H

#include "error_ext.h"
#include "std_ext.h"

#include "fm_macsec.h"


/**************************************************************************//**
 @Description       Exceptions
*//***************************************************************************/

#define FM_MACSEC_SECY_EX_FRAME_DISCARDED           0x80000000

#define GET_EXCEPTION_FLAG(bitMask, exception)  switch (exception){     \
    case e_FM_MACSEC_SECY_EX_FRAME_DISCARDED:                           \
        bitMask = FM_MACSEC_SECY_EX_FRAME_DISCARDED; break;             \
    default: bitMask = 0;break;}

/**************************************************************************//**
 @Description       Events
*//***************************************************************************/

#define FM_MACSEC_SECY_EV_NEXT_PN                        0x80000000

#define GET_EVENT_FLAG(bitMask, event)          switch (event){     \
    case e_FM_MACSEC_SECY_EV_NEXT_PN:                               \
        bitMask = FM_MACSEC_SECY_EV_NEXT_PN; break;                 \
    default: bitMask = 0;break;}

/**************************************************************************//**
 @Description       Defaults
*//***************************************************************************/

#define DEFAULT_exceptions                  (FM_MACSEC_SECY_EX_FRAME_DISCARDED)
#define DEFAULT_events                      (FM_MACSEC_SECY_EV_NEXT_PN)
#define DEFAULT_numOfTxSc                   1
#define DEFAULT_confidentialityEnable       FALSE
#define DEFAULT_confidentialityOffset       0
#define DEFAULT_sciInsertionMode            e_FM_MACSEC_SCI_INSERTION_MODE_EXPLICIT_SECTAG
#define DEFAULT_validateFrames              e_FM_MACSEC_VALID_FRAME_BEHAVIOR_STRICT
#define DEFAULT_replayEnable                FALSE
#define DEFAULT_replayWindow                0
#define DEFAULT_protectFrames               TRUE
#define DEFAULT_ptp                         FALSE

/**************************************************************************//**
 @Description       General defines
*//***************************************************************************/

#define SECY_AN_FREE_VALUE              MAX_NUM_OF_SA_PER_SC


typedef struct {
    e_ScSaId                            saId;
    bool                                active;
    union {
        t_FmMacsecSecYRxSaStatistics    rxSaStatistics;
        t_FmMacsecSecYTxSaStatistics    txSaStatistics;
    };
} t_SecYSa;

typedef struct {
    bool                                inUse;
    uint32_t                            scId;
    e_ScType                            type;
    uint8_t                             numOfSa;
    t_SecYSa                            sa[MAX_NUM_OF_SA_PER_SC];
    union {
        t_FmMacsecSecYRxScStatistics    rxScStatistics;
        t_FmMacsecSecYTxScStatistics    txScStatistics;
    };
} t_SecYSc;

typedef struct {
    t_FmMacsecSecYSCParams              txScParams;             /**< Tx SC Params */
} t_FmMacsecSecYDriverParam;

typedef struct {
    t_Handle                            h_FmMacsec;
    bool                                confidentialityEnable;  /**< TRUE  - confidentiality protection and integrity protection
                                                                     FALSE - no confidentiality protection, only integrity protection*/
    uint16_t                            confidentialityOffset;  /**< The number of initial octets of each MSDU without confidentiality protection
                                                                     common values are 0, 30, and 50 */
    bool                                replayProtect;          /**< replay protection function mode */
    uint32_t                            replayWindow;           /**< the size of the replay window */
    e_FmMacsecValidFrameBehavior        validateFrames;         /**< validation function mode */
    e_FmMacsecSciInsertionMode          sciInsertionMode;
    bool                                protectFrames;
    bool                                isPointToPoint;
    e_FmMacsecSecYCipherSuite           cipherSuite;            /**< Cipher suite to be used for this SecY */
    uint32_t                            numOfRxSc;              /**< Number of receive channels */
    uint32_t                            numOfTxSc;              /**< Number of transmit channels */
    t_SecYSc                            *p_RxSc;
    t_SecYSc                            *p_TxSc;
    uint32_t                            events;
    uint32_t                            exceptions;
    t_FmMacsecSecYExceptionsCallback    *f_Exception;           /**< TODO */
    t_FmMacsecSecYEventsCallback        *f_Event;               /**< TODO */
    t_Handle                            h_App;
    t_FmMacsecSecYStatistics            statistics;
    t_FmMacsecSecYDriverParam           *p_FmMacsecSecYDriverParam;
} t_FmMacsecSecY;


#endif /* __FM_MACSEC_SECY_H */
