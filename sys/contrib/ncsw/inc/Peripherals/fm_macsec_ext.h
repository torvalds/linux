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

/**************************************************************************//**
 @File          fm_macsec_ext.h

 @Description   FM MACSEC ...
*//***************************************************************************/
#ifndef __FM_MACSEC_EXT_H
#define __FM_MACSEC_EXT_H

#include "std_ext.h"


/**************************************************************************//**
 @Group         FM_grp Frame Manager API

 @Description   FM API functions, definitions and enums

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         FM_MACSEC_grp FM MACSEC

 @Description   FM MACSEC API functions, definitions and enums

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   MACSEC Exceptions
*//***************************************************************************/
typedef enum e_FmMacsecExceptions {
    e_FM_MACSEC_EX_SINGLE_BIT_ECC,          /**< Single bit ECC error */
    e_FM_MACSEC_EX_MULTI_BIT_ECC            /**< Multi bit ECC error */
} e_FmMacsecExceptions;


/**************************************************************************//**
 @Group         FM_MACSEC_init_grp FM-MACSEC Initialization Unit

 @Description   FM MACSEC Initialization Unit

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      t_FmMacsecExceptionsCallback

 @Description   Exceptions user callback routine, will be called upon an
                exception passing the exception identification.

 @Param[in]     h_App       A handle to an application layer object; This handle
                            will be passed by the driver upon calling this callback.
 @Param[in]     exception   The exception.
*//***************************************************************************/
typedef void (t_FmMacsecExceptionsCallback) ( t_Handle                  h_App,
                                              e_FmMacsecExceptions      exception);


/**************************************************************************//**
 @Description   FM MACSEC config input
*//***************************************************************************/
typedef struct t_FmMacsecParams {
    t_Handle                                h_Fm;               /**< A handle to the FM object related to */
    bool                                    guestMode;          /**< Partition-id */
    union {
        struct {
            uint8_t                         fmMacId;            /**< FM MAC id */
        } guestParams;

        struct {
            uintptr_t                       baseAddr;           /**< Base of memory mapped FM MACSEC registers */
            t_Handle                        h_FmMac;            /**< A handle to the FM MAC object  related to */
            t_FmMacsecExceptionsCallback    *f_Exception;       /**< Exception Callback Routine         */
            t_Handle                        h_App;              /**< A handle to an application layer object; This handle will
                                                                     be passed by the driver upon calling the above callbacks */
        } nonGuestParams;
    };
} t_FmMacsecParams;

/**************************************************************************//**
 @Function      FM_MACSEC_Config

 @Description   Creates descriptor for the FM MACSEC module;

                The routine returns a handle (descriptor) to the FM MACSEC object;
                This descriptor must be passed as first parameter to all other
                FM MACSEC function calls;

                No actual initialization or configuration of FM MACSEC hardware is
                done by this routine.

 @Param[in]     p_FmMacsecParam     Pointer to data structure of parameters.

 @Retval        Handle to FM MACSEC object, or NULL for Failure.
*//***************************************************************************/
t_Handle FM_MACSEC_Config(t_FmMacsecParams *p_FmMacsecParam);

/**************************************************************************//**
 @Function      FM_MACSEC_Init

 @Description   Initializes the FM MACSEC module.

 @Param[in]     h_FmMacsec      FM MACSEC module descriptor.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_MACSEC_Init(t_Handle h_FmMacsec);

/**************************************************************************//**
 @Function      FM_MACSEC_Free

 @Description   Frees all resources that were assigned to FM MACSEC module;

                Calling this routine invalidates the descriptor.

 @Param[in]     h_FmMacsec      FM MACSEC module descriptor.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_MACSEC_Free(t_Handle h_FmMacsec);


/**************************************************************************//**
 @Group         FM_MACSEC_advanced_init_grp    FM-MACSEC Advanced Configuration Unit

 @Description   Configuration functions used to change default values.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   enum for unknown sci frame treatment
*//***************************************************************************/
typedef enum e_FmMacsecUnknownSciFrameTreatment {
    e_FM_MACSEC_UNKNOWN_SCI_FRAME_TREATMENT_DISCARD_BOTH = 0,                                               /**< Controlled port - Strict mode */
    e_FM_MACSEC_UNKNOWN_SCI_FRAME_TREATMENT_DISCARD_UNCONTROLLED_DELIVER_OR_DISCARD_CONTROLLED,             /**< If C bit clear deliver on controlled port, else discard
                                                                                                                 Controlled port - Check or Disable mode */
    e_FM_MACSEC_UNKNOWN_SCI_FRAME_TREATMENT_DELIVER_UNCONTROLLED_DISCARD_CONTROLLED,                        /**< Controlled port - Strict mode */
    e_FM_MACSEC_UNKNOWN_SCI_FRAME_TREATMENT_DELIVER_OR_DISCARD_UNCONTROLLED_DELIVER_OR_DISCARD_CONTROLLED   /**< If C bit set deliver on uncontrolled port and discard on controlled port,
                                                                                                                 else discard on uncontrolled port and deliver on controlled port
                                                                                                                 Controlled port - Check or Disable mode */
} e_FmMacsecUnknownSciFrameTreatment;

/**************************************************************************//**
 @Description   enum for untag frame treatment
*//***************************************************************************/
typedef enum e_FmMacsecUntagFrameTreatment {
    e_FM_MACSEC_UNTAG_FRAME_TREATMENT_DELIVER_UNCONTROLLED_DISCARD_CONTROLLED = 0,                    /**< Controlled port - Strict mode */
    e_FM_MACSEC_UNTAG_FRAME_TREATMENT_DISCARD_BOTH,                                                   /**< Controlled port - Strict mode */
    e_FM_MACSEC_UNTAG_FRAME_TREATMENT_DISCARD_UNCONTROLLED_DELIVER_CONTROLLED_UNMODIFIED              /**< Controlled port - Strict mode */
} e_FmMacsecUntagFrameTreatment;

/**************************************************************************//**
 @Function      FM_MACSEC_ConfigUnknownSciFrameTreatment

 @Description   Change the treatment for received frames with unknown sci from its default
                configuration [DEFAULT_unknownSciFrameTreatment].

 @Param[in]     h_FmMacsec      FM MACSEC module descriptor.
 @Param[in]     treatMode       The selected mode.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_Config() and before FM_MACSEC_Init().
*//***************************************************************************/
t_Error FM_MACSEC_ConfigUnknownSciFrameTreatment(t_Handle h_FmMacsec, e_FmMacsecUnknownSciFrameTreatment treatMode);

/**************************************************************************//**
 @Function      FM_MACSEC_ConfigInvalidTagsFrameTreatment

 @Description   Change the treatment for received frames with invalid tags or
                a zero value PN or an invalid ICV from its default configuration
                [DEFAULT_invalidTagsFrameTreatment].

 @Param[in]     h_FmMacsec              FM MACSEC module descriptor.
 @Param[in]     deliverUncontrolled     If True deliver on the uncontrolled port, else discard;
                                        In both cases discard on the controlled port;
                                        this provide Strict, Check or Disable mode.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_Config() and before FM_MACSEC_Init().
*//***************************************************************************/
t_Error FM_MACSEC_ConfigInvalidTagsFrameTreatment(t_Handle h_FmMacsec, bool deliverUncontrolled);

/**************************************************************************//**
 @Function      FM_MACSEC_ConfigEncryptWithNoChangedTextFrameTreatment

 @Description   Change the treatment for received frames with the Encryption bit
                set and the Changed Text bit clear from its default configuration
                [DEFAULT_encryptWithNoChangedTextFrameTreatment].

 @Param[in]     h_FmMacsec              FM MACSEC module descriptor.
 @Param[in]     discardUncontrolled     If True discard on the uncontrolled port, else deliver;
                                        In both cases discard on the controlled port;
                                        this provide Strict, Check or Disable mode.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_Config() and before FM_MACSEC_Init().
*//***************************************************************************/
t_Error FM_MACSEC_ConfigEncryptWithNoChangedTextFrameTreatment(t_Handle h_FmMacsec, bool discardUncontrolled);

/**************************************************************************//**
 @Function      FM_MACSEC_ConfigChangedTextWithNoEncryptFrameTreatment

 @Description   Change the treatment for received frames with the Encryption bit
                clear and the Changed Text bit set from its default configuration
                [DEFAULT_changedTextWithNoEncryptFrameTreatment].

 @Param[in]     h_FmMacsec              FM MACSEC module descriptor.
 @Param[in]     deliverUncontrolled     If True deliver on the uncontrolled port, else discard;
                                        In both cases discard on the controlled port;
                                        this provide Strict, Check or Disable mode.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_Config() and before FM_MACSEC_Init().
*//***************************************************************************/
t_Error FM_MACSEC_ConfigChangedTextWithNoEncryptFrameTreatment(t_Handle h_FmMacsec, bool deliverUncontrolled);

/**************************************************************************//**
 @Function      FM_MACSEC_ConfigUntagFrameTreatment

 @Description   Change the treatment for received frames without the MAC security tag (SecTAG)
                from its default configuration [DEFAULT_untagFrameTreatment].

 @Param[in]     h_FmMacsec     FM MACSEC module descriptor.
 @Param[in]     treatMode      The selected mode.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_Config() and before FM_MACSEC_Init().
*//***************************************************************************/
t_Error FM_MACSEC_ConfigUntagFrameTreatment(t_Handle h_FmMacsec, e_FmMacsecUntagFrameTreatment treatMode);

/**************************************************************************//**
 @Function      FM_MACSEC_ConfigOnlyScbIsSetFrameTreatment

 @Description   Change the treatment for received frames with only SCB bit set
                from its default configuration [DEFAULT_onlyScbIsSetFrameTreatment].

 @Param[in]     h_FmMacsec              FM MACSEC module descriptor.
 @Param[in]     deliverUncontrolled     If True deliver on the uncontrolled port, else discard;
                                        In both cases discard on the controlled port;
                                        this provide Strict, Check or Disable mode.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_Config() and before FM_MACSEC_Init().
*//***************************************************************************/
t_Error FM_MACSEC_ConfigOnlyScbIsSetFrameTreatment(t_Handle h_FmMacsec, bool deliverUncontrolled);

/**************************************************************************//**
 @Function      FM_MACSEC_ConfigPnExhaustionThreshold

 @Description   It's provide the ability to configure a PN exhaustion threshold;
                When the NextPn crosses this value an interrupt event
                is asserted to warn that the active SA should re-key.

 @Param[in]     h_FmMacsec     FM MACSEC module descriptor.
 @Param[in]     pnExhThr       If the threshold is reached, an interrupt event
                               is asserted to re-key.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_Config() and before FM_MACSEC_Init().
*//***************************************************************************/
t_Error FM_MACSEC_ConfigPnExhaustionThreshold(t_Handle h_FmMacsec, uint32_t pnExhThr);

/**************************************************************************//**
 @Function      FM_MACSEC_ConfigKeysUnreadable

 @Description   Turn on privacy mode; All the keys and their hash values can't be read any more;
                Can not be cleared unless hard reset.

 @Param[in]     h_FmMacsec         FM MACSEC module descriptor.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_Config() and before FM_MACSEC_Init().
*//***************************************************************************/
t_Error FM_MACSEC_ConfigKeysUnreadable(t_Handle h_FmMacsec);

/**************************************************************************//**
 @Function      FM_MACSEC_ConfigSectagWithoutSCI

 @Description   Promise that all generated Sectag will be without SCI included.

 @Param[in]     h_FmMacsec         FM MACSEC module descriptor.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_Config() and before FM_MACSEC_Init().
*//***************************************************************************/
t_Error FM_MACSEC_ConfigSectagWithoutSCI(t_Handle h_FmMacsec);

/**************************************************************************//**
 @Function      FM_MACSEC_ConfigException

 @Description   Calling this routine changes the internal driver data base
                from its default selection of exceptions enablement;
                By default all exceptions are enabled.

 @Param[in]     h_FmMacsec      FM MACSEC module descriptor.
 @Param[in]     exception       The exception to be selected.
 @Param[in]     enable          TRUE to enable interrupt, FALSE to mask it.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_Config() and before FM_MACSEC_Init().
*//***************************************************************************/
t_Error FM_MACSEC_ConfigException(t_Handle h_FmMacsec, e_FmMacsecExceptions exception, bool enable);

/** @} */ /* end of FM_MACSEC_advanced_init_grp group */
/** @} */ /* end of FM_MACSEC_init_grp group */


/**************************************************************************//**
 @Group         FM_MACSEC_runtime_control_grp FM-MACSEC Runtime Control Data Unit

 @Description   FM MACSEC runtime control data unit API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      FM_MACSEC_GetRevision

 @Description   Return MACSEC HW chip revision

 @Param[in]     h_FmMacsec         FM MACSEC module descriptor.
 @Param[out]    p_MacsecRevision   MACSEC revision as defined by the chip.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only after FM_MACSEC_Init().
*//***************************************************************************/
t_Error FM_MACSEC_GetRevision(t_Handle h_FmMacsec, uint32_t *p_MacsecRevision);

/**************************************************************************//**
 @Function      FM_MACSEC_Enable

 @Description   This routine should be called after MACSEC is initialized for enabling all
                MACSEC engines according to their existing configuration.

 @Param[in]     h_FmMacsec         FM MACSEC module descriptor.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_Init() and when MACSEC is disabled.
*//***************************************************************************/
t_Error FM_MACSEC_Enable(t_Handle h_FmMacsec);

/**************************************************************************//**
 @Function      FM_MACSEC_Disable

 @Description   This routine may be called when MACSEC is enabled in order to
                disable all MACSEC engines; The MACSEC is working in bypass mode.

 @Param[in]     h_FmMacsec         FM MACSEC module descriptor.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_Init() and when MACSEC is enabled.
*//***************************************************************************/
t_Error FM_MACSEC_Disable(t_Handle h_FmMacsec);

/**************************************************************************//**
 @Function      FM_MACSEC_SetException

 @Description   Calling this routine enables/disables the specified exception.

 @Param[in]     h_FmMacsec      FM MACSEC module descriptor.
 @Param[in]     exception       The exception to be selected.
 @Param[in]     enable          TRUE to enable interrupt, FALSE to mask it.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SetException(t_Handle h_FmMacsec, e_FmMacsecExceptions exception, bool enable);

#if (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0))
/**************************************************************************//**
 @Function      FM_MACSEC_DumpRegs

 @Description   Dump internal registers.

 @Param[in]     h_FmMacsec  - FM MACSEC module descriptor.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only after FM_MACSEC_Init().
*//***************************************************************************/
t_Error FM_MACSEC_DumpRegs(t_Handle h_FmMacsec);
#endif /* (defined(DEBUG_ERRORS) && ... */

#ifdef VERIFICATION_SUPPORT
/********************* VERIFICATION ONLY ********************************/
/**************************************************************************//**
 @Function      FM_MACSEC_BackdoorSet

 @Description   Set register of the MACSEC memory map

 @Param[in]     h_FmMacsec          FM MACSEC module descriptor.
 @Param[out]    offset              Register offset.
 @Param[out]    value               Value to write.


 @Return        None

 @Cautions      Allowed only following FM_MACSEC_Init().
*//***************************************************************************/
t_Error FM_MACSEC_BackdoorSet(t_Handle h_FmMacsec, uint32_t offset, uint32_t value);

/**************************************************************************//**
 @Function      FM_MACSEC_BackdoorGet

 @Description   Read from register of the MACSEC memory map.

 @Param[in]     h_FmMacsec          FM MACSEC module descriptor.
 @Param[out]    offset              Register offset.

 @Return        Value read

 @Cautions      Allowed only following FM_MACSEC_Init().
*//***************************************************************************/
uint32_t FM_MACSEC_BackdoorGet(t_Handle h_FmMacsec, uint32_t offset);
#endif /* VERIFICATION_SUPPORT */

/** @} */ /* end of FM_MACSEC_runtime_control_grp group */


/**************************************************************************//**
 @Group         FM_MACSEC_SECY_grp FM-MACSEC SecY

 @Description   FM-MACSEC SecY API functions, definitions and enums

 @{
*//***************************************************************************/

typedef uint8_t     macsecSAKey_t[32];
typedef uint64_t    macsecSCI_t;
typedef uint8_t     macsecAN_t;

/**************************************************************************//**
@Description   MACSEC SECY Cipher Suite
*//***************************************************************************/
typedef enum e_FmMacsecSecYCipherSuite {
    e_FM_MACSEC_SECY_GCM_AES_128 = 0,       /**< GCM-AES-128 */
#if (DPAA_VERSION >= 11)
    e_FM_MACSEC_SECY_GCM_AES_256            /**< GCM-AES-256 */
#endif /* (DPAA_VERSION >= 11) */
} e_FmMacsecSecYCipherSuite;

/**************************************************************************//**
 @Description   MACSEC SECY Exceptions
*//***************************************************************************/
typedef enum e_FmMacsecSecYExceptions {
    e_FM_MACSEC_SECY_EX_FRAME_DISCARDED     /**< Frame  Discarded */
} e_FmMacsecSecYExceptions;

/**************************************************************************//**
 @Description   MACSEC SECY Events
*//***************************************************************************/
typedef enum e_FmMacsecSecYEvents {
    e_FM_MACSEC_SECY_EV_NEXT_PN             /**< Next Packet Number exhaustion threshold reached */
} e_FmMacsecSecYEvents;

/**************************************************************************//**
 @Collection   MACSEC SECY Frame Discarded Descriptor error
*//***************************************************************************/
typedef uint8_t    macsecTxScFrameDiscardedErrSelect_t; /**< typedef for defining Frame Discarded Descriptor errors */

#define FM_MACSEC_SECY_TX_SC_FRM_DISCAR_ERR_NEXT_PN_ZERO              0x8000  /**< NextPn == 0 */
#define FM_MACSEC_SECY_TX_SC_FRM_DISCAR_ERR_SC_DISBALE                0x4000  /**< SC is disable */
/* @} */

/**************************************************************************//**
 @Function      t_FmMacsecSecYExceptionsCallback

 @Description   Exceptions user callback routine, will be called upon an
                exception passing the exception identification.

 @Param[in]     h_App       A handle to an application layer object; This handle
                            will be passed by the driver upon calling this callback.
 @Param[in]     exception   The exception.
*//***************************************************************************/
typedef void (t_FmMacsecSecYExceptionsCallback) ( t_Handle                  h_App,
                                                  e_FmMacsecSecYExceptions  exception);

/**************************************************************************//**
 @Function      t_FmMacsecSecYEventsCallback

 @Description   Events user callback routine, will be called upon an
                event passing the event identification.

 @Param[in]     h_App       A handle to an application layer object; This handle
                            will be passed by the driver upon calling this callback.
 @Param[in]     event       The event.
*//***************************************************************************/
typedef void (t_FmMacsecSecYEventsCallback) ( t_Handle                  h_App,
                                              e_FmMacsecSecYEvents      event);

/**************************************************************************//**
 @Description   RFC2863 MIB
*//***************************************************************************/
typedef struct t_MIBStatistics {
    uint64_t  ifInOctets;              /**< Total number of byte received */
    uint64_t  ifInPkts;                /**< Total number of packets received */
    uint64_t  ifInMcastPkts;           /**< Total number of multicast frame received */
    uint64_t  ifInBcastPkts;           /**< Total number of broadcast frame received */
    uint64_t  ifInDiscards;            /**< Frames received, but discarded due to problems within the MAC RX :
                                               - InPktsNoTag,
                                               - InPktsLate,
                                               - InPktsOverrun */
    uint64_t  ifInErrors;              /**< Number of frames received with error:
                                               - InPktsBadTag,
                                               - InPktsNoSCI,
                                               - InPktsNotUsingSA
                                               - InPktsNotValid */
    uint64_t  ifOutOctets;             /**< Total number of byte sent */
    uint64_t  ifOutPkts;               /**< Total number of packets sent */
    uint64_t  ifOutMcastPkts;          /**< Total number of multicast frame sent */
    uint64_t  ifOutBcastPkts;          /**< Total number of multicast frame sent */
    uint64_t  ifOutDiscards;           /**< Frames received, but discarded due to problems within the MAC TX N/A! */
    uint64_t  ifOutErrors;             /**< Number of frames transmitted with error:
                                               - FIFO Overflow Error
                                               - FIFO Underflow Error
                                               - Other */
} t_MIBStatistics;

/**************************************************************************//**
 @Description   MACSEC SecY Rx SA Statistics
*//***************************************************************************/
typedef struct t_FmMacsecSecYRxSaStatistics {
    uint32_t            inPktsOK;               /**< The number of frames with resolved SCI, have passed all
                                                     frame validation frame validation with the validateFrame not set to disable */
    uint32_t            inPktsInvalid;          /**< The number of frames with resolved SCI, that have failed frame
                                                     validation with the validateFrame set to check */
    uint32_t            inPktsNotValid;         /**< The number of frames with resolved SCI, discarded on the controlled port,
                                                     that have failed frame validation with the validateFrame set to strict or the c bit is set */
    uint32_t            inPktsNotUsingSA;       /**< The number of frames received with resolved SCI and discarded on disabled or
                                                     not provisioned SA with validateFrame in the strict mode or the C bit is set */
    uint32_t            inPktsUnusedSA;         /**< The number of frames received with resolved SCI on disabled or not provisioned SA
                                                     with validateFrame not in the strict mode and the C bit is cleared */
} t_FmMacsecSecYRxSaStatistics;

/**************************************************************************//**
 @Description   MACSEC SecY Tx SA Statistics
*//***************************************************************************/
typedef struct t_FmMacsecSecYTxSaStatistics {
    uint64_t            outPktsProtected;       /**< The number of frames, that the user of the controlled port requested to
                                                     be transmitted, which were integrity protected */
    uint64_t            outPktsEncrypted;       /**< The number of frames, that the user of the controlled port requested to
                                                     be transmitted, which were confidentiality protected */
} t_FmMacsecSecYTxSaStatistics;

/**************************************************************************//**
 @Description   MACSEC SecY Rx SC Statistics
*//***************************************************************************/
typedef struct t_FmMacsecSecYRxScStatistics {
    uint64_t            inPktsUnchecked;        /**< The number of frames with resolved SCI, delivered to the user of a controlled port,
                                                     that are not validated with the validateFrame set to disable */
    uint64_t            inPktsDelayed;          /**< The number of frames with resolved SCI, delivered to the user of a controlled port,
                                                     that have their PN smaller than the lowest_PN with the validateFrame set to
                                                     disable or replayProtect disabled */
    uint64_t            inPktsLate;             /**< The number of frames with resolved SCI, discarded on the controlled port,
                                                     that have their PN smaller than the lowest_PN with the validateFrame set to
                                                     Check or Strict and replayProtect enabled */
    uint64_t            inPktsOK;               /**< The number of frames with resolved SCI, have passed all
                                                     frame validation frame validation with the validateFrame not set to disable */
    uint64_t            inPktsInvalid;          /**< The number of frames with resolved SCI, that have failed frame
                                                     validation with the validateFrame set to check */
    uint64_t            inPktsNotValid;         /**< The number of frames with resolved SCI, discarded on the controlled port,
                                                     that have failed frame validation with the validateFrame set to strict or the c bit is set */
    uint64_t            inPktsNotUsingSA;       /**< The number of frames received with resolved SCI and discarded on disabled or
                                                     not provisioned SA with validateFrame in the strict mode or the C bit is set */
    uint64_t            inPktsUnusedSA;         /**< The number of frames received with resolved SCI on disabled or not provisioned SA
                                                     with validateFrame not in the strict mode and the C bit is cleared */
} t_FmMacsecSecYRxScStatistics;

/**************************************************************************//**
 @Description   MACSEC SecY Tx SC Statistics
*//***************************************************************************/
typedef struct t_FmMacsecSecYTxScStatistics {
    uint64_t            outPktsProtected;       /**< The number of frames, that the user of the controlled port requested to
                                                     be transmitted, which were integrity protected */
    uint64_t            outPktsEncrypted;       /**< The number of frames, that the user of the controlled port requested to
                                                     be transmitted, which were confidentiality protected */
} t_FmMacsecSecYTxScStatistics;

/**************************************************************************//**
 @Description   MACSEC SecY Statistics
*//***************************************************************************/
typedef struct t_FmMacsecSecYStatistics {
    t_MIBStatistics     mibCtrlStatistics;      /**< Controlled port MIB statistics */
    t_MIBStatistics     mibNonCtrlStatistics;   /**< Uncontrolled port MIB statistics */
/* Frame verification statistics */
    uint64_t            inPktsUntagged;         /**< The number of received packets without the MAC security tag
                                                     (SecTAG) with validateFrames which is not in the strict mode */
    uint64_t            inPktsNoTag;            /**< The number of received packets discarded without the
                                                     MAC security tag (SecTAG) with validateFrames which is in the strict mode */
    uint64_t            inPktsBadTag;           /**< The number of received packets discarded with an invalid
                                                     SecTAG or a zero value PN or an invalid ICV */
    uint64_t            inPktsUnknownSCI;       /**< The number of received packets with unknown SCI with the
                                                     condition : validateFrames is not in the strict mode and the
                                                     C bit in the SecTAG is not set */
    uint64_t            inPktsNoSCI;            /**< The number of received packets discarded with unknown SCI
                                                     information with the condition : validateFrames is in the strict mode
                                                     or the C bit in the SecTAG is set */
    uint64_t            inPktsOverrun;          /**< The number of packets discarded because the number of
                                                     received packets exceeded the cryptographic performance capabilities */
/* Frame validation statistics */
    uint64_t            inOctetsValidated;      /**< The number of octets of plaintext recovered from received frames with
                                                     resolved SCI that were integrity protected but not encrypted */
    uint64_t            inOctetsDecrypted;      /**< The number of octets of plaintext recovered from received frames with
                                                     resolved SCI that were integrity protected and encrypted */
/* Frame generation statistics */
    uint64_t            outPktsUntagged;        /**< The number of frames, that the user of the controlled port requested to
                                                     be transmitted, with protectFrame false */
    uint64_t            outPktsTooLong;         /**< The number of frames, that the user of the controlled port requested to
                                                     be transmitted, discarded due to length being larger than Maximum Frame Length (MACSEC_MFL) */
/* Frame protection statistics */
    uint64_t            outOctetsProtected;     /**< The number of octets of User Data in transmitted frames that were
                                                     integrity protected but not encrypted */
    uint64_t            outOctetsEncrypted;     /**< The number of octets of User Data in transmitted frames that were
                                                     both integrity protected and encrypted */
} t_FmMacsecSecYStatistics;


/**************************************************************************//**
 @Description   MACSEC SecY SC Params
*//***************************************************************************/
typedef struct t_FmMacsecSecYSCParams {
    macsecSCI_t                 sci;            /**< The secure channel identification of the SC */
    e_FmMacsecSecYCipherSuite   cipherSuite;    /**< Cipher suite to be used for the SC */
} t_FmMacsecSecYSCParams;

/**************************************************************************//**
 @Group         FM_MACSEC_SECY_init_grp FM-MACSEC SecY Initialization Unit

 @Description   FM-MACSEC SecY Initialization Unit

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   enum for validate frames
*//***************************************************************************/
typedef enum e_FmMacsecValidFrameBehavior {
    e_FM_MACSEC_VALID_FRAME_BEHAVIOR_DISABLE = 0,   /**< disable the validation function */
    e_FM_MACSEC_VALID_FRAME_BEHAVIOR_CHECK,         /**< enable the validation function but only for checking
                                                         without filtering out invalid frames */
    e_FM_MACSEC_VALID_FRAME_BEHAVIOR_STRICT         /**< enable the validation function and also strictly filter
                                                         out those invalid frames */
} e_FmMacsecValidFrameBehavior;

/**************************************************************************//**
 @Description   enum for sci insertion
*//***************************************************************************/
typedef enum e_FmMacsecSciInsertionMode {
    e_FM_MACSEC_SCI_INSERTION_MODE_EXPLICIT_SECTAG = 0, /**< explicit sci in the sectag */
    e_FM_MACSEC_SCI_INSERTION_MODE_EXPLICIT_MAC_SA,     /**< mac sa is overwritten with the sci*/
    e_FM_MACSEC_SCI_INSERTION_MODE_IMPLICT_PTP          /**< implicit point-to-point sci (pre-shared) */
} e_FmMacsecSciInsertionMode;

/**************************************************************************//**
 @Description   FM MACSEC SecY config input
*//***************************************************************************/
typedef struct t_FmMacsecSecYParams {
    t_Handle                                    h_FmMacsec;             /**< A handle to the FM MACSEC object */
    t_FmMacsecSecYSCParams                      txScParams;             /**< Tx SC Params */
    uint32_t                                    numReceiveChannels;     /**< Number of receive channels dedicated to this SecY */
    t_FmMacsecSecYExceptionsCallback            *f_Exception;           /**< Callback routine to be called by the driver upon SecY exception */
    t_FmMacsecSecYEventsCallback                *f_Event;               /**< Callback routine to be called by the driver upon SecY event */
    t_Handle                                    h_App;                  /**< A handle to an application layer object; This handle will
                                                                             be passed by the driver upon calling the above callbacks */
} t_FmMacsecSecYParams;

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_Config

 @Description   Creates descriptor for the FM MACSEC SECY module;

                The routine returns a handle (descriptor) to the FM MACSEC SECY object;
                This descriptor must be passed as first parameter to all other
                FM MACSEC SECY function calls;
                No actual initialization or configuration of FM MACSEC SecY hardware is
                done by this routine.

 @Param[in]     p_FmMacsecSecYParam     Pointer to data structure of parameters.

 @Return        Handle to FM MACSEC SECY object, or NULL for Failure.
*//***************************************************************************/
t_Handle FM_MACSEC_SECY_Config(t_FmMacsecSecYParams *p_FmMacsecSecYParam);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_Init

 @Description   Initializes the FM MACSEC SECY module.

 @Param[in]     h_FmMacsecSecY  FM MACSEC SECY module descriptor.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_MACSEC_SECY_Init(t_Handle h_FmMacsecSecY);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_Free

 @Description   Frees all resources that were assigned to FM MACSEC SECY module.

                Calling this routine invalidates the descriptor.

 @Param[in]     h_FmMacsecSecY  FM MACSEC SECY module descriptor.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_MACSEC_SECY_Free(t_Handle h_FmMacsecSecY);

/**************************************************************************//**
 @Group         FM_MACSEC_SECY_advanced_init_grp  FM-MACSEC SecY Advanced Configuration Unit

 @Description   Configuration functions used to change default values.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_ConfigSciInsertionMode

 @Description   Calling this routine changes the SCI-insertion-mode in the
                internal driver data base from its default configuration
                [DEFAULT_sciInsertionMode]

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     sciInsertionMode    Sci insertion mode

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Config() and before FM_MACSEC_SECY_Init();

*//***************************************************************************/
t_Error FM_MACSEC_SECY_ConfigSciInsertionMode(t_Handle h_FmMacsecSecY, e_FmMacsecSciInsertionMode sciInsertionMode);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_ConfigProtectFrames

 @Description   Calling this routine changes the protect-frame mode in the
                internal driver data base from its default configuration
                [DEFAULT_protectFrames]

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     protectFrames       If FALSE, frames are transmitted without modification

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Config() and before FM_MACSEC_SECY_Init();

*//***************************************************************************/
t_Error FM_MACSEC_SECY_ConfigProtectFrames(t_Handle h_FmMacsecSecY, bool protectFrames);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_ConfigReplayWindow

 @Description   Calling this routine changes the replay-window settings in the
                internal driver data base from its default configuration
                [DEFAULT_replayEnable], [DEFAULT_replayWindow]

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     replayProtect;      Replay protection function mode
 @Param[in]     replayWindow;       The size of the replay window

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Config() and before FM_MACSEC_SECY_Init();

*//***************************************************************************/
t_Error FM_MACSEC_SECY_ConfigReplayWindow(t_Handle h_FmMacsecSecY, bool replayProtect, uint32_t replayWindow);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_ConfigValidationMode

 @Description   Calling this routine changes the frame-validation-behavior mode
                in the internal driver data base from its default configuration
                [DEFAULT_validateFrames]

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     validateFrames      Validation function mode

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Config() and before FM_MACSEC_SECY_Init();

*//***************************************************************************/
t_Error FM_MACSEC_SECY_ConfigValidationMode(t_Handle h_FmMacsecSecY, e_FmMacsecValidFrameBehavior validateFrames);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_ConfigConfidentiality

 @Description   Calling this routine changes the confidentiality settings in the
                internal driver data base from its default configuration
                [DEFAULT_confidentialityEnable], [DEFAULT_confidentialityOffset]

 @Param[in]     h_FmMacsecSecY          FM MACSEC SECY module descriptor.
 @Param[in]     confidentialityEnable   TRUE  - confidentiality protection and integrity protection
                                        FALSE - no confidentiality protection, only integrity protection
 @Param[in]     confidentialityOffset   The number of initial octets of each MSDU without confidentiality protection
                                        common values are 0, 30, and 50

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Config() and before FM_MACSEC_SECY_Init();

*//***************************************************************************/
t_Error FM_MACSEC_SECY_ConfigConfidentiality(t_Handle h_FmMacsecSecY, bool confidentialityEnable, uint16_t confidentialityOffset);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_ConfigPointToPoint

 @Description   configure this SecY to work in point-to-point mode, means that
                it will have only one rx sc;

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Config() and before FM_MACSEC_SECY_Init();
                Can be called only once in a system; only the first secY that will call this
                routine will be able to operate in Point-To-Point mode.
*//***************************************************************************/
t_Error FM_MACSEC_SECY_ConfigPointToPoint(t_Handle h_FmMacsecSecY);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_ConfigException

 @Description   Calling this routine changes the internal driver data base
                from its default selection of exceptions enablement;
                By default all exceptions are enabled.

 @Param[in]     h_FmMacsecSecY  FM MACSEC SECY module descriptor.
 @Param[in]     exception       The exception to be selected.
 @Param[in]     enable          TRUE to enable interrupt, FALSE to mask it.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Config() and before FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_ConfigException(t_Handle h_FmMacsecSecY, e_FmMacsecSecYExceptions exception, bool enable);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_ConfigEvent

 @Description   Calling this routine changes the internal driver data base
                from its default selection of events enablement;
                By default all events are enabled.

 @Param[in]     h_FmMacsecSecY  FM MACSEC SECY module descriptor.
 @Param[in]     event           The event to be selected.
 @Param[in]     enable          TRUE to enable interrupt, FALSE to mask it.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Config() and before FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_ConfigEvent(t_Handle h_FmMacsecSecY, e_FmMacsecSecYEvents event, bool enable);

/** @} */ /* end of FM_MACSEC_SECY_advanced_init_grp group */
/** @} */ /* end of FM_MACSEC_SECY_init_grp group */


/**************************************************************************//**
 @Group         FM_MACSEC_SECY_runtime_control_grp FM-MACSEC SecY Runtime Control Unit

 @Description   FM MACSEC SECY Runtime control unit API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_CreateRxSc

 @Description   Create a receive secure channel.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     scParams            secure channel params.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Handle FM_MACSEC_SECY_CreateRxSc(t_Handle h_FmMacsecSecY, t_FmMacsecSecYSCParams *p_ScParams);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_DeleteRxSc

 @Description   Deleting an initialized secure channel.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     h_Sc                SC handle as returned by FM_MACSEC_SECY_CreateRxSc.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_CreateRxSc().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_DeleteRxSc(t_Handle h_FmMacsecSecY, t_Handle h_Sc);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_CreateRxSa

 @Description   Create a receive secure association for the secure channel;
                the SA cannot be used to receive frames until FM_MACSEC_SECY_RxSaEnableReceive is called.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     h_Sc                SC handle as returned by FM_MACSEC_SECY_CreateRxSc.
 @Param[in]     an                  association number represent the SA.
 @Param[in]     lowestPn            the lowest acceptable PN value for a received frame.
 @Param[in]     key                 the desired key for this SA.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_CreateRxSc().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_CreateRxSa(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an, uint32_t lowestPn, macsecSAKey_t key);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_DeleteRxSa

 @Description   Deleting an initialized secure association.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     h_Sc                SC handle as returned by FM_MACSEC_SECY_CreateRxSc.
 @Param[in]     an                  association number represent the SA.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_DeleteRxSa(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_RxSaEnableReceive

 @Description   Enabling the SA to receive frames.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     h_Sc                SC handle as returned by FM_MACSEC_SECY_CreateRxSc.
 @Param[in]     an                  association number represent the SA.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_CreateRxSa().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_RxSaEnableReceive(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_RxSaDisableReceive

 @Description   Disabling the SA from receive frames.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     h_Sc                SC handle as returned by FM_MACSEC_SECY_CreateRxSc.
 @Param[in]     an                  association number represent the SA.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_CreateRxSa().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_RxSaDisableReceive(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_RxSaUpdateNextPn

 @Description   Update the next packet number expected on RX;
                The value of nextPN shall be set to the greater of its existing value and the
                supplied of updtNextPN (802.1AE-2006 10.7.15).

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     h_Sc                SC handle as returned by FM_MACSEC_SECY_CreateRxSc.
 @Param[in]     an                  association number represent the SA.
 @Param[in]     updtNextPN          the next PN value for a received frame.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_CreateRxSa().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_RxSaUpdateNextPn(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an, uint32_t updtNextPN);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_RxSaUpdateLowestPn

 @Description   Update the lowest packet number expected on RX;
                The value of lowestPN shall be set to the greater of its existing value and the
                supplied of updtLowestPN (802.1AE-2006 10.7.15).

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     h_Sc                SC handle as returned by FM_MACSEC_SECY_CreateRxSc.
 @Param[in]     an                  association number represent the SA.
 @Param[in]     updtLowestPN        the lowest PN acceptable value for a received frame.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_CreateRxSa().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_RxSaUpdateLowestPn(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an, uint32_t updtLowestPN);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_RxSaModifyKey

 @Description   Modify the current key of the SA with a new one.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     h_Sc                SC handle as returned by FM_MACSEC_SECY_CreateRxSc.
 @Param[in]     an                  association number represent the SA.
 @Param[in]     key                 new key to replace the current key.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_CreateRxSa().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_RxSaModifyKey(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an, macsecSAKey_t key);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_CreateTxSa

 @Description   Create a transmit secure association for the secure channel;
                the SA cannot be used to transmit frames until FM_MACSEC_SECY_TxSaSetActivate is called;
                Only one SA can be active at a time.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     an                  association number represent the SA.
 @Param[in]     key                 the desired key for this SA.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_CreateTxSa(t_Handle h_FmMacsecSecY, macsecAN_t an, macsecSAKey_t key);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_DeleteTxSa

 @Description   Deleting an initialized secure association.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     an                  association number represent the SA.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_DeleteTxSa(t_Handle h_FmMacsecSecY, macsecAN_t an);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_TxSaModifyKey

 @Description   Modify the key of the inactive SA with a new one.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     nextActiveAn        association number represent the next SA to be activated.
 @Param[in]     key                 new key to replace the current key.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_TxSaModifyKey(t_Handle h_FmMacsecSecY, macsecAN_t nextActiveAn, macsecSAKey_t key);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_TxSaSetActive

 @Description   Set this SA to the active SA to be used on TX for SC;
                only one SA can be active at a time.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     an                  association number represent the SA.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_TxSaSetActive(t_Handle h_FmMacsecSecY, macsecAN_t an);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_TxSaGetActive

 @Description   Get the active SA that being used for TX.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[out]    p_An                the active an.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_TxSaGetActive(t_Handle h_FmMacsecSecY, macsecAN_t *p_An);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_GetStatistics

 @Description   get all statistics counters.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     p_Statistics        Structure with statistics.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_GetStatistics(t_Handle h_FmMacsecSecY, t_FmMacsecSecYStatistics *p_Statistics);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_RxScGetStatistics

 @Description   get all statistics counters.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     h_Sc                Rx Sc handle.
 @Param[in]     p_Statistics        Structure with statistics.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_RxScGetStatistics(t_Handle h_FmMacsecSecY, t_Handle h_Sc, t_FmMacsecSecYRxScStatistics *p_Statistics);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_RxSaGetStatistics

 @Description   get all statistics counters

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     h_Sc                Rx Sc handle.
 @Param[in]     an                  association number represent the SA.
 @Param[in]     p_Statistics        Structure with statistics.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_RxSaGetStatistics(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an, t_FmMacsecSecYRxSaStatistics *p_Statistics);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_TxScGetStatistics

 @Description   get all statistics counters.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     p_Statistics        Structure with statistics.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_TxScGetStatistics(t_Handle h_FmMacsecSecY, t_FmMacsecSecYTxScStatistics *p_Statistics);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_TxSaGetStatistics

 @Description   get all statistics counters.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     an                  association number represent the SA.
 @Param[in]     p_Statistics        Structure with statistics.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_TxSaGetStatistics(t_Handle h_FmMacsecSecY, macsecAN_t an, t_FmMacsecSecYTxSaStatistics *p_Statistics);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_SetException

 @Description   Calling this routine enables/disables the specified exception.

 @Param[in]     h_FmMacsecSecY  FM MACSEC SECY module descriptor.
 @Param[in]     exception       The exception to be selected.
 @Param[in]     enable          TRUE to enable interrupt, FALSE to mask it.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_SetException(t_Handle h_FmMacsecSecY, e_FmMacsecExceptions exception, bool enable);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_SetEvent

 @Description   Calling this routine enables/disables the specified event.

 @Param[in]     h_FmMacsecSecY  FM MACSEC SECY module descriptor.
 @Param[in]     event           The event to be selected.
 @Param[in]     enable          TRUE to enable interrupt, FALSE to mask it.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Config() and before FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_SetEvent(t_Handle h_FmMacsecSecY, e_FmMacsecSecYEvents event, bool enable);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_GetRxScPhysId

 @Description   return the physical id of the Secure Channel.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[in]     h_Sc                SC handle as returned by FM_MACSEC_SECY_CreateRxSc.
 @Param[out]    p_ScPhysId          the SC physical id.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_CreateRxSc().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_GetRxScPhysId(t_Handle h_FmMacsecSecY, t_Handle h_Sc, uint32_t *p_ScPhysId);

/**************************************************************************//**
 @Function      FM_MACSEC_SECY_GetTxScPhysId

 @Description   return the physical id of the Secure Channel.

 @Param[in]     h_FmMacsecSecY      FM MACSEC SECY module descriptor.
 @Param[out]    p_ScPhysId          the SC physical id.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_MACSEC_SECY_Init().
*//***************************************************************************/
t_Error FM_MACSEC_SECY_GetTxScPhysId(t_Handle h_FmMacsecSecY, uint32_t *p_ScPhysId);

/** @} */ /* end of FM_MACSEC_SECY_runtime_control_grp group */
/** @} */ /* end of FM_MACSEC_SECY_grp group */
/** @} */ /* end of FM_MACSEC_grp group */
/** @} */ /* end of FM_grp group */


#endif /* __FM_MACSEC_EXT_H */
