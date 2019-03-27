/* Copyright (c) 2008-2012 Freescale Semiconductor, Inc
 * All rights reserved.
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
 @File          fm_pcd_ext.h

 @Description   FM PCD API definitions
*//***************************************************************************/
#ifndef __FM_PCD_EXT
#define __FM_PCD_EXT

#include "std_ext.h"
#include "net_ext.h"
#include "list_ext.h"
#include "fm_ext.h"
#include "fsl_fman_kg.h"


/**************************************************************************//**
 @Group         FM_grp Frame Manager API

 @Description   Frame Manager Application Programming Interface

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         FM_PCD_grp FM PCD

 @Description   Frame Manager PCD (Parse-Classify-Distribute) API.

                The FM PCD module is responsible for the initialization of all
                global classifying FM modules. This includes the parser general and
                common registers, the key generator global and common registers,
                and the policer global and common registers.
                In addition, the FM PCD SW module will initialize all required
                key generator schemes, coarse classification flows, and policer
                profiles. When FM module is configured to work with one of these
                entities, it will register to it using the FM PORT API. The PCD
                module will manage the PCD resources - i.e. resource management of
                KeyGen schemes, etc.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Collection    General PCD defines
*//***************************************************************************/
#define FM_PCD_MAX_NUM_OF_PRIVATE_HDRS              2                   /**< Number of units/headers saved for user */

#define FM_PCD_PRS_NUM_OF_HDRS                      16                  /**< Number of headers supported by HW parser */
#define FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS         (32 - FM_PCD_MAX_NUM_OF_PRIVATE_HDRS)
                                                                        /**< Number of distinction units is limited by
                                                                             register size (32 bits) minus reserved bits
                                                                             for private headers. */
#define FM_PCD_MAX_NUM_OF_INTERCHANGEABLE_HDRS      4                   /**< Maximum number of interchangeable headers
                                                                             in a distinction unit */
#define FM_PCD_KG_NUM_OF_GENERIC_REGS               FM_KG_NUM_OF_GENERIC_REGS /**< Total number of generic KeyGen registers */
#define FM_PCD_KG_MAX_NUM_OF_EXTRACTS_PER_KEY       35                  /**< Max number allowed on any configuration;
                                                                             For HW implementation reasons, in most
                                                                             cases less than this will be allowed; The
                                                                             driver will return an initialization error
                                                                             if resource is unavailable. */
#define FM_PCD_KG_NUM_OF_EXTRACT_MASKS              4                   /**< Total number of masks allowed on KeyGen extractions. */
#define FM_PCD_KG_NUM_OF_DEFAULT_GROUPS             16                  /**< Number of default value logical groups */

#define FM_PCD_PRS_NUM_OF_LABELS                    32                  /**< Maximum number of SW parser labels */
#define FM_SW_PRS_MAX_IMAGE_SIZE                    (FM_PCD_SW_PRS_SIZE /*- FM_PCD_PRS_SW_OFFSET -FM_PCD_PRS_SW_TAIL_SIZE*/-FM_PCD_PRS_SW_PATCHES_SIZE)
                                                                        /**< Maximum size of SW parser code */

#define FM_PCD_MAX_MANIP_INSRT_TEMPLATE_SIZE        128                 /**< Maximum size of insertion template for
                                                                             insert manipulation */

#if (DPAA_VERSION >= 11)
#define FM_PCD_FRM_REPLIC_MAX_NUM_OF_ENTRIES        64                  /**< Maximum possible entries for frame replicator group */
#endif /* (DPAA_VERSION >= 11) */
/* @} */


/**************************************************************************//**
 @Group         FM_PCD_init_grp FM PCD Initialization Unit

 @Description   Frame Manager PCD Initialization Unit API

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   PCD counters
*//***************************************************************************/
typedef enum e_FmPcdCounters {
    e_FM_PCD_KG_COUNTERS_TOTAL,                                 /**< KeyGen counter */
    e_FM_PCD_PLCR_COUNTERS_RED,                                 /**< Policer counter - counts the total number of RED packets that exit the Policer. */
    e_FM_PCD_PLCR_COUNTERS_YELLOW,                              /**< Policer counter - counts the total number of YELLOW packets that exit the Policer. */
    e_FM_PCD_PLCR_COUNTERS_RECOLORED_TO_RED,                    /**< Policer counter - counts the number of packets that changed color to RED by the Policer;
                                                                     This is a subset of e_FM_PCD_PLCR_COUNTERS_RED packet count, indicating active color changes. */
    e_FM_PCD_PLCR_COUNTERS_RECOLORED_TO_YELLOW,                 /**< Policer counter - counts the number of packets that changed color to YELLOW by the Policer;
                                                                     This is a subset of e_FM_PCD_PLCR_COUNTERS_YELLOW packet count, indicating active color changes. */
    e_FM_PCD_PLCR_COUNTERS_TOTAL,                               /**< Policer counter - counts the total number of packets passed in the Policer. */
    e_FM_PCD_PLCR_COUNTERS_LENGTH_MISMATCH,                     /**< Policer counter - counts the number of packets with length mismatch. */
    e_FM_PCD_PRS_COUNTERS_PARSE_DISPATCH,                       /**< Parser counter - counts the number of times the parser block is dispatched. */
    e_FM_PCD_PRS_COUNTERS_L2_PARSE_RESULT_RETURNED,             /**< Parser counter - counts the number of times L2 parse result is returned (including errors). */
    e_FM_PCD_PRS_COUNTERS_L3_PARSE_RESULT_RETURNED,             /**< Parser counter - counts the number of times L3 parse result is returned (including errors). */
    e_FM_PCD_PRS_COUNTERS_L4_PARSE_RESULT_RETURNED,             /**< Parser counter - counts the number of times L4 parse result is returned (including errors). */
    e_FM_PCD_PRS_COUNTERS_SHIM_PARSE_RESULT_RETURNED,           /**< Parser counter - counts the number of times SHIM parse result is returned (including errors). */
    e_FM_PCD_PRS_COUNTERS_L2_PARSE_RESULT_RETURNED_WITH_ERR,    /**< Parser counter - counts the number of times L2 parse result is returned with errors. */
    e_FM_PCD_PRS_COUNTERS_L3_PARSE_RESULT_RETURNED_WITH_ERR,    /**< Parser counter - counts the number of times L3 parse result is returned with errors. */
    e_FM_PCD_PRS_COUNTERS_L4_PARSE_RESULT_RETURNED_WITH_ERR,    /**< Parser counter - counts the number of times L4 parse result is returned with errors. */
    e_FM_PCD_PRS_COUNTERS_SHIM_PARSE_RESULT_RETURNED_WITH_ERR,  /**< Parser counter - counts the number of times SHIM parse result is returned with errors. */
    e_FM_PCD_PRS_COUNTERS_SOFT_PRS_CYCLES,                      /**< Parser counter - counts the number of cycles spent executing soft parser instruction (including stall cycles). */
    e_FM_PCD_PRS_COUNTERS_SOFT_PRS_STALL_CYCLES,                /**< Parser counter - counts the number of cycles stalled waiting for parser internal memory reads while executing soft parser instruction. */
    e_FM_PCD_PRS_COUNTERS_HARD_PRS_CYCLE_INCL_STALL_CYCLES,     /**< Parser counter - counts the number of cycles spent executing hard parser (including stall cycles). */
    e_FM_PCD_PRS_COUNTERS_MURAM_READ_CYCLES,                    /**< MURAM counter - counts the number of cycles while performing FMan Memory read. */
    e_FM_PCD_PRS_COUNTERS_MURAM_READ_STALL_CYCLES,              /**< MURAM counter - counts the number of cycles stalled while performing FMan Memory read. */
    e_FM_PCD_PRS_COUNTERS_MURAM_WRITE_CYCLES,                   /**< MURAM counter - counts the number of cycles while performing FMan Memory write. */
    e_FM_PCD_PRS_COUNTERS_MURAM_WRITE_STALL_CYCLES,             /**< MURAM counter - counts the number of cycles stalled while performing FMan Memory write. */
    e_FM_PCD_PRS_COUNTERS_FPM_COMMAND_STALL_CYCLES              /**< FPM counter - counts the number of cycles stalled while performing a FPM Command. */
} e_FmPcdCounters;

/**************************************************************************//**
 @Description   PCD interrupts
*//***************************************************************************/
typedef enum e_FmPcdExceptions {
    e_FM_PCD_KG_EXCEPTION_DOUBLE_ECC,                   /**< KeyGen double-bit ECC error is detected on internal memory read access. */
    e_FM_PCD_KG_EXCEPTION_KEYSIZE_OVERFLOW,             /**< KeyGen scheme configuration error indicating a key size larger than 56 bytes. */
    e_FM_PCD_PLCR_EXCEPTION_DOUBLE_ECC,                 /**< Policer double-bit ECC error has been detected on PRAM read access. */
    e_FM_PCD_PLCR_EXCEPTION_INIT_ENTRY_ERROR,           /**< Policer access to a non-initialized profile has been detected. */
    e_FM_PCD_PLCR_EXCEPTION_PRAM_SELF_INIT_COMPLETE,    /**< Policer RAM self-initialization complete */
    e_FM_PCD_PLCR_EXCEPTION_ATOMIC_ACTION_COMPLETE,     /**< Policer atomic action complete */
    e_FM_PCD_PRS_EXCEPTION_DOUBLE_ECC,                  /**< Parser double-bit ECC error */
    e_FM_PCD_PRS_EXCEPTION_SINGLE_ECC                   /**< Parser single-bit ECC error */
} e_FmPcdExceptions;


/**************************************************************************//**
 @Description   Exceptions user callback routine, will be called upon an
                exception passing the exception identification.

 @Param[in]     h_App      - User's application descriptor.
 @Param[in]     exception  - The exception.
  *//***************************************************************************/
typedef void (t_FmPcdExceptionCallback) (t_Handle h_App, e_FmPcdExceptions exception);

/**************************************************************************//**
 @Description   Exceptions user callback routine, will be called upon an exception
                passing the exception identification.

 @Param[in]     h_App           - User's application descriptor.
 @Param[in]     exception       - The exception.
 @Param[in]     index           - id of the relevant source (may be scheme or profile id).
 *//***************************************************************************/
typedef void (t_FmPcdIdExceptionCallback) ( t_Handle           h_App,
                                            e_FmPcdExceptions  exception,
                                            uint16_t           index);

/**************************************************************************//**
 @Description   A callback for enqueuing frame onto a QM queue.

 @Param[in]     h_QmArg         - Application's handle passed to QM module on enqueue.
 @Param[in]     p_Fd            - Frame descriptor for the frame.

 @Return        E_OK on success; Error code otherwise.
 *//***************************************************************************/
typedef t_Error (t_FmPcdQmEnqueueCallback) (t_Handle h_QmArg, void *p_Fd);

/**************************************************************************//**
 @Description   Host-Command parameters structure.

                When using Host command for PCD functionalities, a dedicated port
                must be used. If this routine is called for a PCD in a single partition
                environment, or it is the Master partition in a Multi-partition
                environment, The port will be initialized by the PCD driver
                initialization routine.
 *//***************************************************************************/
typedef struct t_FmPcdHcParams {
    uintptr_t                   portBaseAddr;       /**< Virtual Address of Host-Command Port memory mapped registers.*/
    uint8_t                     portId;             /**< Port Id (0-6 relative to Host-Command/Offline-Parsing ports);
                                                         NOTE: When configuring Host Command port for
                                                         FMANv3 devices (DPAA_VERSION 11 and higher),
                                                         portId=0 MUST be used. */
    uint16_t                    liodnBase;          /**< LIODN base for this port, to be used together with LIODN offset
                                                         (irrelevant for P4080 revision 1.0) */
    uint32_t                    errFqid;            /**< Host-Command Port error queue Id. */
    uint32_t                    confFqid;           /**< Host-Command Port confirmation queue Id. */
    uint32_t                    qmChannel;          /**< QM channel dedicated to this Host-Command port;
                                                         will be used by the FM for dequeue. */
    t_FmPcdQmEnqueueCallback    *f_QmEnqueue;       /**< Callback routine for enqueuing a frame to the QM */
    t_Handle                    h_QmArg;            /**< Application's handle passed to QM module on enqueue */
} t_FmPcdHcParams;

/**************************************************************************//**
 @Description   The main structure for PCD initialization
 *//***************************************************************************/
typedef struct t_FmPcdParams {
    bool                        prsSupport;             /**< TRUE if Parser will be used for any of the FM ports. */
    bool                        ccSupport;              /**< TRUE if Coarse Classification will be used for any
                                                             of the FM ports. */
    bool                        kgSupport;              /**< TRUE if KeyGen will be used for any of the FM ports. */
    bool                        plcrSupport;            /**< TRUE if Policer will be used for any of the FM ports. */
    t_Handle                    h_Fm;                   /**< A handle to the FM module. */
    uint8_t                     numOfSchemes;           /**< Number of schemes dedicated to this partition.
                                                             this parameter is relevant if 'kgSupport'=TRUE. */
    bool                        useHostCommand;         /**< Optional for single partition, Mandatory for Multi partition */
    t_FmPcdHcParams             hc;                     /**< Host Command parameters, relevant only if 'useHostCommand'=TRUE;
                                                             Relevant when FM not runs in "guest-mode". */

    t_FmPcdExceptionCallback    *f_Exception;           /**< Callback routine for general PCD exceptions;
                                                             Relevant when FM not runs in "guest-mode". */
    t_FmPcdIdExceptionCallback  *f_ExceptionId;         /**< Callback routine for specific KeyGen scheme or
                                                             Policer profile exceptions;
                                                             Relevant when FM not runs in "guest-mode". */
    t_Handle                    h_App;                  /**< A handle to an application layer object; This handle will
                                                             be passed by the driver upon calling the above callbacks;
                                                             Relevant when FM not runs in "guest-mode". */
    uint8_t                     partPlcrProfilesBase;   /**< The first policer-profile-id dedicated to this partition.
                                                             this parameter is relevant if 'plcrSupport'=TRUE.
                                                             NOTE: this parameter relevant only when working with multiple partitions. */
    uint16_t                    partNumOfPlcrProfiles;  /**< Number of policer-profiles dedicated to this partition.
                                                             this parameter is relevant if 'plcrSupport'=TRUE.
                                                             NOTE: this parameter relevant only when working with multiple partitions. */
} t_FmPcdParams;


/**************************************************************************//**
 @Function      FM_PCD_Config

 @Description   Basic configuration of the PCD module.
                Creates descriptor for the FM PCD module.

 @Param[in]     p_FmPcdParams    A structure of parameters for the initialization of PCD.

 @Return        A handle to the initialized module.
*//***************************************************************************/
t_Handle FM_PCD_Config(t_FmPcdParams *p_FmPcdParams);

/**************************************************************************//**
 @Function      FM_PCD_Init

 @Description   Initialization of the PCD module.

 @Param[in]     h_FmPcd - FM PCD module descriptor.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_PCD_Init(t_Handle h_FmPcd);

/**************************************************************************//**
 @Function      FM_PCD_Free

 @Description   Frees all resources that were assigned to FM module.

                Calling this routine invalidates the descriptor.

 @Param[in]     h_FmPcd - FM PCD module descriptor.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_PCD_Free(t_Handle h_FmPcd);

/**************************************************************************//**
 @Group         FM_PCD_advanced_cfg_grp    FM PCD Advanced Configuration Unit

 @Description   Frame Manager PCD Advanced Configuration API.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      FM_PCD_ConfigException

 @Description   Calling this routine changes the internal driver data base
                from its default selection of exceptions enabling.
                [DEFAULT_numOfSharedPlcrProfiles].

 @Param[in]     h_FmPcd         FM PCD module descriptor.
 @Param[in]     exception       The exception to be selected.
 @Param[in]     enable          TRUE to enable interrupt, FALSE to mask it.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_PCD_ConfigException(t_Handle h_FmPcd, e_FmPcdExceptions exception, bool enable);

/**************************************************************************//**
 @Function      FM_PCD_ConfigHcFramesDataMemory

 @Description   Configures memory-partition-id for FMan-Controller Host-Command
                frames. Calling this routine changes the internal driver data
                base from its default configuration [0].

 @Param[in]     h_FmPcd         FM PCD module descriptor.
 @Param[in]     memId           Memory partition ID.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      This routine may be called only if 'useHostCommand' was TRUE
                when FM_PCD_Config() routine was called.
*//***************************************************************************/
t_Error FM_PCD_ConfigHcFramesDataMemory(t_Handle h_FmPcd, uint8_t memId);

/**************************************************************************//**
 @Function      FM_PCD_ConfigPlcrNumOfSharedProfiles

 @Description   Calling this routine changes the internal driver data base
                from its default selection of exceptions enablement.
                [DEFAULT_numOfSharedPlcrProfiles].

 @Param[in]     h_FmPcd                     FM PCD module descriptor.
 @Param[in]     numOfSharedPlcrProfiles     Number of profiles to
                                            be shared between ports on this partition

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_PCD_ConfigPlcrNumOfSharedProfiles(t_Handle h_FmPcd, uint16_t numOfSharedPlcrProfiles);

/**************************************************************************//**
 @Function      FM_PCD_ConfigPlcrAutoRefreshMode

 @Description   Calling this routine changes the internal driver data base
                from its default selection of exceptions enablement.
                By default auto-refresh is [DEFAULT_plcrAutoRefresh].

 @Param[in]     h_FmPcd         FM PCD module descriptor.
 @Param[in]     enable          TRUE to enable, FALSE to disable

 @Return        E_OK on success; Error code otherwise.

 @Cautions      This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_PCD_ConfigPlcrAutoRefreshMode(t_Handle h_FmPcd, bool enable);

/**************************************************************************//**
 @Function      FM_PCD_ConfigPrsMaxCycleLimit

 @Description   Calling this routine changes the internal data structure for
                the maximum parsing time from its default value
                [DEFAULT_MAX_PRS_CYC_LIM].

 @Param[in]     h_FmPcd         FM PCD module descriptor.
 @Param[in]     value           0 to disable the mechanism, or new
                                maximum parsing time.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_PCD_ConfigPrsMaxCycleLimit(t_Handle h_FmPcd,uint16_t value);

/** @} */ /* end of FM_PCD_advanced_cfg_grp group */
/** @} */ /* end of FM_PCD_init_grp group */


/**************************************************************************//**
 @Group         FM_PCD_Runtime_grp FM PCD Runtime Unit

 @Description   Frame Manager PCD Runtime Unit API

                The runtime control allows creation of PCD infrastructure modules
                such as Network Environment Characteristics, Classification Plan
                Groups and Coarse Classification Trees.
                It also allows on-the-fly initialization, modification and removal
                of PCD modules such as KeyGen schemes, coarse classification nodes
                and Policer profiles.

                In order to explain the programming model of the PCD driver interface
                a few terms should be explained, and will be used below.
                  - Distinction Header - One of the 16 protocols supported by the FM parser,
                    or one of the SHIM headers (1 or 2). May be a header with a special
                    option (see below).
                  - Interchangeable Headers Group - This is a group of Headers recognized
                    by either one of them. For example, if in a specific context the user
                    chooses to treat IPv4 and IPV6 in the same way, they may create an
                    interchangeable Headers Unit consisting of these 2 headers.
                  - A Distinction Unit - a Distinction Header or an Interchangeable Headers
                    Group.
                  - Header with special option - applies to Ethernet, MPLS, VLAN, IPv4 and
                    IPv6, includes multicast, broadcast and other protocol specific options.
                    In terms of hardware it relates to the options available in the classification
                    plan.
                  - Network Environment Characteristics - a set of Distinction Units that define
                    the total recognizable header selection for a certain environment. This is
                    NOT the list of all headers that will ever appear in a flow, but rather
                    everything that needs distinction in a flow, where distinction is made by KeyGen
                    schemes and coarse classification action descriptors.

                The PCD runtime modules initialization is done in stages. The first stage after
                initializing the PCD module itself is to establish a Network Flows Environment
                Definition. The application may choose to establish one or more such environments.
                Later, when needed, the application will have to state, for some of its modules,
                to which single environment it belongs.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   A structure for SW parser labels
 *//***************************************************************************/
typedef struct t_FmPcdPrsLabelParams {
    uint32_t                instructionOffset;              /**< SW parser label instruction offset (2 bytes
                                                                 resolution), relative to Parser RAM. */
    e_NetHeaderType         hdr;                            /**< The existence of this header will invoke
                                                                 the SW parser code; Use  HEADER_TYPE_NONE
                                                                 to indicate that sw parser is to run
                                                                 independent of the existence of any protocol
                                                                 (run before HW parser). */
    uint8_t                 indexPerHdr;                    /**< Normally 0, if more than one SW parser
                                                                 attachments for the same header, use this
                                                                 index to distinguish between them. */
} t_FmPcdPrsLabelParams;

/**************************************************************************//**
 @Description   A structure for SW parser
 *//***************************************************************************/
typedef struct t_FmPcdPrsSwParams {
    bool                    override;                   /**< FALSE to invoke a check that nothing else
                                                             was loaded to this address, including
                                                             internal patches.
                                                             TRUE to override any existing code.*/
    uint32_t                size;                       /**< SW parser code size */
    uint16_t                base;                       /**< SW parser base (in instruction counts!
                                                             must be larger than 0x20)*/
    uint8_t                 *p_Code;                    /**< SW parser code */
    uint32_t                swPrsDataParams[FM_PCD_PRS_NUM_OF_HDRS];
                                                        /**< SW parser data (parameters) */
    uint8_t                 numOfLabels;                /**< Number of labels for SW parser. */
    t_FmPcdPrsLabelParams   labelsTable[FM_PCD_PRS_NUM_OF_LABELS];
                                                        /**< SW parser labels table, containing
                                                             numOfLabels entries */
} t_FmPcdPrsSwParams;


/**************************************************************************//**
 @Function      FM_PCD_Enable

 @Description   This routine should be called after PCD is initialized for enabling all
                PCD engines according to their existing configuration.

 @Param[in]     h_FmPcd         FM PCD module descriptor.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init() and when PCD is disabled.
*//***************************************************************************/
t_Error FM_PCD_Enable(t_Handle h_FmPcd);

/**************************************************************************//**
 @Function      FM_PCD_Disable

 @Description   This routine may be called when PCD is enabled in order to
                disable all PCD engines. It may be called
                only when none of the ports in the system are using the PCD.

 @Param[in]     h_FmPcd         FM PCD module descriptor.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init() and when PCD is enabled.
*//***************************************************************************/
t_Error FM_PCD_Disable(t_Handle h_FmPcd);

/**************************************************************************//**
 @Function      FM_PCD_GetCounter

 @Description   Reads one of the FM PCD counters.

 @Param[in]     h_FmPcd     FM PCD module descriptor.
 @Param[in]     counter     The requested counter.

 @Return        Counter's current value.

 @Cautions      Allowed only following FM_PCD_Init().
                Note that it is user's responsibility to call this routine only
                for enabled counters, and there will be no indication if a
                disabled counter is accessed.
*//***************************************************************************/
uint32_t FM_PCD_GetCounter(t_Handle h_FmPcd, e_FmPcdCounters counter);

/**************************************************************************//**
@Function       FM_PCD_PrsLoadSw

@Description    This routine may be called in order to load software parsing code.


@Param[in]      h_FmPcd        FM PCD module descriptor.
@Param[in]      p_SwPrs        A pointer to a structure of software
                               parser parameters, including the software
                               parser image.

@Return         E_OK on success; Error code otherwise.

@Cautions       Allowed only following FM_PCD_Init() and when PCD is disabled.
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_PCD_PrsLoadSw(t_Handle h_FmPcd, t_FmPcdPrsSwParams *p_SwPrs);

/**************************************************************************//**
@Function      FM_PCD_SetAdvancedOffloadSupport

@Description   This routine must be called in order to support the following features:
               IP-fragmentation, IP-reassembly, IPsec, Header-manipulation, frame-replicator.

@Param[in]     h_FmPcd         FM PCD module descriptor.

@Return        E_OK on success; Error code otherwise.

@Cautions      Allowed only following FM_PCD_Init() and when PCD is disabled.
               This routine should NOT be called from guest-partition
               (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_PCD_SetAdvancedOffloadSupport(t_Handle h_FmPcd);

/**************************************************************************//**
 @Function      FM_PCD_KgSetDfltValue

 @Description   Calling this routine sets a global default value to be used
                by the KeyGen when parser does not recognize a required
                field/header.
                By default default values are 0.

 @Param[in]     h_FmPcd         FM PCD module descriptor.
 @Param[in]     valueId         0,1 - one of 2 global default values.
 @Param[in]     value           The requested default value.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init() and when PCD is disabled.
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_PCD_KgSetDfltValue(t_Handle h_FmPcd, uint8_t valueId, uint32_t value);

/**************************************************************************//**
 @Function      FM_PCD_KgSetAdditionalDataAfterParsing

 @Description   Calling this routine allows the KeyGen to access data past
                the parser finishing point.

 @Param[in]     h_FmPcd         FM PCD module descriptor.
 @Param[in]     payloadOffset   the number of bytes beyond the parser location.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init() and when PCD is disabled.
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_PCD_KgSetAdditionalDataAfterParsing(t_Handle h_FmPcd, uint8_t payloadOffset);

/**************************************************************************//**
 @Function      FM_PCD_SetException

 @Description   Calling this routine enables/disables PCD interrupts.

 @Param[in]     h_FmPcd         FM PCD module descriptor.
 @Param[in]     exception       The exception to be selected.
 @Param[in]     enable          TRUE to enable interrupt, FALSE to mask it.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_PCD_SetException(t_Handle h_FmPcd, e_FmPcdExceptions exception, bool enable);

/**************************************************************************//**
 @Function      FM_PCD_ModifyCounter

 @Description   Sets a value to an enabled counter. Use "0" to reset the counter.

 @Param[in]     h_FmPcd     FM PCD module descriptor.
 @Param[in]     counter     The requested counter.
 @Param[in]     value       The requested value to be written into the counter.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_PCD_ModifyCounter(t_Handle h_FmPcd, e_FmPcdCounters counter, uint32_t value);

/**************************************************************************//**
 @Function      FM_PCD_SetPlcrStatistics

 @Description   This routine may be used to enable/disable policer statistics
                counter. By default the statistics is enabled.

 @Param[in]     h_FmPcd         FM PCD module descriptor
 @Param[in]     enable          TRUE to enable, FALSE to disable.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_PCD_SetPlcrStatistics(t_Handle h_FmPcd, bool enable);

/**************************************************************************//**
 @Function      FM_PCD_SetPrsStatistics

 @Description   Defines whether to gather parser statistics including all ports.

 @Param[in]     h_FmPcd     FM PCD module descriptor.
 @Param[in]     enable      TRUE to enable, FALSE to disable.

 @Return        None

 @Cautions      Allowed only following FM_PCD_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
void FM_PCD_SetPrsStatistics(t_Handle h_FmPcd, bool enable);

/**************************************************************************//**
 @Function      FM_PCD_HcTxConf

 @Description   This routine should be called to confirm frames that were
                 received on the HC confirmation queue.

 @Param[in]     h_FmPcd         A handle to an FM PCD Module.
 @Param[in]     p_Fd            Frame descriptor of the received frame.

 @Cautions      Allowed only following FM_PCD_Init(). Allowed only if 'useHostCommand'
                option was selected in the initialization.
*//***************************************************************************/
void FM_PCD_HcTxConf(t_Handle h_FmPcd, t_DpaaFD *p_Fd);

/**************************************************************************//*
 @Function      FM_PCD_ForceIntr

 @Description   Causes an interrupt event on the requested source.

 @Param[in]     h_FmPcd     FM PCD module descriptor.
 @Param[in]     exception       An exception to be forced.

 @Return        E_OK on success; Error code if the exception is not enabled,
                or is not able to create interrupt.

 @Cautions      Allowed only following FM_PCD_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error FM_PCD_ForceIntr (t_Handle h_FmPcd, e_FmPcdExceptions exception);

#if (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0))
/**************************************************************************//**
 @Function      FM_PCD_DumpRegs

 @Description   Dumps all PCD registers

 @Param[in]     h_FmPcd         A handle to an FM PCD Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
                NOTE: this routine may be called only for FM in master mode
                (i.e. 'guestId'=NCSW_MASTER_ID) or in a case that the registers
                are mapped.
*//***************************************************************************/
t_Error FM_PCD_DumpRegs(t_Handle h_FmPcd);

/**************************************************************************//**
 @Function      FM_PCD_KgDumpRegs

 @Description   Dumps all PCD KG registers

 @Param[in]     h_FmPcd         A handle to an FM PCD Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
                NOTE: this routine may be called only for FM in master mode
                (i.e. 'guestId'=NCSW_MASTER_ID) or in a case that the registers
                are mapped.
*//***************************************************************************/
t_Error FM_PCD_KgDumpRegs(t_Handle h_FmPcd);

/**************************************************************************//**
 @Function      FM_PCD_PlcrDumpRegs

 @Description   Dumps all PCD Policer registers

 @Param[in]     h_FmPcd         A handle to an FM PCD Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
                NOTE: this routine may be called only for FM in master mode
                (i.e. 'guestId'=NCSW_MASTER_ID) or in a case that the registers
                are mapped.
*//***************************************************************************/
t_Error FM_PCD_PlcrDumpRegs(t_Handle h_FmPcd);

/**************************************************************************//**
 @Function      FM_PCD_PlcrProfileDumpRegs

 @Description   Dumps all PCD Policer profile registers

 @Param[in]     h_Profile       A handle to a Policer profile.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
                NOTE: this routine may be called only for FM in master mode
                (i.e. 'guestId'=NCSW_MASTER_ID) or in a case that the registers
                are mapped.
*//***************************************************************************/
t_Error FM_PCD_PlcrProfileDumpRegs(t_Handle h_Profile);

/**************************************************************************//**
 @Function      FM_PCD_PrsDumpRegs

 @Description   Dumps all PCD Parser registers

 @Param[in]     h_FmPcd         A handle to an FM PCD Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
                NOTE: this routine may be called only for FM in master mode
                (i.e. 'guestId'=NCSW_MASTER_ID) or in a case that the registers
                are mapped.
*//***************************************************************************/
t_Error FM_PCD_PrsDumpRegs(t_Handle h_FmPcd);

/**************************************************************************//**
 @Function      FM_PCD_HcDumpRegs

 @Description   Dumps HC Port registers

 @Param[in]     h_FmPcd         A handle to an FM PCD Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
                NOTE: this routine may be called only for FM in master mode
                (i.e. 'guestId'=NCSW_MASTER_ID).
*//***************************************************************************/
t_Error     FM_PCD_HcDumpRegs(t_Handle h_FmPcd);
#endif /* (defined(DEBUG_ERRORS) && ... */



/**************************************************************************//**
 KeyGen         FM_PCD_Runtime_build_grp FM PCD Runtime Building Unit

 @Description   Frame Manager PCD Runtime Building API

                This group contains routines for setting, deleting and modifying
                PCD resources, for defining the total PCD tree.
 @{
*//***************************************************************************/

/**************************************************************************//**
 @Collection    Definitions of coarse classification
                parameters as required by KeyGen (when coarse classification
                is the next engine after this scheme).
*//***************************************************************************/
#define FM_PCD_MAX_NUM_OF_CC_TREES              8
#define FM_PCD_MAX_NUM_OF_CC_GROUPS             16
#define FM_PCD_MAX_NUM_OF_CC_UNITS              4
#define FM_PCD_MAX_NUM_OF_KEYS                  256
#define FM_PCD_MAX_NUM_OF_FLOWS                 (4*KILOBYTE)
#define FM_PCD_MAX_SIZE_OF_KEY                  56
#define FM_PCD_MAX_NUM_OF_CC_ENTRIES_IN_GRP     16
#define FM_PCD_LAST_KEY_INDEX                   0xffff

#define FM_PCD_MAX_NUM_OF_CC_NODES              255 /* Obsolete, not used - will be removed in the future */
/* @} */

/**************************************************************************//**
 @Collection    A set of definitions to allow protocol
                special option description.
*//***************************************************************************/
typedef uint32_t        protocolOpt_t;          /**< A general type to define a protocol option. */

typedef protocolOpt_t   ethProtocolOpt_t;       /**< Ethernet protocol options. */
#define ETH_BROADCAST               0x80000000  /**< Ethernet Broadcast. */
#define ETH_MULTICAST               0x40000000  /**< Ethernet Multicast. */

typedef protocolOpt_t   vlanProtocolOpt_t;      /**< VLAN protocol options. */
#define VLAN_STACKED                0x20000000  /**< Stacked VLAN. */

typedef protocolOpt_t   mplsProtocolOpt_t;      /**< MPLS protocol options. */
#define MPLS_STACKED                0x10000000  /**< Stacked MPLS. */

typedef protocolOpt_t   ipv4ProtocolOpt_t;      /**< IPv4 protocol options. */
#define IPV4_BROADCAST_1            0x08000000  /**< IPv4 Broadcast. */
#define IPV4_MULTICAST_1            0x04000000  /**< IPv4 Multicast. */
#define IPV4_UNICAST_2              0x02000000  /**< Tunneled IPv4 - Unicast. */
#define IPV4_MULTICAST_BROADCAST_2  0x01000000  /**< Tunneled IPv4 - Broadcast/Multicast. */

#define IPV4_FRAG_1                 0x00000008  /**< IPV4 reassembly option.
                                                     IPV4 Reassembly manipulation requires network
                                                     environment with IPV4 header and IPV4_FRAG_1 option  */

typedef protocolOpt_t   ipv6ProtocolOpt_t;      /**< IPv6 protocol options. */
#define IPV6_MULTICAST_1            0x00800000  /**< IPv6 Multicast. */
#define IPV6_UNICAST_2              0x00400000  /**< Tunneled IPv6 - Unicast. */
#define IPV6_MULTICAST_2            0x00200000  /**< Tunneled IPv6 - Multicast. */

#define IPV6_FRAG_1                 0x00000004  /**< IPV6 reassembly option.
                                                     IPV6 Reassembly manipulation requires network
                                                     environment with IPV6 header and IPV6_FRAG_1 option;
                                                     in case where fragment found, the fragment-extension offset
                                                     may be found at 'shim2' (in parser-result). */
#if (DPAA_VERSION >= 11)
typedef protocolOpt_t   capwapProtocolOpt_t;      /**< CAPWAP protocol options. */
#define CAPWAP_FRAG_1               0x00000008  /**< CAPWAP reassembly option.
                                                     CAPWAP Reassembly manipulation requires network
                                                     environment with CAPWAP header and CAPWAP_FRAG_1 option;
                                                     in case where fragment found, the fragment-extension offset
                                                     may be found at 'shim2' (in parser-result). */
#endif /* (DPAA_VERSION >= 11) */


/* @} */

#define FM_PCD_MANIP_MAX_HDR_SIZE               256
#define FM_PCD_MANIP_DSCP_TO_VLAN_TRANS         64

/**************************************************************************//**
 @Collection    A set of definitions to support Header Manipulation selection.
*//***************************************************************************/
typedef uint32_t                hdrManipFlags_t;            /**< A general type to define a HMan update command flags. */

typedef hdrManipFlags_t         ipv4HdrManipUpdateFlags_t;  /**< IPv4 protocol HMan update command flags. */

#define HDR_MANIP_IPV4_TOS      0x80000000                  /**< update TOS with the given value ('tos' field
                                                                 of t_FmPcdManipHdrFieldUpdateIpv4) */
#define HDR_MANIP_IPV4_ID       0x40000000                  /**< update IP ID with the given value ('id' field
                                                                 of t_FmPcdManipHdrFieldUpdateIpv4) */
#define HDR_MANIP_IPV4_TTL      0x20000000                  /**< Decrement TTL by 1 */
#define HDR_MANIP_IPV4_SRC      0x10000000                  /**< update IP source address with the given value
                                                                 ('src' field of t_FmPcdManipHdrFieldUpdateIpv4) */
#define HDR_MANIP_IPV4_DST      0x08000000                  /**< update IP destination address with the given value
                                                                 ('dst' field of t_FmPcdManipHdrFieldUpdateIpv4) */

typedef hdrManipFlags_t         ipv6HdrManipUpdateFlags_t;  /**< IPv6 protocol HMan update command flags. */

#define HDR_MANIP_IPV6_TC       0x80000000                  /**< update Traffic Class address with the given value
                                                                 ('trafficClass' field of t_FmPcdManipHdrFieldUpdateIpv6) */
#define HDR_MANIP_IPV6_HL       0x40000000                  /**< Decrement Hop Limit by 1 */
#define HDR_MANIP_IPV6_SRC      0x20000000                  /**< update IP source address with the given value
                                                                 ('src' field of t_FmPcdManipHdrFieldUpdateIpv6) */
#define HDR_MANIP_IPV6_DST      0x10000000                  /**< update IP destination address with the given value
                                                                 ('dst' field of t_FmPcdManipHdrFieldUpdateIpv6) */

typedef hdrManipFlags_t         tcpUdpHdrManipUpdateFlags_t;/**< TCP/UDP protocol HMan update command flags. */

#define HDR_MANIP_TCP_UDP_SRC       0x80000000              /**< update TCP/UDP source address with the given value
                                                                 ('src' field of t_FmPcdManipHdrFieldUpdateTcpUdp) */
#define HDR_MANIP_TCP_UDP_DST       0x40000000              /**< update TCP/UDP destination address with the given value
                                                                 ('dst' field of t_FmPcdManipHdrFieldUpdateTcpUdp) */
#define HDR_MANIP_TCP_UDP_CHECKSUM  0x20000000             /**< update TCP/UDP checksum */

/* @} */

/**************************************************************************//**
 @Description   A type used for returning the order of the key extraction.
                each value in this array represents the index of the extraction
                command as defined by the user in the initialization extraction array.
                The valid size of this array is the user define number of extractions
                required (also marked by the second '0' in this array).
*//***************************************************************************/
typedef    uint8_t    t_FmPcdKgKeyOrder [FM_PCD_KG_MAX_NUM_OF_EXTRACTS_PER_KEY];

/**************************************************************************//**
 @Description   All PCD engines
*//***************************************************************************/
typedef enum e_FmPcdEngine {
    e_FM_PCD_INVALID = 0,   /**< Invalid PCD engine */
    e_FM_PCD_DONE,          /**< No PCD Engine indicated */
    e_FM_PCD_KG,            /**< KeyGen */
    e_FM_PCD_CC,            /**< Coarse classifier */
    e_FM_PCD_PLCR,          /**< Policer */
    e_FM_PCD_PRS,           /**< Parser */
#if (DPAA_VERSION >= 11)
    e_FM_PCD_FR,            /**< Frame-Replicator */
#endif /* (DPAA_VERSION >= 11) */
    e_FM_PCD_HASH           /**< Hash table */
} e_FmPcdEngine;

/**************************************************************************//**
 @Description   Enumeration type for selecting extraction by header types
*//***************************************************************************/
typedef enum e_FmPcdExtractByHdrType {
    e_FM_PCD_EXTRACT_FROM_HDR,      /**< Extract bytes from header */
    e_FM_PCD_EXTRACT_FROM_FIELD,    /**< Extract bytes from header field */
    e_FM_PCD_EXTRACT_FULL_FIELD     /**< Extract a full field */
} e_FmPcdExtractByHdrType;

/**************************************************************************//**
 @Description   Enumeration type for selecting extraction source
                (when it is not the header)
*//***************************************************************************/
typedef enum e_FmPcdExtractFrom {
    e_FM_PCD_EXTRACT_FROM_FRAME_START,          /**< KG & CC: Extract from beginning of frame */
    e_FM_PCD_EXTRACT_FROM_DFLT_VALUE,           /**< KG only: Extract from a default value */
    e_FM_PCD_EXTRACT_FROM_CURR_END_OF_PARSE,    /**< KG & CC: Extract from the point where parsing had finished */
    e_FM_PCD_EXTRACT_FROM_KEY,                  /**< CC only: Field where saved KEY */
    e_FM_PCD_EXTRACT_FROM_HASH,                 /**< CC only: Field where saved HASH */
    e_FM_PCD_EXTRACT_FROM_PARSE_RESULT,         /**< KG only: Extract from the parser result */
    e_FM_PCD_EXTRACT_FROM_ENQ_FQID,             /**< KG & CC: Extract from enqueue FQID */
    e_FM_PCD_EXTRACT_FROM_FLOW_ID               /**< CC only: Field where saved Dequeue FQID */
} e_FmPcdExtractFrom;

/**************************************************************************//**
 @Description   Enumeration type for selecting extraction type
*//***************************************************************************/
typedef enum e_FmPcdExtractType {
    e_FM_PCD_EXTRACT_BY_HDR,                /**< Extract according to header */
    e_FM_PCD_EXTRACT_NON_HDR,               /**< Extract from data that is not the header */
    e_FM_PCD_KG_EXTRACT_PORT_PRIVATE_INFO   /**< Extract private info as specified by user */
} e_FmPcdExtractType;

/**************************************************************************//**
 @Description   Enumeration type for selecting default extraction value
*//***************************************************************************/
typedef enum e_FmPcdKgExtractDfltSelect {
    e_FM_PCD_KG_DFLT_GBL_0,          /**< Default selection is KG register 0 */
    e_FM_PCD_KG_DFLT_GBL_1,          /**< Default selection is KG register 1 */
    e_FM_PCD_KG_DFLT_PRIVATE_0,      /**< Default selection is a per scheme register 0 */
    e_FM_PCD_KG_DFLT_PRIVATE_1,      /**< Default selection is a per scheme register 1 */
    e_FM_PCD_KG_DFLT_ILLEGAL         /**< Illegal selection */
} e_FmPcdKgExtractDfltSelect;

/**************************************************************************//**
 @Description   Enumeration type defining all default groups - each group shares
                a default value, one of four user-initialized values.
*//***************************************************************************/
typedef enum e_FmPcdKgKnownFieldsDfltTypes {
    e_FM_PCD_KG_MAC_ADDR,               /**< MAC Address */
    e_FM_PCD_KG_TCI,                    /**< TCI field */
    e_FM_PCD_KG_ENET_TYPE,              /**< ENET Type */
    e_FM_PCD_KG_PPP_SESSION_ID,         /**< PPP Session id */
    e_FM_PCD_KG_PPP_PROTOCOL_ID,        /**< PPP Protocol id */
    e_FM_PCD_KG_MPLS_LABEL,             /**< MPLS label */
    e_FM_PCD_KG_IP_ADDR,                /**< IP address */
    e_FM_PCD_KG_PROTOCOL_TYPE,          /**< Protocol type */
    e_FM_PCD_KG_IP_TOS_TC,              /**< TOS or TC */
    e_FM_PCD_KG_IPV6_FLOW_LABEL,        /**< IPV6 flow label */
    e_FM_PCD_KG_IPSEC_SPI,              /**< IPSEC SPI */
    e_FM_PCD_KG_L4_PORT,                /**< L4 Port */
    e_FM_PCD_KG_TCP_FLAG,               /**< TCP Flag */
    e_FM_PCD_KG_GENERIC_FROM_DATA,      /**< grouping implemented by SW,
                                             any data extraction that is not the full
                                             field described above  */
    e_FM_PCD_KG_GENERIC_FROM_DATA_NO_V, /**< grouping implemented by SW,
                                             any data extraction without validation */
    e_FM_PCD_KG_GENERIC_NOT_FROM_DATA   /**< grouping implemented by SW,
                                             extraction from parser result or
                                             direct use of default value  */
} e_FmPcdKgKnownFieldsDfltTypes;

/**************************************************************************//**
 @Description   Enumeration type for defining header index for scenarios with
                multiple (tunneled) headers
*//***************************************************************************/
typedef enum e_FmPcdHdrIndex {
    e_FM_PCD_HDR_INDEX_NONE = 0,        /**< used when multiple headers not used, also
                                             to specify regular IP (not tunneled). */
    e_FM_PCD_HDR_INDEX_1,               /**< may be used for VLAN, MPLS, tunneled IP */
    e_FM_PCD_HDR_INDEX_2,               /**< may be used for MPLS, tunneled IP */
    e_FM_PCD_HDR_INDEX_3,               /**< may be used for MPLS */
    e_FM_PCD_HDR_INDEX_LAST = 0xFF      /**< may be used for VLAN, MPLS */
} e_FmPcdHdrIndex;

/**************************************************************************//**
 @Description   Enumeration type for selecting the policer profile functional type
*//***************************************************************************/
typedef enum e_FmPcdProfileTypeSelection {
    e_FM_PCD_PLCR_PORT_PRIVATE,         /**< Port dedicated profile */
    e_FM_PCD_PLCR_SHARED                /**< Shared profile (shared within partition) */
} e_FmPcdProfileTypeSelection;

/**************************************************************************//**
 @Description   Enumeration type for selecting the policer profile algorithm
*//***************************************************************************/
typedef enum e_FmPcdPlcrAlgorithmSelection {
    e_FM_PCD_PLCR_PASS_THROUGH,         /**< Policer pass through */
    e_FM_PCD_PLCR_RFC_2698,             /**< Policer algorithm RFC 2698 */
    e_FM_PCD_PLCR_RFC_4115              /**< Policer algorithm RFC 4115 */
} e_FmPcdPlcrAlgorithmSelection;

/**************************************************************************//**
 @Description   Enumeration type for selecting a policer profile color mode
*//***************************************************************************/
typedef enum e_FmPcdPlcrColorMode {
    e_FM_PCD_PLCR_COLOR_BLIND,          /**< Color blind */
    e_FM_PCD_PLCR_COLOR_AWARE           /**< Color aware */
} e_FmPcdPlcrColorMode;

/**************************************************************************//**
 @Description   Enumeration type for selecting a policer profile color
*//***************************************************************************/
typedef enum e_FmPcdPlcrColor {
    e_FM_PCD_PLCR_GREEN,                /**< Green color code */
    e_FM_PCD_PLCR_YELLOW,               /**< Yellow color code */
    e_FM_PCD_PLCR_RED,                  /**< Red color code */
    e_FM_PCD_PLCR_OVERRIDE              /**< Color override code */
} e_FmPcdPlcrColor;

/**************************************************************************//**
 @Description   Enumeration type for selecting the policer profile packet frame length selector
*//***************************************************************************/
typedef enum e_FmPcdPlcrFrameLengthSelect {
  e_FM_PCD_PLCR_L2_FRM_LEN,             /**< L2 frame length */
  e_FM_PCD_PLCR_L3_FRM_LEN,             /**< L3 frame length */
  e_FM_PCD_PLCR_L4_FRM_LEN,             /**< L4 frame length */
  e_FM_PCD_PLCR_FULL_FRM_LEN            /**< Full frame length */
} e_FmPcdPlcrFrameLengthSelect;

/**************************************************************************//**
 @Description   Enumeration type for selecting roll-back frame
*//***************************************************************************/
typedef enum e_FmPcdPlcrRollBackFrameSelect {
  e_FM_PCD_PLCR_ROLLBACK_L2_FRM_LEN,    /**< Roll-back L2 frame length */
  e_FM_PCD_PLCR_ROLLBACK_FULL_FRM_LEN   /**< Roll-back Full frame length */
} e_FmPcdPlcrRollBackFrameSelect;

/**************************************************************************//**
 @Description   Enumeration type for selecting the policer profile packet or byte mode
*//***************************************************************************/
typedef enum e_FmPcdPlcrRateMode {
    e_FM_PCD_PLCR_BYTE_MODE,            /**< Byte mode */
    e_FM_PCD_PLCR_PACKET_MODE           /**< Packet mode */
} e_FmPcdPlcrRateMode;

/**************************************************************************//**
 @Description   Enumeration type for defining action of frame
*//***************************************************************************/
typedef enum e_FmPcdDoneAction {
    e_FM_PCD_ENQ_FRAME = 0,        /**< Enqueue frame */
    e_FM_PCD_DROP_FRAME            /**< Mark this frame as error frame and continue
                                        to error flow; 'FM_PORT_FRM_ERR_CLS_DISCARD'
                                        flag will be set for this frame. */
} e_FmPcdDoneAction;

/**************************************************************************//**
 @Description   Enumeration type for selecting the policer counter
*//***************************************************************************/
typedef enum e_FmPcdPlcrProfileCounters {
    e_FM_PCD_PLCR_PROFILE_GREEN_PACKET_TOTAL_COUNTER,               /**< Green packets counter */
    e_FM_PCD_PLCR_PROFILE_YELLOW_PACKET_TOTAL_COUNTER,              /**< Yellow packets counter */
    e_FM_PCD_PLCR_PROFILE_RED_PACKET_TOTAL_COUNTER,                 /**< Red packets counter */
    e_FM_PCD_PLCR_PROFILE_RECOLOURED_YELLOW_PACKET_TOTAL_COUNTER,   /**< Recolored yellow packets counter */
    e_FM_PCD_PLCR_PROFILE_RECOLOURED_RED_PACKET_TOTAL_COUNTER       /**< Recolored red packets counter */
} e_FmPcdPlcrProfileCounters;

/**************************************************************************//**
 @Description   Enumeration type for selecting the PCD action after extraction
*//***************************************************************************/
typedef enum e_FmPcdAction {
    e_FM_PCD_ACTION_NONE,                           /**< NONE  */
    e_FM_PCD_ACTION_EXACT_MATCH,                    /**< Exact match on the selected extraction */
    e_FM_PCD_ACTION_INDEXED_LOOKUP                  /**< Indexed lookup on the selected extraction */
} e_FmPcdAction;

/**************************************************************************//**
 @Description   Enumeration type for selecting type of insert manipulation
*//***************************************************************************/
typedef enum e_FmPcdManipHdrInsrtType {
    e_FM_PCD_MANIP_INSRT_GENERIC,                   /**< Insert according to offset & size */
    e_FM_PCD_MANIP_INSRT_BY_HDR,                    /**< Insert according to protocol */
#if ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT))
    e_FM_PCD_MANIP_INSRT_BY_TEMPLATE                /**< Insert template to start of frame */
#endif /* ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT)) */
} e_FmPcdManipHdrInsrtType;

/**************************************************************************//**
 @Description   Enumeration type for selecting type of remove manipulation
*//***************************************************************************/
typedef enum e_FmPcdManipHdrRmvType {
    e_FM_PCD_MANIP_RMV_GENERIC,                     /**< Remove according to offset & size */
    e_FM_PCD_MANIP_RMV_BY_HDR                       /**< Remove according to offset & size */
} e_FmPcdManipHdrRmvType;

/**************************************************************************//**
 @Description   Enumeration type for selecting specific L2 fields removal
*//***************************************************************************/
typedef enum e_FmPcdManipHdrRmvSpecificL2 {
    e_FM_PCD_MANIP_HDR_RMV_ETHERNET,                /**< Ethernet/802.3 MAC */
    e_FM_PCD_MANIP_HDR_RMV_STACKED_QTAGS,           /**< stacked QTags */
    e_FM_PCD_MANIP_HDR_RMV_ETHERNET_AND_MPLS,       /**< MPLS and Ethernet/802.3 MAC header until
                                                         the header which follows the MPLS header */
    e_FM_PCD_MANIP_HDR_RMV_MPLS,                     /**< Remove MPLS header (Unlimited MPLS labels) */
    e_FM_PCD_MANIP_HDR_RMV_PPPOE                     /**< Remove the PPPoE header and PPP protocol field. */
} e_FmPcdManipHdrRmvSpecificL2;

/**************************************************************************//**
 @Description   Enumeration type for selecting specific fields updates
*//***************************************************************************/
typedef enum e_FmPcdManipHdrFieldUpdateType {
    e_FM_PCD_MANIP_HDR_FIELD_UPDATE_VLAN,               /**< VLAN updates */
    e_FM_PCD_MANIP_HDR_FIELD_UPDATE_IPV4,               /**< IPV4 updates */
    e_FM_PCD_MANIP_HDR_FIELD_UPDATE_IPV6,               /**< IPV6 updates */
    e_FM_PCD_MANIP_HDR_FIELD_UPDATE_TCP_UDP,            /**< TCP_UDP updates */
} e_FmPcdManipHdrFieldUpdateType;

/**************************************************************************//**
 @Description   Enumeration type for selecting VLAN updates
*//***************************************************************************/
typedef enum e_FmPcdManipHdrFieldUpdateVlan {
    e_FM_PCD_MANIP_HDR_FIELD_UPDATE_VLAN_VPRI,      /**< Replace VPri of outer most VLAN tag. */
    e_FM_PCD_MANIP_HDR_FIELD_UPDATE_DSCP_TO_VLAN    /**< DSCP to VLAN priority bits translation */
} e_FmPcdManipHdrFieldUpdateVlan;

/**************************************************************************//**
 @Description   Enumeration type for selecting specific L2 header insertion
*//***************************************************************************/
typedef enum e_FmPcdManipHdrInsrtSpecificL2 {
    e_FM_PCD_MANIP_HDR_INSRT_MPLS,                   /**< Insert MPLS header (Unlimited MPLS labels) */
    e_FM_PCD_MANIP_HDR_INSRT_PPPOE                   /**< Insert PPPOE */
} e_FmPcdManipHdrInsrtSpecificL2;

#if (DPAA_VERSION >= 11)
/**************************************************************************//**
 @Description   Enumeration type for selecting QoS mapping mode

                Note: In all cases except 'e_FM_PCD_MANIP_HDR_QOS_MAPPING_NONE'
                User should instruct the port to read the hash-result
*//***************************************************************************/
typedef enum e_FmPcdManipHdrQosMappingMode {
    e_FM_PCD_MANIP_HDR_QOS_MAPPING_NONE = 0,   /**< No mapping, QoS field will not be changed */
    e_FM_PCD_MANIP_HDR_QOS_MAPPING_AS_IS, /**< QoS field will be overwritten by the last byte in the hash-result. */
} e_FmPcdManipHdrQosMappingMode;

/**************************************************************************//**
 @Description   Enumeration type for selecting QoS source

                Note: In all cases except 'e_FM_PCD_MANIP_HDR_QOS_SRC_NONE'
                User should left room for the hash-result on input/output buffer
                and instruct the port to read/write the hash-result to the buffer (RPD should be set)
*//***************************************************************************/
typedef enum e_FmPcdManipHdrQosSrc {
    e_FM_PCD_MANIP_HDR_QOS_SRC_NONE = 0,        /**< TODO */
    e_FM_PCD_MANIP_HDR_QOS_SRC_USER_DEFINED,    /**< QoS will be taken from the last byte in the hash-result. */
} e_FmPcdManipHdrQosSrc;
#endif /* (DPAA_VERSION >= 11) */

/**************************************************************************//**
 @Description   Enumeration type for selecting type of header insertion
*//***************************************************************************/
typedef enum e_FmPcdManipHdrInsrtByHdrType {
    e_FM_PCD_MANIP_INSRT_BY_HDR_SPECIFIC_L2,        /**< Specific L2 fields insertion */
#if (DPAA_VERSION >= 11)
    e_FM_PCD_MANIP_INSRT_BY_HDR_IP,                 /**< IP insertion */
    e_FM_PCD_MANIP_INSRT_BY_HDR_UDP,                /**< UDP insertion */
    e_FM_PCD_MANIP_INSRT_BY_HDR_UDP_LITE,             /**< UDP lite insertion */
    e_FM_PCD_MANIP_INSRT_BY_HDR_CAPWAP                 /**< CAPWAP insertion */
#endif /* (DPAA_VERSION >= 11) */
} e_FmPcdManipHdrInsrtByHdrType;

/**************************************************************************//**
 @Description   Enumeration type for selecting specific customCommand
*//***************************************************************************/
typedef enum e_FmPcdManipHdrCustomType {
    e_FM_PCD_MANIP_HDR_CUSTOM_IP_REPLACE,           /**< Replace IPv4/IPv6 */
    e_FM_PCD_MANIP_HDR_CUSTOM_GEN_FIELD_REPLACE,     /**< Replace IPv4/IPv6 */
} e_FmPcdManipHdrCustomType;

/**************************************************************************//**
 @Description   Enumeration type for selecting specific customCommand
*//***************************************************************************/
typedef enum e_FmPcdManipHdrCustomIpReplace {
    e_FM_PCD_MANIP_HDR_CUSTOM_REPLACE_IPV4_BY_IPV6,           /**< Replace IPv4 by IPv6 */
    e_FM_PCD_MANIP_HDR_CUSTOM_REPLACE_IPV6_BY_IPV4            /**< Replace IPv6 by IPv4 */
} e_FmPcdManipHdrCustomIpReplace;

/**************************************************************************//**
 @Description   Enumeration type for selecting type of header removal
*//***************************************************************************/
typedef enum e_FmPcdManipHdrRmvByHdrType {
    e_FM_PCD_MANIP_RMV_BY_HDR_SPECIFIC_L2 = 0,      /**< Specific L2 fields removal */
#if (DPAA_VERSION >= 11)
    e_FM_PCD_MANIP_RMV_BY_HDR_CAPWAP,                  /**< CAPWAP removal */
#endif /* (DPAA_VERSION >= 11) */
#if (DPAA_VERSION >= 11) || ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT))
    e_FM_PCD_MANIP_RMV_BY_HDR_FROM_START,           /**< Locate from data that is not the header */
#endif /* (DPAA_VERSION >= 11) || ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT)) */
} e_FmPcdManipHdrRmvByHdrType;

/**************************************************************************//**
 @Description   Enumeration type for selecting type of timeout mode
*//***************************************************************************/
typedef enum e_FmPcdManipReassemTimeOutMode {
    e_FM_PCD_MANIP_TIME_OUT_BETWEEN_FRAMES, /**< Limits the time of the reassembly process
                                                 from the first fragment to the last */
    e_FM_PCD_MANIP_TIME_OUT_BETWEEN_FRAG    /**< Limits the time of receiving the fragment */
} e_FmPcdManipReassemTimeOutMode;

/**************************************************************************//**
 @Description   Enumeration type for selecting type of WaysNumber mode
*//***************************************************************************/
typedef enum e_FmPcdManipReassemWaysNumber {
    e_FM_PCD_MANIP_ONE_WAY_HASH = 1,    /**< One way hash    */
    e_FM_PCD_MANIP_TWO_WAYS_HASH,       /**< Two ways hash   */
    e_FM_PCD_MANIP_THREE_WAYS_HASH,     /**< Three ways hash */
    e_FM_PCD_MANIP_FOUR_WAYS_HASH,      /**< Four ways hash  */
    e_FM_PCD_MANIP_FIVE_WAYS_HASH,      /**< Five ways hash  */
    e_FM_PCD_MANIP_SIX_WAYS_HASH,       /**< Six ways hash   */
    e_FM_PCD_MANIP_SEVEN_WAYS_HASH,     /**< Seven ways hash */
    e_FM_PCD_MANIP_EIGHT_WAYS_HASH      /**< Eight ways hash */
} e_FmPcdManipReassemWaysNumber;

#if ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT))
/**************************************************************************//**
 @Description   Enumeration type for selecting type of statistics mode
*//***************************************************************************/
typedef enum e_FmPcdStatsType {
    e_FM_PCD_STATS_PER_FLOWID = 0       /**< Flow ID is used as index for getting statistics */
} e_FmPcdStatsType;
#endif /* ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT)) */

/**************************************************************************//**
 @Description   Enumeration type for selecting manipulation type
*//***************************************************************************/
typedef enum e_FmPcdManipType {
    e_FM_PCD_MANIP_HDR = 0,             /**< Header manipulation */
    e_FM_PCD_MANIP_REASSEM,             /**< Reassembly */
    e_FM_PCD_MANIP_FRAG,                /**< Fragmentation */
    e_FM_PCD_MANIP_SPECIAL_OFFLOAD      /**< Special Offloading */
} e_FmPcdManipType;

/**************************************************************************//**
 @Description   Enumeration type for selecting type of statistics mode
*//***************************************************************************/
typedef enum e_FmPcdCcStatsMode {
    e_FM_PCD_CC_STATS_MODE_NONE = 0,        /**< No statistics support */
    e_FM_PCD_CC_STATS_MODE_FRAME,           /**< Frame count statistics */
    e_FM_PCD_CC_STATS_MODE_BYTE_AND_FRAME,  /**< Byte and frame count statistics */
#if (DPAA_VERSION >= 11)
    e_FM_PCD_CC_STATS_MODE_RMON,            /**< Byte and frame length range count statistics;
                                                 This mode is supported only on B4860 device */
#endif /* (DPAA_VERSION >= 11) */
} e_FmPcdCcStatsMode;

/**************************************************************************//**
 @Description   Enumeration type for determining the action in case an IP packet
                is larger than MTU but its DF (Don't Fragment) bit is set.
*//***************************************************************************/
typedef enum e_FmPcdManipDontFragAction {
    e_FM_PCD_MANIP_DISCARD_PACKET = 0,                  /**< Discard packet */
    e_FM_PCD_MANIP_ENQ_TO_ERR_Q_OR_DISCARD_PACKET = e_FM_PCD_MANIP_DISCARD_PACKET,
                                                        /**< Obsolete, cannot enqueue to error queue;
                                                             In practice, selects to discard packets;
                                                             Will be removed in the future */
    e_FM_PCD_MANIP_FRAGMENT_PACKET,                     /**< Fragment packet and continue normal processing */
    e_FM_PCD_MANIP_CONTINUE_WITHOUT_FRAG                /**< Continue normal processing without fragmenting the packet */
} e_FmPcdManipDontFragAction;

/**************************************************************************//**
 @Description   Enumeration type for selecting type of special offload manipulation
*//***************************************************************************/
typedef enum e_FmPcdManipSpecialOffloadType {
    e_FM_PCD_MANIP_SPECIAL_OFFLOAD_IPSEC,    /**< IPSec offload manipulation */
#if (DPAA_VERSION >= 11)
    e_FM_PCD_MANIP_SPECIAL_OFFLOAD_CAPWAP    /**< CAPWAP offload manipulation */
#endif /* (DPAA_VERSION >= 11) */
} e_FmPcdManipSpecialOffloadType;


/**************************************************************************//**
 @Description   A Union of protocol dependent special options
*//***************************************************************************/
typedef union u_FmPcdHdrProtocolOpt {
    ethProtocolOpt_t    ethOpt;     /**< Ethernet options */
    vlanProtocolOpt_t   vlanOpt;    /**< VLAN options */
    mplsProtocolOpt_t   mplsOpt;    /**< MPLS options */
    ipv4ProtocolOpt_t   ipv4Opt;    /**< IPv4 options */
    ipv6ProtocolOpt_t   ipv6Opt;    /**< IPv6 options */
#if (DPAA_VERSION >= 11)
    capwapProtocolOpt_t capwapOpt;  /**< CAPWAP options */
#endif /* (DPAA_VERSION >= 11) */
} u_FmPcdHdrProtocolOpt;

/**************************************************************************//**
 @Description   A union holding protocol fields


                Fields supported as "full fields":
                    HEADER_TYPE_ETH:
                        NET_HEADER_FIELD_ETH_DA
                        NET_HEADER_FIELD_ETH_SA
                        NET_HEADER_FIELD_ETH_TYPE

                    HEADER_TYPE_LLC_SNAP:
                        NET_HEADER_FIELD_LLC_SNAP_TYPE

                    HEADER_TYPE_VLAN:
                        NET_HEADER_FIELD_VLAN_TCI
                                (index may apply:
                                 e_FM_PCD_HDR_INDEX_NONE/e_FM_PCD_HDR_INDEX_1,
                                 e_FM_PCD_HDR_INDEX_LAST)

                    HEADER_TYPE_MPLS:
                        NET_HEADER_FIELD_MPLS_LABEL_STACK
                                (index may apply:
                                 e_FM_PCD_HDR_INDEX_NONE/e_FM_PCD_HDR_INDEX_1,
                                 e_FM_PCD_HDR_INDEX_2,
                                 e_FM_PCD_HDR_INDEX_LAST)

                    HEADER_TYPE_IPv4:
                        NET_HEADER_FIELD_IPv4_SRC_IP
                        NET_HEADER_FIELD_IPv4_DST_IP
                        NET_HEADER_FIELD_IPv4_PROTO
                        NET_HEADER_FIELD_IPv4_TOS
                                (index may apply:
                                 e_FM_PCD_HDR_INDEX_NONE/e_FM_PCD_HDR_INDEX_1,
                                 e_FM_PCD_HDR_INDEX_2/e_FM_PCD_HDR_INDEX_LAST)

                    HEADER_TYPE_IPv6:
                        NET_HEADER_FIELD_IPv6_SRC_IP
                        NET_HEADER_FIELD_IPv6_DST_IP
                        NET_HEADER_FIELD_IPv6_NEXT_HDR
                        NET_HEADER_FIELD_IPv6_VER | NET_HEADER_FIELD_IPv6_FL | NET_HEADER_FIELD_IPv6_TC (must come together!)
                                (index may apply:
                                 e_FM_PCD_HDR_INDEX_NONE/e_FM_PCD_HDR_INDEX_1,
                                 e_FM_PCD_HDR_INDEX_2/e_FM_PCD_HDR_INDEX_LAST)

                                (Note that starting from DPAA 1-1, NET_HEADER_FIELD_IPv6_NEXT_HDR applies to
                                 the last next header indication, meaning the next L4, which may be
                                 present at the Ipv6 last extension. On earlier revisions this field
                                 applies to the Next-Header field of the main IPv6 header)

                    HEADER_TYPE_IP:
                        NET_HEADER_FIELD_IP_PROTO
                                (index may apply:
                                 e_FM_PCD_HDR_INDEX_LAST)
                        NET_HEADER_FIELD_IP_DSCP
                                (index may apply:
                                 e_FM_PCD_HDR_INDEX_NONE/e_FM_PCD_HDR_INDEX_1)
                    HEADER_TYPE_GRE:
                        NET_HEADER_FIELD_GRE_TYPE

                    HEADER_TYPE_MINENCAP
                        NET_HEADER_FIELD_MINENCAP_SRC_IP
                        NET_HEADER_FIELD_MINENCAP_DST_IP
                        NET_HEADER_FIELD_MINENCAP_TYPE

                    HEADER_TYPE_TCP:
                        NET_HEADER_FIELD_TCP_PORT_SRC
                        NET_HEADER_FIELD_TCP_PORT_DST
                        NET_HEADER_FIELD_TCP_FLAGS

                    HEADER_TYPE_UDP:
                        NET_HEADER_FIELD_UDP_PORT_SRC
                        NET_HEADER_FIELD_UDP_PORT_DST

                    HEADER_TYPE_UDP_LITE:
                        NET_HEADER_FIELD_UDP_LITE_PORT_SRC
                        NET_HEADER_FIELD_UDP_LITE_PORT_DST

                    HEADER_TYPE_IPSEC_AH:
                        NET_HEADER_FIELD_IPSEC_AH_SPI
                        NET_HEADER_FIELD_IPSEC_AH_NH

                    HEADER_TYPE_IPSEC_ESP:
                        NET_HEADER_FIELD_IPSEC_ESP_SPI

                    HEADER_TYPE_SCTP:
                        NET_HEADER_FIELD_SCTP_PORT_SRC
                        NET_HEADER_FIELD_SCTP_PORT_DST

                    HEADER_TYPE_DCCP:
                        NET_HEADER_FIELD_DCCP_PORT_SRC
                        NET_HEADER_FIELD_DCCP_PORT_DST

                    HEADER_TYPE_PPPoE:
                        NET_HEADER_FIELD_PPPoE_PID
                        NET_HEADER_FIELD_PPPoE_SID

        *****************************************************************
                Fields supported as "from fields":
                    HEADER_TYPE_ETH (with or without validation):
                        NET_HEADER_FIELD_ETH_TYPE

                    HEADER_TYPE_VLAN (with or without validation):
                        NET_HEADER_FIELD_VLAN_TCI
                                (index may apply:
                                 e_FM_PCD_HDR_INDEX_NONE/e_FM_PCD_HDR_INDEX_1,
                                 e_FM_PCD_HDR_INDEX_LAST)

                    HEADER_TYPE_IPv4 (without validation):
                        NET_HEADER_FIELD_IPv4_PROTO
                                (index may apply:
                                 e_FM_PCD_HDR_INDEX_NONE/e_FM_PCD_HDR_INDEX_1,
                                 e_FM_PCD_HDR_INDEX_2/e_FM_PCD_HDR_INDEX_LAST)

                    HEADER_TYPE_IPv6 (without validation):
                        NET_HEADER_FIELD_IPv6_NEXT_HDR
                                (index may apply:
                                 e_FM_PCD_HDR_INDEX_NONE/e_FM_PCD_HDR_INDEX_1,
                                 e_FM_PCD_HDR_INDEX_2/e_FM_PCD_HDR_INDEX_LAST)

*//***************************************************************************/
typedef union t_FmPcdFields {
    headerFieldEth_t            eth;            /**< Ethernet               */
    headerFieldVlan_t           vlan;           /**< VLAN                   */
    headerFieldLlcSnap_t        llcSnap;        /**< LLC SNAP               */
    headerFieldPppoe_t          pppoe;          /**< PPPoE                  */
    headerFieldMpls_t           mpls;           /**< MPLS                   */
    headerFieldIp_t             ip;             /**< IP                     */
    headerFieldIpv4_t           ipv4;           /**< IPv4                   */
    headerFieldIpv6_t           ipv6;           /**< IPv6                   */
    headerFieldUdp_t            udp;            /**< UDP                    */
    headerFieldUdpLite_t        udpLite;        /**< UDP Lite               */
    headerFieldTcp_t            tcp;            /**< TCP                    */
    headerFieldSctp_t           sctp;           /**< SCTP                   */
    headerFieldDccp_t           dccp;           /**< DCCP                   */
    headerFieldGre_t            gre;            /**< GRE                    */
    headerFieldMinencap_t       minencap;       /**< Minimal Encapsulation  */
    headerFieldIpsecAh_t        ipsecAh;        /**< IPSec AH               */
    headerFieldIpsecEsp_t       ipsecEsp;       /**< IPSec ESP              */
    headerFieldUdpEncapEsp_t    udpEncapEsp;    /**< UDP Encapsulation ESP  */
} t_FmPcdFields;

/**************************************************************************//**
 @Description   Parameters for defining header extraction for key generation
*//***************************************************************************/
typedef struct t_FmPcdFromHdr {
    uint8_t             size;           /**< Size in byte */
    uint8_t             offset;         /**< Byte offset */
} t_FmPcdFromHdr;

/**************************************************************************//**
 @Description   Parameters for defining field extraction for key generation
*//***************************************************************************/
typedef struct t_FmPcdFromField {
    t_FmPcdFields       field;          /**< Field selection */
    uint8_t             size;           /**< Size in byte */
    uint8_t             offset;         /**< Byte offset */
} t_FmPcdFromField;

/**************************************************************************//**
 @Description   Parameters for defining a single network environment unit

                A distinction unit should be defined if it will later be used
                by one or more PCD engines to distinguish between flows.
*//***************************************************************************/
typedef struct t_FmPcdDistinctionUnit {
    struct {
        e_NetHeaderType         hdr;        /**< One of the headers supported by the FM */
        u_FmPcdHdrProtocolOpt   opt;        /**< Select only one option ! */
    } hdrs[FM_PCD_MAX_NUM_OF_INTERCHANGEABLE_HDRS];
} t_FmPcdDistinctionUnit;

/**************************************************************************//**
 @Description   Parameters for defining all different distinction units supported
                by a specific PCD Network Environment Characteristics module.

                Each unit represent a protocol or a group of protocols that may
                be used later by the different PCD engines to distinguish
                between flows.
*//***************************************************************************/
typedef struct t_FmPcdNetEnvParams {
    uint8_t                 numOfDistinctionUnits;                      /**< Number of different units to be identified */
    t_FmPcdDistinctionUnit  units[FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS]; /**< An array of numOfDistinctionUnits of the
                                                                             different units to be identified */
} t_FmPcdNetEnvParams;

/**************************************************************************//**
 @Description   Parameters for defining a single extraction action when
                creating a key
*//***************************************************************************/
typedef struct t_FmPcdExtractEntry {
    e_FmPcdExtractType                  type;           /**< Extraction type select */
    union {
        struct {
            e_NetHeaderType             hdr;            /**< Header selection */
            bool                        ignoreProtocolValidation;
                                                        /**< Ignore protocol validation */
            e_FmPcdHdrIndex             hdrIndex;       /**< Relevant only for MPLS, VLAN and tunneled
                                                             IP. Otherwise should be cleared. */
            e_FmPcdExtractByHdrType     type;           /**< Header extraction type select */
            union {
                t_FmPcdFromHdr          fromHdr;        /**< Extract bytes from header parameters */
                t_FmPcdFromField        fromField;      /**< Extract bytes from field parameters */
                t_FmPcdFields           fullField;      /**< Extract full filed parameters */
            } extractByHdrType;
        } extractByHdr;                                 /**< used when type = e_FM_PCD_KG_EXTRACT_BY_HDR */
        struct {
            e_FmPcdExtractFrom          src;            /**< Non-header extraction source */
            e_FmPcdAction               action;         /**< Relevant for CC Only */
            uint16_t                    icIndxMask;     /**< Relevant only for CC when
                                                             action = e_FM_PCD_ACTION_INDEXED_LOOKUP;
                                                             Note that the number of bits that are set within
                                                             this mask must be log2 of the CC-node 'numOfKeys'.
                                                             Note that the mask cannot be set on the lower bits. */
            uint8_t                     offset;         /**< Byte offset */
            uint8_t                     size;           /**< Size in byte */
        } extractNonHdr;                                /**< used when type = e_FM_PCD_KG_EXTRACT_NON_HDR */
    };
} t_FmPcdExtractEntry;

/**************************************************************************//**
 @Description   Parameters for defining masks for each extracted field in the key.
*//***************************************************************************/
typedef struct t_FmPcdKgExtractMask {
    uint8_t                             extractArrayIndex;  /**< Index in the extraction array, as initialized by user */
    uint8_t                             offset;             /**< Byte offset */
    uint8_t                             mask;               /**< A byte mask (selected bits will be used) */
} t_FmPcdKgExtractMask;

/**************************************************************************//**
 @Description   Parameters for defining default selection per groups of fields
*//***************************************************************************/
typedef struct t_FmPcdKgExtractDflt {
    e_FmPcdKgKnownFieldsDfltTypes       type;                /**< Default type select */
    e_FmPcdKgExtractDfltSelect          dfltSelect;          /**< Default register select */
} t_FmPcdKgExtractDflt;

/**************************************************************************//**
 @Description   Parameters for defining key extraction and hashing
*//***************************************************************************/
typedef struct t_FmPcdKgKeyExtractAndHashParams {
    uint32_t                    privateDflt0;                /**< Scheme default register 0 */
    uint32_t                    privateDflt1;                /**< Scheme default register 1 */
    uint8_t                     numOfUsedExtracts;           /**< defines the valid size of the following array */
    t_FmPcdExtractEntry         extractArray [FM_PCD_KG_MAX_NUM_OF_EXTRACTS_PER_KEY]; /**< An array of extractions definition. */
    uint8_t                     numOfUsedDflts;              /**< defines the valid size of the following array */
    t_FmPcdKgExtractDflt        dflts[FM_PCD_KG_NUM_OF_DEFAULT_GROUPS];
                                                             /**< For each extraction used in this scheme, specify the required
                                                                  default register to be used when header is not found.
                                                                  types not specified in this array will get undefined value. */
    uint8_t                     numOfUsedMasks;              /**< defines the valid size of the following array */
    t_FmPcdKgExtractMask        masks[FM_PCD_KG_NUM_OF_EXTRACT_MASKS];
    uint8_t                     hashShift;                   /**< hash result right shift. Select the 24 bits out of the 64 hash
                                                                  result. 0 means using the 24 LSB's, otherwise use the
                                                                  24 LSB's after shifting right.*/
    uint32_t                    hashDistributionNumOfFqids;  /**< must be > 1 and a power of 2. Represents the range
                                                                  of queues for the key and hash functionality */
    uint8_t                     hashDistributionFqidsShift;  /**< selects the FQID bits that will be effected by the hash */
    bool                        symmetricHash;               /**< TRUE to generate the same hash for frames with swapped source and
                                                                  destination fields on all layers; If TRUE, driver will check that for
                                                                  all layers, if SRC extraction is selected, DST extraction must also be
                                                                  selected, and vice versa. */
} t_FmPcdKgKeyExtractAndHashParams;

/**************************************************************************//**
 @Description   Parameters for defining a single FQID mask (extracted OR).
*//***************************************************************************/
typedef struct t_FmPcdKgExtractedOrParams {
    e_FmPcdExtractType              type;               /**< Extraction type select */
    union {
        struct {                                        /**< used when type = e_FM_PCD_KG_EXTRACT_BY_HDR */
            e_NetHeaderType         hdr;
            e_FmPcdHdrIndex         hdrIndex;           /**< Relevant only for MPLS, VLAN and tunneled
                                                             IP. Otherwise should be cleared.*/
            bool                    ignoreProtocolValidation;
                                                        /**< continue extraction even if protocol is not recognized */
        } extractByHdr;                                 /**< Header to extract by */
        e_FmPcdExtractFrom          src;                /**< used when type = e_FM_PCD_KG_EXTRACT_NON_HDR */
    };
    uint8_t                         extractionOffset;   /**< Offset for extraction (in bytes).  */
    e_FmPcdKgExtractDfltSelect      dfltValue;          /**< Select register from which extraction is taken if
                                                             field not found */
    uint8_t                         mask;               /**< Extraction mask (specified bits are used) */
    uint8_t                         bitOffsetInFqid;    /**< 0-31, Selects which bits of the 24 FQID bits to effect using
                                                             the extracted byte; Assume byte is placed as the 8 MSB's in
                                                             a 32 bit word where the lower bits
                                                             are the FQID; i.e if bitOffsetInFqid=1 than its LSB
                                                             will effect the FQID MSB, if bitOffsetInFqid=24 than the
                                                             extracted byte will effect the 8 LSB's of the FQID,
                                                             if bitOffsetInFqid=31 than the byte's MSB will effect
                                                             the FQID's LSB; 0 means - no effect on FQID;
                                                             Note that one, and only one of
                                                             bitOffsetInFqid or bitOffsetInPlcrProfile must be set (i.e,
                                                             extracted byte must effect either FQID or Policer profile).*/
    uint8_t                         bitOffsetInPlcrProfile;
                                                        /**< 0-15, Selects which bits of the 8 policer profile id bits to
                                                             effect using the extracted byte; Assume byte is placed
                                                             as the 8 MSB's in a 16 bit word where the lower bits
                                                             are the policer profile id; i.e if bitOffsetInPlcrProfile=1
                                                             than its LSB will effect the profile MSB, if bitOffsetInFqid=8
                                                             than the extracted byte will effect the whole policer profile id,
                                                             if bitOffsetInFqid=15 than the byte's MSB will effect
                                                             the Policer Profile id's LSB;
                                                             0 means - no effect on policer profile; Note that one, and only one of
                                                             bitOffsetInFqid or bitOffsetInPlcrProfile must be set (i.e,
                                                             extracted byte must effect either FQID or Policer profile).*/
} t_FmPcdKgExtractedOrParams;

/**************************************************************************//**
 @Description   Parameters for configuring a scheme counter
*//***************************************************************************/
typedef struct t_FmPcdKgSchemeCounter {
    bool        update;     /**< FALSE to keep the current counter state
                                 and continue from that point, TRUE to update/reset
                                 the counter when the scheme is written. */
    uint32_t    value;      /**< If update=TRUE, this value will be written into the
                                 counter. clear this field to reset the counter. */
} t_FmPcdKgSchemeCounter;

/**************************************************************************//**
 @Description   Parameters for configuring a policer profile for a KeyGen scheme
                (when policer is the next engine after this scheme).
*//***************************************************************************/
typedef struct t_FmPcdKgPlcrProfile {
    bool                sharedProfile;              /**< TRUE if this profile is shared between ports
                                                         (managed by master partition); Must not be TRUE
                                                         if profile is after Coarse Classification*/
    bool                direct;                     /**< if TRUE, directRelativeProfileId only selects the profile
                                                         id, if FALSE fqidOffsetRelativeProfileIdBase is used
                                                         together with fqidOffsetShift and numOfProfiles
                                                         parameters, to define a range of profiles from
                                                         which the KeyGen result will determine the
                                                         destination policer profile.  */
    union {
        uint16_t        directRelativeProfileId;    /**< Used if 'direct' is TRUE, to select policer profile.
                                                         should indicate the policer profile offset within the
                                                         port's policer profiles or shared window. */
        struct {
            uint8_t     fqidOffsetShift;            /**< Shift on the KeyGen create FQID offset (i.e. not the
                                                         final FQID - without the FQID base). */
            uint8_t     fqidOffsetRelativeProfileIdBase;
                                                    /**< The base of the FMan Port's relative Storage-Profile ID;
                                                         this value will be "OR'ed" with the KeyGen create FQID
                                                         offset (i.e. not the final FQID - without the FQID base);
                                                         the final result should indicate the Storage-Profile offset
                                                         within the FMan Port's relative Storage-Profiles window/
                                                         (or the SHARED window depends on 'sharedProfile'). */
            uint8_t     numOfProfiles;              /**< Range of profiles starting at base */
        } indirectProfile;                          /**< Indirect profile parameters */
    } profileSelect;                                /**< Direct/indirect profile selection and parameters */
} t_FmPcdKgPlcrProfile;

#if (DPAA_VERSION >= 11)
/**************************************************************************//**
 @Description   Parameters for configuring a storage profile for a KeyGen scheme.
*//***************************************************************************/
typedef struct t_FmPcdKgStorageProfile {
    bool                direct;                     /**< If TRUE, directRelativeProfileId only selects the
                                                         profile id;
                                                         If FALSE, fqidOffsetRelativeProfileIdBase is used
                                                         together with fqidOffsetShift and numOfProfiles
                                                         parameters to define a range of profiles from which
                                                         the KeyGen result will determine the destination
                                                         storage profile. */
    union {
        uint16_t        directRelativeProfileId;    /**< Used when 'direct' is TRUE, to select a storage profile;
                                                         should indicate the storage profile offset within the
                                                         port's storage profiles window. */
        struct {
            uint8_t     fqidOffsetShift;            /**< Shift on the KeyGen create FQID offset (i.e. not the
                                                         final FQID - without the FQID base). */
            uint8_t     fqidOffsetRelativeProfileIdBase;
                                                    /**< The base of the FMan Port's relative Storage-Profile ID;
                                                         this value will be "OR'ed" with the KeyGen create FQID
                                                         offset (i.e. not the final FQID - without the FQID base);
                                                         the final result should indicate the Storage-Profile offset
                                                         within the FMan Port's relative Storage-Profiles window. */
            uint8_t     numOfProfiles;              /**< Range of profiles starting at base. */
        } indirectProfile;                          /**< Indirect profile parameters. */
    } profileSelect;                                /**< Direct/indirect profile selection and parameters. */
} t_FmPcdKgStorageProfile;
#endif /* (DPAA_VERSION >= 11) */

/**************************************************************************//**
 @Description   Parameters for defining CC as the next engine after KeyGen
*//***************************************************************************/
typedef struct t_FmPcdKgCc {
    t_Handle                h_CcTree;                       /**< A handle to a CC Tree */
    uint8_t                 grpId;                          /**< CC group id within the CC tree */
    bool                    plcrNext;                       /**< TRUE if after CC, in case of data frame,
                                                                 policing is required. */
    bool                    bypassPlcrProfileGeneration;    /**< TRUE to bypass KeyGen policer profile generation;
                                                                 selected profile is the one set at port initialization. */
    t_FmPcdKgPlcrProfile    plcrProfile;                    /**< Valid only if plcrNext = TRUE and
                                                                 bypassPlcrProfileGeneration = FALSE */
} t_FmPcdKgCc;

/**************************************************************************//**
 @Description   Parameters for defining initializing a KeyGen scheme
*//***************************************************************************/
typedef struct t_FmPcdKgSchemeParams {
    bool                                modify;                 /**< TRUE to change an existing scheme */
    union
    {
        uint8_t                         relativeSchemeId;       /**< if modify=FALSE:Partition relative scheme id */
        t_Handle                        h_Scheme;               /**< if modify=TRUE: a handle of the existing scheme */
    } id;
    bool                                alwaysDirect;           /**< This scheme is reached only directly, i.e. no need
                                                                     for match vector; KeyGen will ignore it when matching */
    struct {                                                    /**< HL Relevant only if alwaysDirect = FALSE */
        t_Handle                        h_NetEnv;               /**< A handle to the Network environment as returned
                                                                     by FM_PCD_NetEnvCharacteristicsSet() */
        uint8_t                         numOfDistinctionUnits;  /**< Number of NetEnv units listed in unitIds array */
        uint8_t                         unitIds[FM_PCD_MAX_NUM_OF_DISTINCTION_UNITS];
                                                                /**< Indexes as passed to SetNetEnvCharacteristics array*/
    } netEnvParams;
    bool                                useHash;                /**< use the KeyGen Hash functionality  */
    t_FmPcdKgKeyExtractAndHashParams    keyExtractAndHashParams;
                                                                /**< used only if useHash = TRUE */
    bool                                bypassFqidGeneration;   /**< Normally - FALSE, TRUE to avoid FQID update in the IC;
                                                                     In such a case FQID after KeyGen will be the default FQID
                                                                     defined for the relevant port, or the FQID defined by CC
                                                                     in cases where CC was the previous engine. */
    uint32_t                            baseFqid;               /**< Base FQID; Relevant only if bypassFqidGeneration = FALSE;
                                                                     If hash is used and an even distribution is expected
                                                                     according to hashDistributionNumOfFqids, baseFqid must be aligned to
                                                                     hashDistributionNumOfFqids. */
    uint8_t                             numOfUsedExtractedOrs;  /**< Number of FQID masks listed in extractedOrs array */
    t_FmPcdKgExtractedOrParams          extractedOrs[FM_PCD_KG_NUM_OF_GENERIC_REGS];
                                                                /**< FM_PCD_KG_NUM_OF_GENERIC_REGS
                                                                     registers are shared between qidMasks
                                                                     functionality and some of the extraction
                                                                     actions; Normally only some will be used
                                                                     for qidMask. Driver will return error if
                                                                     resource is full at initialization time. */

#if (DPAA_VERSION >= 11)
    bool                                overrideStorageProfile; /**< TRUE if KeyGen override previously decided storage profile */
    t_FmPcdKgStorageProfile             storageProfile;         /**< Used when overrideStorageProfile TRUE */
#endif /* (DPAA_VERSION >= 11) */

    e_FmPcdEngine                       nextEngine;             /**< may be BMI, PLCR or CC */
    union {                                                     /**< depends on nextEngine */
        e_FmPcdDoneAction               doneAction;             /**< Used when next engine is BMI (done) */
        t_FmPcdKgPlcrProfile            plcrProfile;            /**< Used when next engine is PLCR */
        t_FmPcdKgCc                     cc;                     /**< Used when next engine is CC */
    } kgNextEngineParams;
    t_FmPcdKgSchemeCounter              schemeCounter;          /**< A structure of parameters for updating
                                                                     the scheme counter */
} t_FmPcdKgSchemeParams;

/**************************************************************************//**
 @Collection    Definitions for CC statistics
*//***************************************************************************/
#if (DPAA_VERSION >= 11)
#define FM_PCD_CC_STATS_MAX_NUM_OF_FLR      10  /* Maximal supported number of frame length ranges */
#define FM_PCD_CC_STATS_FLR_SIZE            2   /* Size in bytes of a frame length range limit */
#endif /* (DPAA_VERSION >= 11) */
#define FM_PCD_CC_STATS_COUNTER_SIZE        4   /* Size in bytes of a frame length range counter */
/* @} */

/**************************************************************************//**
 @Description   Parameters for defining CC as the next engine after a CC node.
*//***************************************************************************/
typedef struct t_FmPcdCcNextCcParams {
    t_Handle    h_CcNode;               /**< A handle of the next CC node */
} t_FmPcdCcNextCcParams;

#if (DPAA_VERSION >= 11)
/**************************************************************************//**
 @Description   Parameters for defining Frame replicator as the next engine after a CC node.
*//***************************************************************************/
typedef struct t_FmPcdCcNextFrParams {
    t_Handle    h_FrmReplic;               /**< A handle of the next frame replicator group */
} t_FmPcdCcNextFrParams;
#endif /* (DPAA_VERSION >= 11) */

/**************************************************************************//**
 @Description   Parameters for defining Policer as the next engine after a CC node.
*//***************************************************************************/
typedef struct t_FmPcdCcNextPlcrParams {
    bool        overrideParams;         /**< TRUE if CC override previously decided parameters*/
    bool        sharedProfile;          /**< Relevant only if overrideParams=TRUE:
                                             TRUE if this profile is shared between ports */
    uint16_t    newRelativeProfileId;   /**< Relevant only if overrideParams=TRUE:
                                             (otherwise profile id is taken from KeyGen);
                                             This parameter should indicate the policer
                                             profile offset within the port's
                                             policer profiles or from SHARED window.*/
    uint32_t    newFqid;                /**< Relevant only if overrideParams=TRUE:
                                             FQID for enqueuing the frame;
                                             In earlier chips  if policer next engine is KEYGEN,
                                             this parameter can be 0, because the KEYGEN
                                             always decides the enqueue FQID.*/
#if (DPAA_VERSION >= 11)
    uint8_t     newRelativeStorageProfileId;
                                        /**< Indicates the relative storage profile offset within
                                             the port's storage profiles window;
                                             Relevant only if the port was configured with VSP. */
#endif /* (DPAA_VERSION >= 11) */
} t_FmPcdCcNextPlcrParams;

/**************************************************************************//**
 @Description   Parameters for defining enqueue as the next action after a CC node.
*//***************************************************************************/
typedef struct t_FmPcdCcNextEnqueueParams {
    e_FmPcdDoneAction    action;        /**< Action - when next engine is BMI (done) */
    bool                 overrideFqid;  /**< TRUE if CC override previously decided fqid and vspid,
                                             relevant if action = e_FM_PCD_ENQ_FRAME */
    uint32_t             newFqid;       /**< Valid if overrideFqid=TRUE, FQID for enqueuing the frame
                                             (otherwise FQID is taken from KeyGen),
                                             relevant if action = e_FM_PCD_ENQ_FRAME */
#if (DPAA_VERSION >= 11)
    uint8_t              newRelativeStorageProfileId;
                                        /**< Valid if overrideFqid=TRUE, Indicates the relative virtual
                                             storage profile offset within the port's storage profiles
                                             window; Relevant only if the port was configured with VSP. */
#endif /* (DPAA_VERSION >= 11) */
} t_FmPcdCcNextEnqueueParams;

/**************************************************************************//**
 @Description   Parameters for defining KeyGen as the next engine after a CC node.
*//***************************************************************************/
typedef struct t_FmPcdCcNextKgParams {
    bool        overrideFqid;           /**< TRUE if CC override previously decided fqid and vspid,
                                             Note - this parameters irrelevant for earlier chips */
    uint32_t    newFqid;                /**< Valid if overrideFqid=TRUE, FQID for enqueuing the frame
                                             (otherwise FQID is taken from KeyGen),
                                             Note - this parameters irrelevant for earlier chips */
#if (DPAA_VERSION >= 11)
    uint8_t     newRelativeStorageProfileId;
                                        /**< Valid if overrideFqid=TRUE, Indicates the relative virtual
                                             storage profile offset within the port's storage profiles
                                             window; Relevant only if the port was configured with VSP. */
#endif /* (DPAA_VERSION >= 11) */

    t_Handle    h_DirectScheme;         /**< Direct scheme handle to go to. */
} t_FmPcdCcNextKgParams;

/**************************************************************************//**
 @Description   Parameters for defining the next engine after a CC node.
*//***************************************************************************/
typedef struct t_FmPcdCcNextEngineParams {
    e_FmPcdEngine                       nextEngine;     /**< User has to initialize parameters
                                                             according to nextEngine definition */
    union {
        t_FmPcdCcNextCcParams           ccParams;       /**< Parameters in case next engine is CC */
        t_FmPcdCcNextPlcrParams         plcrParams;     /**< Parameters in case next engine is PLCR */
        t_FmPcdCcNextEnqueueParams      enqueueParams;  /**< Parameters in case next engine is BMI */
        t_FmPcdCcNextKgParams           kgParams;       /**< Parameters in case next engine is KG */
#if (DPAA_VERSION >= 11)
        t_FmPcdCcNextFrParams           frParams;       /**< Parameters in case next engine is FR */
#endif /* (DPAA_VERSION >= 11) */
    } params;                                           /**< union used for all the next-engine parameters options */

    t_Handle                            h_Manip;        /**< Handle to Manipulation object.
                                                             Relevant if next engine is of type result
                                                             (e_FM_PCD_PLCR, e_FM_PCD_KG, e_FM_PCD_DONE) */

    bool                                statisticsEn;   /**< If TRUE, statistics counters are incremented
                                                             for each frame passing through this
                                                             Coarse Classification entry. */
} t_FmPcdCcNextEngineParams;

/**************************************************************************//**
 @Description   Parameters for defining a single CC key
*//***************************************************************************/
typedef struct t_FmPcdCcKeyParams {
    uint8_t                     *p_Key;     /**< Relevant only if 'action' = e_FM_PCD_ACTION_EXACT_MATCH;
                                                 pointer to the key of the size defined in keySize */
    uint8_t                     *p_Mask;    /**< Relevant only if 'action' = e_FM_PCD_ACTION_EXACT_MATCH;
                                                 pointer to the Mask per key  of the size defined
                                                 in keySize. p_Key and p_Mask (if defined) has to be
                                                 of the same size defined in the keySize;
                                                 NOTE that if this value is equal for all entries whithin
                                                 this table, the driver will automatically use global-mask
                                                 (i.e. one common mask for all entries) instead of private
                                                 one; that is done in order to spare some memory and for
                                                 better performance. */
    t_FmPcdCcNextEngineParams   ccNextEngineParams;
                                            /**< parameters for the next for the defined Key in
                                                 the p_Key */
} t_FmPcdCcKeyParams;

/**************************************************************************//**
 @Description   Parameters for defining CC keys parameters
                The driver supports two methods for CC node allocation: dynamic and static.
                Static mode was created in order to prevent runtime alloc/free
                of FMan memory (MURAM), which may cause fragmentation; in this mode,
                the driver automatically allocates the memory according to
                'maxNumOfKeys' parameter. The driver calculates the maximal memory
                size that may be used for this CC-Node taking into consideration
                'maskSupport' and 'statisticsMode' parameters.
                When 'action' = e_FM_PCD_ACTION_INDEXED_LOOKUP in the extraction
                parameters of this node, 'maxNumOfKeys' must be equal to 'numOfKeys'.
                In dynamic mode, 'maxNumOfKeys' must be zero. At initialization,
                all required structures are allocated according to 'numOfKeys'
                parameter. During runtime modification, these structures are
                re-allocated according to the updated number of keys.

                Please note that 'action' and 'icIndxMask' mentioned in the
                specific parameter explanations are passed in the extraction
                parameters of the node (fields of extractCcParams.extractNonHdr).
*//***************************************************************************/
typedef struct t_KeysParams {
    uint16_t                    maxNumOfKeys;   /**< Maximum number of keys that will (ever) be used in this CC-Node;
                                                     A value of zero may be used for dynamic memory allocation. */
    bool                        maskSupport;    /**< This parameter is relevant only if a node is initialized with
                                                     'action' = e_FM_PCD_ACTION_EXACT_MATCH and maxNumOfKeys > 0;
                                                     Should be TRUE to reserve table memory for key masks, even if
                                                     initial keys do not contain masks, or if the node was initialized
                                                     as 'empty' (without keys); this will allow user to add keys with
                                                     masks at runtime.
                                                     NOTE that if user want to use only global-masks (i.e. one common mask
                                                     for all the entries within this table, this parameter should set to 'FALSE'. */
    e_FmPcdCcStatsMode          statisticsMode; /**< Determines the supported statistics mode for all node's keys.
                                                     To enable statistics gathering, statistics should be enabled per
                                                     every key, using 'statisticsEn' in next engine parameters structure
                                                     of that key;
                                                     If 'maxNumOfKeys' is set, all required structures will be
                                                     preallocated for all keys. */
#if (DPAA_VERSION >= 11)
    uint16_t                    frameLengthRanges[FM_PCD_CC_STATS_MAX_NUM_OF_FLR];
                                                /**< Relevant only for 'RMON' statistics mode
                                                     (this feature is supported only on B4860 device);
                                                     Holds a list of programmable thresholds - for each received frame,
                                                     its length in bytes is examined against these range thresholds and
                                                     the appropriate counter is incremented by 1 - for example, to belong
                                                     to range i, the following should hold:
                                                     range i-1 threshold < frame length <= range i threshold
                                                     Each range threshold must be larger then its preceding range
                                                     threshold, and last range threshold must be 0xFFFF. */
#endif /* (DPAA_VERSION >= 11) */
    uint16_t                    numOfKeys;      /**< Number of initial keys;
                                                     Note that in case of 'action' = e_FM_PCD_ACTION_INDEXED_LOOKUP,
                                                     this field should be power-of-2 of the number of bits that are
                                                     set in 'icIndxMask'. */
    uint8_t                     keySize;        /**< Size of key - for extraction of type FULL_FIELD, 'keySize' has
                                                     to be the standard size of the selected key; For other extraction
                                                     types, 'keySize' has to be as size of extraction; When 'action' =
                                                     e_FM_PCD_ACTION_INDEXED_LOOKUP, 'keySize' must be 2. */
    t_FmPcdCcKeyParams          keyParams[FM_PCD_MAX_NUM_OF_KEYS];
                                                /**< An array with 'numOfKeys' entries, each entry specifies the
                                                     corresponding key parameters;
                                                     When 'action' = e_FM_PCD_ACTION_EXACT_MATCH, this value must not
                                                     exceed 255 (FM_PCD_MAX_NUM_OF_KEYS-1) as the last entry is saved
                                                     for the 'miss' entry. */
    t_FmPcdCcNextEngineParams   ccNextEngineParamsForMiss;
                                                /**< Parameters for defining the next engine when a key is not matched;
                                                     Not relevant if action = e_FM_PCD_ACTION_INDEXED_LOOKUP. */
} t_KeysParams;


/**************************************************************************//**
 @Description   Parameters for defining a CC node
*//***************************************************************************/
typedef struct t_FmPcdCcNodeParams {
    t_FmPcdExtractEntry         extractCcParams;    /**< Extraction parameters */
    t_KeysParams                keysParams;         /**< Keys definition matching the selected extraction */
} t_FmPcdCcNodeParams;

/**************************************************************************//**
 @Description   Parameters for defining a hash table
*//***************************************************************************/
typedef struct t_FmPcdHashTableParams {
    uint16_t                    maxNumOfKeys;               /**< Maximum Number Of Keys that will (ever) be used in this Hash-table */
    e_FmPcdCcStatsMode          statisticsMode;             /**< If not e_FM_PCD_CC_STATS_MODE_NONE, the required structures for the
                                                                 requested statistics mode will be allocated according to maxNumOfKeys. */
    uint8_t                     kgHashShift;                /**< KG-Hash-shift as it was configured in the KG-scheme
                                                                 that leads to this hash-table. */
    uint16_t                    hashResMask;                /**< Mask that will be used on the hash-result;
                                                                 The number-of-sets for this hash will be calculated
                                                                 as (2^(number of bits set in 'hashResMask'));
                                                                 The 4 lower bits must be cleared. */
    uint8_t                     hashShift;                  /**< Byte offset from the beginning of the KeyGen hash result to the
                                                                 2-bytes to be used as hash index. */
    uint8_t                     matchKeySize;               /**< Size of the exact match keys held by the hash buckets */

    t_FmPcdCcNextEngineParams   ccNextEngineParamsForMiss;  /**< Parameters for defining the next engine when a key is not matched */

} t_FmPcdHashTableParams;

/**************************************************************************//**
 @Description   Parameters for defining a CC tree group.

                This structure defines a CC group in terms of NetEnv units
                and the action to be taken in each case. The unitIds list must
                be given in order from low to high indices.

                t_FmPcdCcNextEngineParams is a list of 2^numOfDistinctionUnits
                structures where each defines the next action to be taken for
                each units combination. for example:
                numOfDistinctionUnits = 2
                unitIds = {1,3}
                p_NextEnginePerEntriesInGrp[0] = t_FmPcdCcNextEngineParams for the case that
                                                        unit 1 - not found; unit 3 - not found;
                p_NextEnginePerEntriesInGrp[1] = t_FmPcdCcNextEngineParams for the case that
                                                        unit 1 - not found; unit 3 - found;
                p_NextEnginePerEntriesInGrp[2] = t_FmPcdCcNextEngineParams for the case that
                                                        unit 1 - found; unit 3 - not found;
                p_NextEnginePerEntriesInGrp[3] = t_FmPcdCcNextEngineParams for the case that
                                                        unit 1 - found; unit 3 - found;
*//***************************************************************************/
typedef struct t_FmPcdCcGrpParams {
    uint8_t                     numOfDistinctionUnits;          /**< Up to 4 */
    uint8_t                     unitIds[FM_PCD_MAX_NUM_OF_CC_UNITS];
                                                                /**< Indices of the units as defined in
                                                                     FM_PCD_NetEnvCharacteristicsSet() */
    t_FmPcdCcNextEngineParams   nextEnginePerEntriesInGrp[FM_PCD_MAX_NUM_OF_CC_ENTRIES_IN_GRP];
                                                                /**< Maximum entries per group is 16 */
} t_FmPcdCcGrpParams;

/**************************************************************************//**
 @Description   Parameters for defining CC tree groups
*//***************************************************************************/
typedef struct t_FmPcdCcTreeParams {
    t_Handle                h_NetEnv;                   /**< A handle to the Network environment as returned
                                                             by FM_PCD_NetEnvCharacteristicsSet() */
    uint8_t                 numOfGrps;                  /**< Number of CC groups within the CC tree */
    t_FmPcdCcGrpParams      ccGrpParams[FM_PCD_MAX_NUM_OF_CC_GROUPS];
                                                        /**< Parameters for each group. */
} t_FmPcdCcTreeParams;


/**************************************************************************//**
 @Description   CC key statistics structure
*//***************************************************************************/
typedef struct t_FmPcdCcKeyStatistics {
    uint32_t    byteCount;      /**< This counter reflects byte count of frames that
                                     were matched by this key. */
    uint32_t    frameCount;     /**< This counter reflects count of frames that
                                     were matched by this key. */
#if (DPAA_VERSION >= 11)
    uint32_t    frameLengthRangeCount[FM_PCD_CC_STATS_MAX_NUM_OF_FLR];
                                /**< These counters reflect how many frames matched
                                     this key in 'RMON' statistics mode:
                                     Each counter holds the number of frames of a
                                     specific frames length range, according to the
                                     ranges provided at initialization. */
#endif /* (DPAA_VERSION >= 11) */
} t_FmPcdCcKeyStatistics;

/**************************************************************************//**
 @Description   Parameters for defining policer byte rate
*//***************************************************************************/
typedef struct t_FmPcdPlcrByteRateModeParams {
    e_FmPcdPlcrFrameLengthSelect    frameLengthSelection;   /**< Frame length selection */
    e_FmPcdPlcrRollBackFrameSelect  rollBackFrameSelection; /**< relevant option only e_FM_PCD_PLCR_L2_FRM_LEN,
                                                                 e_FM_PCD_PLCR_FULL_FRM_LEN */
} t_FmPcdPlcrByteRateModeParams;

/**************************************************************************//**
 @Description   Parameters for defining the policer profile (based on
                RFC-2698 or RFC-4115 attributes).
*//***************************************************************************/
typedef struct t_FmPcdPlcrNonPassthroughAlgParams {
    e_FmPcdPlcrRateMode              rateMode;                       /**< Byte mode or Packet mode */
    t_FmPcdPlcrByteRateModeParams    byteModeParams;                 /**< Valid for Byte NULL for Packet */
    uint32_t                         committedInfoRate;              /**< KBits/Second or Packets/Second */
    uint32_t                         committedBurstSize;             /**< Bytes/Packets */
    uint32_t                         peakOrExcessInfoRate;           /**< KBits/Second or Packets/Second */
    uint32_t                         peakOrExcessBurstSize;          /**< Bytes/Packets */
} t_FmPcdPlcrNonPassthroughAlgParams;

/**************************************************************************//**
 @Description   Parameters for defining the next engine after policer
*//***************************************************************************/
typedef union u_FmPcdPlcrNextEngineParams {
    e_FmPcdDoneAction               action;             /**< Action - when next engine is BMI (done) */
    t_Handle                        h_Profile;          /**< Policer profile handle -  used when next engine
                                                             is Policer, must be a SHARED profile */
    t_Handle                        h_DirectScheme;     /**< Direct scheme select - when next engine is KeyGen */
} u_FmPcdPlcrNextEngineParams;

/**************************************************************************//**
 @Description   Parameters for defining the policer profile entry
*//***************************************************************************/
typedef struct t_FmPcdPlcrProfileParams {
    bool                                modify;                     /**< TRUE to change an existing profile */
    union {
        struct {
            e_FmPcdProfileTypeSelection profileType;                /**< Type of policer profile */
            t_Handle                    h_FmPort;                   /**< Relevant for per-port profiles only */
            uint16_t                    relativeProfileId;          /**< Profile id - relative to shared group or to port */
        } newParams;                                                /**< use it when modify = FALSE */
        t_Handle                        h_Profile;                  /**< A handle to a profile - use it when modify=TRUE */
    } id;
    e_FmPcdPlcrAlgorithmSelection       algSelection;               /**< Profile Algorithm PASS_THROUGH, RFC_2698, RFC_4115 */
    e_FmPcdPlcrColorMode                colorMode;                  /**< COLOR_BLIND, COLOR_AWARE */

    union {
        e_FmPcdPlcrColor                dfltColor;                  /**< For Color-Blind Pass-Through mode; the policer will re-color
                                                                         any incoming packet with the default value. */
        e_FmPcdPlcrColor                override;                   /**< For Color-Aware modes; the profile response to a
                                                                         pre-color value of 2'b11. */
    } color;

    t_FmPcdPlcrNonPassthroughAlgParams  nonPassthroughAlgParams;    /**< RFC2698 or RFC4115 parameters */

    e_FmPcdEngine                       nextEngineOnGreen;          /**< Next engine for green-colored frames */
    u_FmPcdPlcrNextEngineParams         paramsOnGreen;              /**< Next engine parameters for green-colored frames  */

    e_FmPcdEngine                       nextEngineOnYellow;         /**< Next engine for yellow-colored frames */
    u_FmPcdPlcrNextEngineParams         paramsOnYellow;             /**< Next engine parameters for yellow-colored frames  */

    e_FmPcdEngine                       nextEngineOnRed;            /**< Next engine for red-colored frames */
    u_FmPcdPlcrNextEngineParams         paramsOnRed;                /**< Next engine parameters for red-colored frames  */

    bool                                trapProfileOnFlowA;         /**< Obsolete - do not use */
    bool                                trapProfileOnFlowB;         /**< Obsolete - do not use */
    bool                                trapProfileOnFlowC;         /**< Obsolete - do not use */
} t_FmPcdPlcrProfileParams;

/**************************************************************************//**
 @Description   Parameters for selecting a location for requested manipulation
*//***************************************************************************/
typedef struct t_FmManipHdrInfo {
    e_NetHeaderType                     hdr;            /**< Header selection */
    e_FmPcdHdrIndex                     hdrIndex;       /**< Relevant only for MPLS, VLAN and tunneled IP. Otherwise should be cleared. */
    bool                                byField;        /**< TRUE if the location of manipulation is according to some field in the specific header*/
    t_FmPcdFields                       fullField;      /**< Relevant only when byField = TRUE: Extract field */
} t_FmManipHdrInfo;

#if ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT))
/**************************************************************************//**
 @Description   Parameters for defining an insertion manipulation
                of type e_FM_PCD_MANIP_INSRT_TO_START_OF_FRAME_TEMPLATE
*//***************************************************************************/
typedef struct t_FmPcdManipHdrInsrtByTemplateParams {
    uint8_t         size;                               /**< Size of insert template to the start of the frame. */
    uint8_t         hdrTemplate[FM_PCD_MAX_MANIP_INSRT_TEMPLATE_SIZE];
                                                        /**< Array of the insertion template. */

    bool            modifyOuterIp;                      /**< TRUE if user want to modify some fields in outer IP. */
    struct {
        uint16_t    ipOuterOffset;                      /**< Offset of outer IP in the insert template, relevant if modifyOuterIp = TRUE.*/
        uint16_t    dscpEcn;                            /**< value of dscpEcn in IP outer, relevant if modifyOuterIp = TRUE.
                                                             in IPV4 dscpEcn only byte - it has to be adjusted to the right*/
        bool        udpPresent;                         /**< TRUE if UDP is present in the insert template, relevant if modifyOuterIp = TRUE.*/
        uint8_t     udpOffset;                          /**< Offset in the insert template of UDP, relevant if modifyOuterIp = TRUE and udpPresent=TRUE.*/
        uint8_t     ipIdentGenId;                       /**< Used by FMan-CTRL to calculate IP-identification field,relevant if modifyOuterIp = TRUE.*/
        bool        recalculateLength;                  /**< TRUE if recalculate length has to be performed due to the engines in the path which can change the frame later, relevant if modifyOuterIp = TRUE.*/
        struct {
            uint8_t blockSize;                          /**< The CAAM block-size; Used by FMan-CTRL to calculate the IP Total Length field.*/
            uint8_t extraBytesAddedAlignedToBlockSize;  /**< Used by FMan-CTRL to calculate the IP Total Length field and UDP length*/
            uint8_t extraBytesAddedNotAlignedToBlockSize;/**< Used by FMan-CTRL to calculate the IP Total Length field and UDP length.*/
        } recalculateLengthParams;                      /**< Recalculate length parameters - relevant if modifyOuterIp = TRUE and recalculateLength = TRUE */
    } modifyOuterIpParams;                              /**< Outer IP modification parameters - ignored if modifyOuterIp is FALSE */

    bool            modifyOuterVlan;                    /**< TRUE if user wants to modify VPri field in the outer VLAN header*/
    struct {
        uint8_t     vpri;                               /**< Value of VPri, relevant if modifyOuterVlan = TRUE
                                                             VPri only 3 bits, it has to be adjusted to the right*/
    } modifyOuterVlanParams;
} t_FmPcdManipHdrInsrtByTemplateParams;

/**************************************************************************//**
 @Description   Parameters for defining CAPWAP fragmentation
*//***************************************************************************/
typedef struct t_CapwapFragmentationParams {
    uint16_t         sizeForFragmentation;              /**< if length of the frame is greater than this value, CAPWAP fragmentation will be executed.*/
    bool             headerOptionsCompr;                /**< TRUE - first fragment include the CAPWAP header options field,
                                                             and all other fragments exclude the CAPWAP options field,
                                                             FALSE - all fragments include CAPWAP header options field. */
} t_CapwapFragmentationParams;

/**************************************************************************//**
 @Description   Parameters for defining CAPWAP reassembly
*//***************************************************************************/
typedef struct t_CapwapReassemblyParams {
    uint16_t                        maxNumFramesInProcess;  /**< Number of frames which can be reassembled concurrently; must be power of 2.
                                                                 In case numOfFramesPerHashEntry == e_FM_PCD_MANIP_FOUR_WAYS_HASH,
                                                                 maxNumFramesInProcess has to be in the range of 4 - 512,
                                                                 In case numOfFramesPerHashEntry == e_FM_PCD_MANIP_EIGHT_WAYS_HASH,
                                                                 maxNumFramesInProcess has to be in the range of 8 - 2048 */
    bool                            haltOnDuplicationFrag;  /**< If TRUE, reassembly process will be halted due to duplicated fragment,
                                                                 and all processed fragments will be enqueued with error indication;
                                                                 If FALSE, only duplicated fragments will be enqueued with error indication. */

    e_FmPcdManipReassemTimeOutMode  timeOutMode;            /**< Expiration delay initialized by the reassembly process */
    uint32_t                        fqidForTimeOutFrames;   /**< FQID in which time out frames will enqueue during Time Out Process  */
    uint32_t                        timeoutRoutineRequestTime;
                                                            /**< Represents the time interval in microseconds between consecutive
                                                                 timeout routine requests It has to be power of 2. */
    uint32_t                        timeoutThresholdForReassmProcess;
                                                            /**< Time interval (microseconds) for marking frames in process as too old;
                                                                 Frames in process are those for which at least one fragment was received
                                                                 but not all fragments. */

    e_FmPcdManipReassemWaysNumber   numOfFramesPerHashEntry;/**< Number of frames per hash entry (needed for the reassembly process) */
} t_CapwapReassemblyParams;

/**************************************************************************//**
 @Description   Parameters for defining fragmentation/reassembly manipulation
*//***************************************************************************/
typedef struct t_FmPcdManipFragOrReasmParams {
    bool                                frag;               /**< TRUE if using the structure for fragmentation,
                                                                 otherwise this structure is used for reassembly */
    uint8_t                             sgBpid;             /**< Scatter/Gather buffer pool id;
                                                                 Same LIODN number is used for these buffers as for
                                                                 the received frames buffers, so buffers of this pool
                                                                 need to be allocated in the same memory area as the
                                                                 received buffers. If the received buffers arrive
                                                                 from different sources, the Scatter/Gather BP id
                                                                 should be mutual to all these sources. */
    e_NetHeaderType                     hdr;                /**< Header selection */
    union {
        t_CapwapFragmentationParams     capwapFragParams;   /**< Structure for CAPWAP fragmentation,
                                                                 relevant if 'frag' = TRUE, 'hdr' = HEADER_TYPE_CAPWAP */
        t_CapwapReassemblyParams        capwapReasmParams;  /**< Structure for CAPWAP reassembly,
                                                                 relevant if 'frag' = FALSE, 'hdr' = HEADER_TYPE_CAPWAP */
    } u;
} t_FmPcdManipFragOrReasmParams;
#endif /* ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT)) */


/**************************************************************************//**
 @Description   Parameters for defining header removal by header type
*//***************************************************************************/
typedef struct t_FmPcdManipHdrRmvByHdrParams {
    e_FmPcdManipHdrRmvByHdrType         type;           /**< Selection of header removal location */
    union {
#if ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT))
        struct {
            bool                        include;        /**< If FALSE, remove until the specified header (not including the header);
                                                             If TRUE, remove also the specified header. */
            t_FmManipHdrInfo            hdrInfo;
        } fromStartByHdr;                               /**< Relevant when type = e_FM_PCD_MANIP_RMV_BY_HDR_FROM_START */
#endif /* (DPAA_VERSION >= 11) || ... */
#if (DPAA_VERSION >= 11)
        t_FmManipHdrInfo                hdrInfo;        /**< Relevant when type = e_FM_PCD_MANIP_RMV_BY_HDR_FROM_START */
#endif /* (DPAA_VERSION >= 11) */
        e_FmPcdManipHdrRmvSpecificL2    specificL2;     /**< Relevant when type = e_FM_PCD_MANIP_BY_HDR_SPECIFIC_L2;
                                                             Defines which L2 headers to remove. */
    } u;
} t_FmPcdManipHdrRmvByHdrParams;

/**************************************************************************//**
 @Description   Parameters for configuring IP fragmentation manipulation

 Restrictions:
     - IP Fragmentation output fragments must not be forwarded to application directly.
     - Maximum number of fragments per frame is 16.
     - Fragmentation of IP fragments is not supported.
     - IPv4 packets containing header Option fields are fragmented by copying all option
       fields to each fragment, regardless of the copy bit value.
     - Transmit confirmation is not supported.
     - Fragmentation after SEC can't handle S/G frames.
     - Fragmentation nodes must be set as the last PCD action (i.e. the
       corresponding CC node key must have next engine set to e_FM_PCD_DONE).
     - Only BMan buffers shall be used for frames to be fragmented.
     - IPF does not support VSP. Therefore, on the same port where we have IPF
       we cannot support VSP.
     - NOTE: The following comment is relevant only for FMAN v3 devices: IPF
       does not support VSP. Therefore, on the same port where we have IPF we
       cannot support VSP.
*//***************************************************************************/
typedef struct t_FmPcdManipFragIpParams {
    uint16_t                    sizeForFragmentation;   /**< If length of the frame is greater than this value,
                                                             IP fragmentation will be executed.*/
#if (DPAA_VERSION == 10)
    uint8_t                     scratchBpid;            /**< Absolute buffer pool id according to BM configuration.*/
#endif /* (DPAA_VERSION == 10) */
    bool                        sgBpidEn;               /**< Enable a dedicated buffer pool id for the Scatter/Gather buffer allocation;
                                                             If disabled, the Scatter/Gather buffer will be allocated from the same pool as the
                                                             received frame's buffer. */
    uint8_t                     sgBpid;                 /**< Scatter/Gather buffer pool id;
                                                             This parameters is relevant when 'sgBpidEn=TRUE';
                                                             Same LIODN number is used for these buffers as for the received frames buffers, so buffers
                                                             of this pool need to be allocated in the same memory area as the received buffers.
                                                             If the received buffers arrive from different sources, the Scatter/Gather BP id should be
                                                             mutual to all these sources. */
    e_FmPcdManipDontFragAction  dontFragAction;         /**< Don't Fragment Action - If an IP packet is larger
                                                             than MTU and its DF bit is set, then this field will
                                                             determine the action to be taken.*/
} t_FmPcdManipFragIpParams;

/**************************************************************************//**
 @Description   Parameters for configuring IP reassembly manipulation.

                This is a common structure for both IPv4 and IPv6 reassembly
                manipulation. For reassembly of both IPv4 and IPv6, make sure to
                set the 'hdr' field in t_FmPcdManipReassemParams to HEADER_TYPE_IPv6.

 Restrictions:
    - Application must define at least one scheme to catch the reassembled frames.
    - Maximum number of fragments per frame is 16.
    - Reassembly of IPv4 fragments containing Option fields is supported.

*//***************************************************************************/
typedef struct t_FmPcdManipReassemIpParams {
    uint8_t                         relativeSchemeId[2];    /**< Partition relative scheme id:
                                                                 relativeSchemeId[0] -  Relative scheme ID for IPV4 Reassembly manipulation;
                                                                 relativeSchemeId[1] -  Relative scheme ID for IPV6 Reassembly manipulation;
                                                                 NOTE: The following comment is relevant only for FMAN v2 devices:
                                                                 Relative scheme ID for IPv4/IPv6 Reassembly manipulation must be smaller than
                                                                 the user schemes id to ensure that the reassembly schemes will be first match;
                                                                 Rest schemes, if defined, should have higher relative scheme ID. */
#if (DPAA_VERSION >= 11)
    uint32_t                        nonConsistentSpFqid;    /**< In case that other fragments of the frame corresponds to different storage
                                                                 profile than the opening fragment (Non-Consistent-SP state)
                                                                 then one of two possible scenarios occurs:
                                                                 if 'nonConsistentSpFqid != 0', the reassembled frame will be enqueued to
                                                                 this fqid, otherwise a 'Non Consistent SP' bit will be set in the FD[status].*/
#else
    uint8_t                         sgBpid;                 /**< Buffer pool id for the S/G frame created by the reassembly process */
#endif /* (DPAA_VERSION >= 11) */
    uint8_t                         dataMemId;              /**< Memory partition ID for the IPR's external tables structure */
    uint16_t                        dataLiodnOffset;        /**< LIODN offset for access the IPR's external tables structure. */
    uint16_t                        minFragSize[2];         /**< Minimum fragment size:
                                                                 minFragSize[0] - for ipv4, minFragSize[1] - for ipv6 */
    e_FmPcdManipReassemWaysNumber   numOfFramesPerHashEntry[2];
                                                            /**< Number of frames per hash entry needed for reassembly process:
                                                                 numOfFramesPerHashEntry[0] - for ipv4 (max value is e_FM_PCD_MANIP_EIGHT_WAYS_HASH);
                                                                 numOfFramesPerHashEntry[1] - for ipv6 (max value is e_FM_PCD_MANIP_SIX_WAYS_HASH). */
    uint16_t                        maxNumFramesInProcess;  /**< Number of frames which can be processed by Reassembly in the same time;
                                                                 Must be power of 2;
                                                                 In the case numOfFramesPerHashEntry == e_FM_PCD_MANIP_FOUR_WAYS_HASH,
                                                                 maxNumFramesInProcess has to be in the range of 4 - 512;
                                                                 In the case numOfFramesPerHashEntry == e_FM_PCD_MANIP_EIGHT_WAYS_HASH,
                                                                 maxNumFramesInProcess has to be in the range of 8 - 2048. */
    e_FmPcdManipReassemTimeOutMode  timeOutMode;            /**< Expiration delay initialized by Reassembly process */
    uint32_t                        fqidForTimeOutFrames;   /**< FQID in which time out frames will enqueue during Time Out Process;
                                                                 Recommended value for this field is 0; in this way timed-out frames will be discarded */
    uint32_t                        timeoutThresholdForReassmProcess;
                                                            /**< Represents the time interval in microseconds which defines
                                                                 if opened frame (at least one fragment was processed but not all the fragments)is found as too old*/
} t_FmPcdManipReassemIpParams;

/**************************************************************************//**
 @Description   structure for defining IPSEC manipulation
*//***************************************************************************/
typedef struct t_FmPcdManipSpecialOffloadIPSecParams {
    bool        decryption;                     /**< TRUE if being used in decryption direction;
                                                     FALSE if being used in encryption direction. */
    bool        ecnCopy;                        /**< TRUE to copy the ECN bits from inner/outer to outer/inner
                                                     (direction depends on the 'decryption' field). */
    bool        dscpCopy;                       /**< TRUE to copy the DSCP bits from inner/outer to outer/inner
                                                     (direction depends on the 'decryption' field). */
    bool        variableIpHdrLen;               /**< TRUE for supporting variable IP header length in decryption. */
    bool        variableIpVersion;              /**< TRUE for supporting both IP version on the same SA in encryption */
    uint8_t     outerIPHdrLen;                  /**< if 'variableIpVersion == TRUE' then this field must be set to non-zero value;
                                                     It is specifies the length of the outer IP header that was configured in the
                                                     corresponding SA. */
    uint16_t    arwSize;                        /**< if <> '0' then will perform ARW check for this SA;
                                                     The value must be a multiplication of 16 */
    uintptr_t   arwAddr;                        /**< if arwSize <> '0' then this field must be set to non-zero value;
                                                     MUST be allocated from FMAN's MURAM that the post-sec op-port belongs to;
                                                     Must be 4B aligned. Required MURAM size is 'NEXT_POWER_OF_2(arwSize+32))/8+4' Bytes */
} t_FmPcdManipSpecialOffloadIPSecParams;

#if (DPAA_VERSION >= 11)
/**************************************************************************//**
 @Description   Parameters for configuring CAPWAP fragmentation manipulation

 Restrictions:
     - Maximum number of fragments per frame is 16.
     - Transmit confirmation is not supported.
     - Fragmentation nodes must be set as the last PCD action (i.e. the
       corresponding CC node key must have next engine set to e_FM_PCD_DONE).
     - Only BMan buffers shall be used for frames to be fragmented.
     - NOTE: The following comment is relevant only for FMAN v3 devices: IPF
       does not support VSP. Therefore, on the same port where we have IPF we
       cannot support VSP.
*//***************************************************************************/
typedef struct t_FmPcdManipFragCapwapParams {
    uint16_t                    sizeForFragmentation;   /**< If length of the frame is greater than this value,
                                                             CAPWAP fragmentation will be executed.*/
    bool                        sgBpidEn;               /**< Enable a dedicated buffer pool id for the Scatter/Gather buffer allocation;
                                                             If disabled, the Scatter/Gather buffer will be allocated from the same pool as the
                                                             received frame's buffer. */
    uint8_t                     sgBpid;                 /**< Scatter/Gather buffer pool id;
                                                             This parameters is relevant when 'sgBpidEn=TRUE';
                                                             Same LIODN number is used for these buffers as for the received frames buffers, so buffers
                                                             of this pool need to be allocated in the same memory area as the received buffers.
                                                             If the received buffers arrive from different sources, the Scatter/Gather BP id should be
                                                             mutual to all these sources. */
    bool                        compressModeEn;         /**< CAPWAP Header Options Compress Enable mode;
                                                             When this mode is enabled then only the first fragment include the CAPWAP header options
                                                             field (if user provides it in the input frame) and all other fragments exclude the CAPWAP
                                                             options field (CAPWAP header is updated accordingly).*/
} t_FmPcdManipFragCapwapParams;

/**************************************************************************//**
 @Description   Parameters for configuring CAPWAP reassembly manipulation.

 Restrictions:
    - Application must define one scheme to catch the reassembled frames.
    - Maximum number of fragments per frame is 16.

*//***************************************************************************/
typedef struct t_FmPcdManipReassemCapwapParams {
    uint8_t                         relativeSchemeId;    /**< Partition relative scheme id;
                                                                 NOTE: this id must be smaller than the user schemes id to ensure that the reassembly scheme will be first match;
                                                                 Rest schemes, if defined, should have higher relative scheme ID. */
    uint8_t                         dataMemId;              /**< Memory partition ID for the IPR's external tables structure */
    uint16_t                        dataLiodnOffset;        /**< LIODN offset for access the IPR's external tables structure. */
    uint16_t                        maxReassembledFrameLength;/**< The maximum CAPWAP reassembled frame length in bytes;
                                                                   If maxReassembledFrameLength == 0, any successful reassembled frame length is
                                                                   considered as a valid length;
                                                                   if maxReassembledFrameLength > 0, a successful reassembled frame which its length
                                                                   exceeds this value is considered as an error frame (FD status[CRE] bit is set). */
    e_FmPcdManipReassemWaysNumber   numOfFramesPerHashEntry;
                                                            /**< Number of frames per hash entry needed for reassembly process */
    uint16_t                        maxNumFramesInProcess;  /**< Number of frames which can be processed by reassembly in the same time;
                                                                 Must be power of 2;
                                                                 In the case numOfFramesPerHashEntry == e_FM_PCD_MANIP_FOUR_WAYS_HASH,
                                                                 maxNumFramesInProcess has to be in the range of 4 - 512;
                                                                 In the case numOfFramesPerHashEntry == e_FM_PCD_MANIP_EIGHT_WAYS_HASH,
                                                                 maxNumFramesInProcess has to be in the range of 8 - 2048. */
    e_FmPcdManipReassemTimeOutMode  timeOutMode;            /**< Expiration delay initialized by Reassembly process */
    uint32_t                        fqidForTimeOutFrames;   /**< FQID in which time out frames will enqueue during Time Out Process;
                                                                 Recommended value for this field is 0; in this way timed-out frames will be discarded */
    uint32_t                        timeoutThresholdForReassmProcess;
                                                            /**< Represents the time interval in microseconds which defines
                                                                 if opened frame (at least one fragment was processed but not all the fragments)is found as too old*/
} t_FmPcdManipReassemCapwapParams;

/**************************************************************************//**
 @Description   structure for defining CAPWAP manipulation
*//***************************************************************************/
typedef struct t_FmPcdManipSpecialOffloadCapwapParams {
    bool                    dtls;   /**< TRUE if continue to SEC DTLS encryption */
    e_FmPcdManipHdrQosSrc   qosSrc; /**< TODO */
} t_FmPcdManipSpecialOffloadCapwapParams;

#endif /* (DPAA_VERSION >= 11) */


/**************************************************************************//**
 @Description   Parameters for defining special offload manipulation
*//***************************************************************************/
typedef struct t_FmPcdManipSpecialOffloadParams {
    e_FmPcdManipSpecialOffloadType              type;       /**< Type of special offload manipulation */
    union
    {
        t_FmPcdManipSpecialOffloadIPSecParams   ipsec;      /**< Parameters for IPSec; Relevant when
                                                                 type = e_FM_PCD_MANIP_SPECIAL_OFFLOAD_IPSEC */
#if (DPAA_VERSION >= 11)
        t_FmPcdManipSpecialOffloadCapwapParams  capwap;     /**< Parameters for CAPWAP; Relevant when
                                                                 type = e_FM_PCD_MANIP_SPECIAL_OFFLOAD_CAPWAP */
#endif /* (DPAA_VERSION >= 11) */
    } u;
} t_FmPcdManipSpecialOffloadParams;

/**************************************************************************//**
 @Description   Parameters for defining insertion manipulation
*//***************************************************************************/
typedef struct t_FmPcdManipHdrInsrt {
    uint8_t size;           /**< size of inserted section */
    uint8_t *p_Data;        /**< data to be inserted */
} t_FmPcdManipHdrInsrt;


/**************************************************************************//**
 @Description   Parameters for defining generic removal manipulation
*//***************************************************************************/
typedef struct t_FmPcdManipHdrRmvGenericParams {
    uint8_t                         offset;         /**< Offset from beginning of header to the start
                                                         location of the removal */
    uint8_t                         size;           /**< Size of removed section */
} t_FmPcdManipHdrRmvGenericParams;

/**************************************************************************//**
 @Description   Parameters for defining generic insertion manipulation
*//***************************************************************************/
typedef struct t_FmPcdManipHdrInsrtGenericParams {
    uint8_t                         offset;         /**< Offset from beginning of header to the start
                                                         location of the insertion */
    uint8_t                         size;           /**< Size of inserted section */
    bool                            replace;        /**< TRUE to override (replace) existing data at
                                                         'offset', FALSE to insert */
    uint8_t                         *p_Data;        /**< Pointer to data to be inserted */
} t_FmPcdManipHdrInsrtGenericParams;

/**************************************************************************//**
 @Description   Parameters for defining header manipulation VLAN DSCP To Vpri translation
*//***************************************************************************/
typedef struct t_FmPcdManipHdrFieldUpdateVlanDscpToVpri {
    uint8_t                         dscpToVpriTable[FM_PCD_MANIP_DSCP_TO_VLAN_TRANS];
                                                        /**< A table of VPri values for each DSCP value;
                                                             The index is the DSCP value (0-0x3F) and the
                                                             value is the corresponding VPRI (0-15). */
    uint8_t                         vpriDefVal;         /**< 0-7, Relevant only if if updateType =
                                                             e_FM_PCD_MANIP_HDR_FIELD_UPDATE_DSCP_TO_VLAN,
                                                             this field is the Q Tag default value if the
                                                             IP header is not found. */
} t_FmPcdManipHdrFieldUpdateVlanDscpToVpri;

/**************************************************************************//**
 @Description   Parameters for defining header manipulation VLAN fields updates
*//***************************************************************************/
typedef struct t_FmPcdManipHdrFieldUpdateVlan {
    e_FmPcdManipHdrFieldUpdateVlan                  updateType; /**< Selects VLAN update type */
    union {
        uint8_t                                     vpri;       /**< 0-7, Relevant only if If updateType =
                                                                     e_FM_PCD_MANIP_HDR_FIELD_UPDATE_VLAN_PRI, this
                                                                     is the new VLAN pri. */
        t_FmPcdManipHdrFieldUpdateVlanDscpToVpri    dscpToVpri; /**< Parameters structure, Relevant only if updateType
                                                                     = e_FM_PCD_MANIP_HDR_FIELD_UPDATE_DSCP_TO_VLAN. */
    } u;
} t_FmPcdManipHdrFieldUpdateVlan;

/**************************************************************************//**
 @Description   Parameters for defining header manipulation IPV4 fields updates
*//***************************************************************************/
typedef struct t_FmPcdManipHdrFieldUpdateIpv4 {
    ipv4HdrManipUpdateFlags_t       validUpdates;       /**< ORed flag, selecting the required updates */
    uint8_t                         tos;                /**< 8 bit New TOS; Relevant if validUpdates contains
                                                             HDR_MANIP_IPV4_TOS */
    uint16_t                        id;                 /**< 16 bit New IP ID; Relevant only if validUpdates
                                                             contains HDR_MANIP_IPV4_ID */
    uint32_t                        src;                /**< 32 bit New IP SRC; Relevant only if validUpdates
                                                             contains HDR_MANIP_IPV4_SRC */
    uint32_t                        dst;                /**< 32 bit New IP DST; Relevant only if validUpdates
                                                             contains HDR_MANIP_IPV4_DST */
} t_FmPcdManipHdrFieldUpdateIpv4;

/**************************************************************************//**
 @Description   Parameters for defining header manipulation IPV6 fields updates
*//***************************************************************************/
typedef struct t_FmPcdManipHdrFieldUpdateIpv6 {
    ipv6HdrManipUpdateFlags_t   validUpdates;           /**< ORed flag, selecting the required updates */
    uint8_t                     trafficClass;           /**< 8 bit New Traffic Class; Relevant if validUpdates contains
                                                             HDR_MANIP_IPV6_TC */
    uint8_t                     src[NET_HEADER_FIELD_IPv6_ADDR_SIZE];
                                                        /**< 16 byte new IP SRC; Relevant only if validUpdates
                                                             contains HDR_MANIP_IPV6_SRC */
    uint8_t                     dst[NET_HEADER_FIELD_IPv6_ADDR_SIZE];
                                                        /**< 16 byte new IP DST; Relevant only if validUpdates
                                                             contains HDR_MANIP_IPV6_DST */
} t_FmPcdManipHdrFieldUpdateIpv6;

/**************************************************************************//**
 @Description   Parameters for defining header manipulation TCP/UDP fields updates
*//***************************************************************************/
typedef struct t_FmPcdManipHdrFieldUpdateTcpUdp {
    tcpUdpHdrManipUpdateFlags_t     validUpdates;       /**< ORed flag, selecting the required updates */
    uint16_t                        src;                /**< 16 bit New TCP/UDP SRC; Relevant only if validUpdates
                                                             contains HDR_MANIP_TCP_UDP_SRC */
    uint16_t                        dst;                /**< 16 bit New TCP/UDP DST; Relevant only if validUpdates
                                                             contains HDR_MANIP_TCP_UDP_DST */
} t_FmPcdManipHdrFieldUpdateTcpUdp;

/**************************************************************************//**
 @Description   Parameters for defining header manipulation fields updates
*//***************************************************************************/
typedef struct t_FmPcdManipHdrFieldUpdateParams {
    e_FmPcdManipHdrFieldUpdateType                  type;           /**< Type of header field update manipulation */
    union {
        t_FmPcdManipHdrFieldUpdateVlan              vlan;           /**< Parameters for VLAN update. Relevant when
                                                                         type = e_FM_PCD_MANIP_HDR_FIELD_UPDATE_VLAN */
        t_FmPcdManipHdrFieldUpdateIpv4              ipv4;           /**< Parameters for IPv4 update. Relevant when
                                                                         type = e_FM_PCD_MANIP_HDR_FIELD_UPDATE_IPV4 */
        t_FmPcdManipHdrFieldUpdateIpv6              ipv6;           /**< Parameters for IPv6 update. Relevant when
                                                                         type = e_FM_PCD_MANIP_HDR_FIELD_UPDATE_IPV6 */
        t_FmPcdManipHdrFieldUpdateTcpUdp            tcpUdp;         /**< Parameters for TCP/UDP update. Relevant when
                                                                         type = e_FM_PCD_MANIP_HDR_FIELD_UPDATE_TCP_UDP */
    } u;
} t_FmPcdManipHdrFieldUpdateParams;



/**************************************************************************//**
 @Description   Parameters for defining custom header manipulation for generic field replacement
*//***************************************************************************/
typedef struct t_FmPcdManipHdrCustomGenFieldReplace {
    uint8_t                         srcOffset;          /**< Location of new data - Offset from
                                                             Parse Result  (>= 16, srcOffset+size <= 32, ) */
    uint8_t                         dstOffset;          /**< Location of data to be overwritten - Offset from
                                                             start of frame (dstOffset + size <= 256). */
    uint8_t                         size;               /**< The number of bytes (<=16) to be replaced */
    uint8_t                         mask;               /**< Optional 1 byte mask. Set to select bits for
                                                             replacement (1 - bit will be replaced);
                                                             Clear to use field as is. */
    uint8_t                         maskOffset;         /**< Relevant if mask != 0;
                                                             Mask offset within the replaces "size" */
} t_FmPcdManipHdrCustomGenFieldReplace;

/**************************************************************************//**
 @Description   Parameters for defining custom header manipulation for IP replacement
*//***************************************************************************/
typedef struct t_FmPcdManipHdrCustomIpHdrReplace {
    e_FmPcdManipHdrCustomIpReplace  replaceType;        /**< Selects replace update type */
    bool                            decTtlHl;           /**< Decrement TTL (IPV4) or Hop limit (IPV6) by 1  */
    bool                            updateIpv4Id;       /**< Relevant when replaceType =
                                                             e_FM_PCD_MANIP_HDR_CUSTOM_REPLACE_IPV6_BY_IPV4 */
    uint16_t                        id;                 /**< 16 bit New IP ID; Relevant only if
                                                             updateIpv4Id = TRUE */
    uint8_t                         hdrSize;            /**< The size of the new IP header */
    uint8_t                         hdr[FM_PCD_MANIP_MAX_HDR_SIZE];
                                                        /**< The new IP header */
} t_FmPcdManipHdrCustomIpHdrReplace;

/**************************************************************************//**
 @Description   Parameters for defining custom header manipulation
*//***************************************************************************/
typedef struct t_FmPcdManipHdrCustomParams {
    e_FmPcdManipHdrCustomType                   type;           /**< Type of header field update manipulation */
    union {
        t_FmPcdManipHdrCustomIpHdrReplace       ipHdrReplace;   /**< Parameters IP header replacement */
        t_FmPcdManipHdrCustomGenFieldReplace    genFieldReplace;   /**< Parameters IP header replacement */
    } u;
} t_FmPcdManipHdrCustomParams;

/**************************************************************************//**
 @Description   Parameters for defining specific L2 insertion manipulation
*//***************************************************************************/
typedef struct t_FmPcdManipHdrInsrtSpecificL2Params {
    e_FmPcdManipHdrInsrtSpecificL2  specificL2;     /**< Selects which L2 headers to insert */
    bool                            update;         /**< TRUE to update MPLS header */
    uint8_t                         size;           /**< size of inserted section */
    uint8_t                         *p_Data;        /**< data to be inserted */
} t_FmPcdManipHdrInsrtSpecificL2Params;

#if (DPAA_VERSION >= 11)
/**************************************************************************//**
 @Description   Parameters for defining IP insertion manipulation
*//***************************************************************************/
typedef struct t_FmPcdManipHdrInsrtIpParams {
    bool                            calcL4Checksum; /**< Calculate L4 checksum. */
    e_FmPcdManipHdrQosMappingMode   mappingMode;    /**< TODO */
    uint8_t                         lastPidOffset;  /**< the offset of the last Protocol within
                                                         the inserted header */
    uint16_t                        id;         /**< 16 bit New IP ID */
    bool                            dontFragOverwrite;
    /**< IPv4 only. DF is overwritten with the hash-result next-to-last byte.
     * This byte is configured to be overwritten when RPD is set. */
    uint8_t                         lastDstOffset;
    /**< IPv6 only. if routing extension exist, user should set the offset of the destination address
     * in order to calculate UDP checksum pseudo header;
     * Otherwise set it to '0'. */
    t_FmPcdManipHdrInsrt            insrt;      /**< size and data to be inserted. */
} t_FmPcdManipHdrInsrtIpParams;
#endif /* (DPAA_VERSION >= 11) */

/**************************************************************************//**
 @Description   Parameters for defining header insertion manipulation by header type
*//***************************************************************************/
typedef struct t_FmPcdManipHdrInsrtByHdrParams {
    e_FmPcdManipHdrInsrtByHdrType               type;   /**< Selects manipulation type */
    union {

        t_FmPcdManipHdrInsrtSpecificL2Params    specificL2Params;
                                                             /**< Used when type = e_FM_PCD_MANIP_INSRT_BY_HDR_SPECIFIC_L2:
                                                              Selects which L2 headers to insert */
#if (DPAA_VERSION >= 11)
        t_FmPcdManipHdrInsrtIpParams            ipParams;  /**< Used when type = e_FM_PCD_MANIP_INSRT_BY_HDR_IP */
        t_FmPcdManipHdrInsrt                    insrt;     /**< Used when type is one of e_FM_PCD_MANIP_INSRT_BY_HDR_UDP,
                                                                e_FM_PCD_MANIP_INSRT_BY_HDR_UDP_LITE, or
                                                                e_FM_PCD_MANIP_INSRT_BY_HDR_CAPWAP */
#endif /* (DPAA_VERSION >= 11) */
    } u;
} t_FmPcdManipHdrInsrtByHdrParams;

/**************************************************************************//**
 @Description   Parameters for defining header insertion manipulation
*//***************************************************************************/
typedef struct t_FmPcdManipHdrInsrtParams {
    e_FmPcdManipHdrInsrtType                    type;       /**< Type of insertion manipulation */
    union {
        t_FmPcdManipHdrInsrtByHdrParams         byHdr;      /**< Parameters for defining header insertion manipulation by header type,
                                                                 relevant if 'type' = e_FM_PCD_MANIP_INSRT_BY_HDR */
        t_FmPcdManipHdrInsrtGenericParams       generic;    /**< Parameters for defining generic header insertion manipulation,
                                                                 relevant if 'type' = e_FM_PCD_MANIP_INSRT_GENERIC */
#if ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT))
        t_FmPcdManipHdrInsrtByTemplateParams    byTemplate; /**< Parameters for defining header insertion manipulation by template,
                                                                 relevant if 'type' = e_FM_PCD_MANIP_INSRT_BY_TEMPLATE */
#endif /* ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT)) */
    } u;
} t_FmPcdManipHdrInsrtParams;

/**************************************************************************//**
 @Description   Parameters for defining header removal manipulation
*//***************************************************************************/
typedef struct t_FmPcdManipHdrRmvParams {
    e_FmPcdManipHdrRmvType                  type;       /**< Type of header removal manipulation */
    union {
        t_FmPcdManipHdrRmvByHdrParams       byHdr;      /**< Parameters for defining header removal manipulation by header type,
                                                             relevant if type = e_FM_PCD_MANIP_RMV_BY_HDR */
        t_FmPcdManipHdrRmvGenericParams     generic;    /**< Parameters for defining generic header removal manipulation,
                                                             relevant if type = e_FM_PCD_MANIP_RMV_GENERIC */
    } u;
} t_FmPcdManipHdrRmvParams;

/**************************************************************************//**
 @Description   Parameters for defining header manipulation node
*//***************************************************************************/
typedef struct t_FmPcdManipHdrParams {
    bool                                        rmv;                /**< TRUE, to define removal manipulation */
    t_FmPcdManipHdrRmvParams                    rmvParams;          /**< Parameters for removal manipulation, relevant if 'rmv' = TRUE */

    bool                                        insrt;              /**< TRUE, to define insertion manipulation */
    t_FmPcdManipHdrInsrtParams                  insrtParams;        /**< Parameters for insertion manipulation, relevant if 'insrt' = TRUE */

    bool                                        fieldUpdate;        /**< TRUE, to define field update manipulation */
    t_FmPcdManipHdrFieldUpdateParams            fieldUpdateParams;  /**< Parameters for field update manipulation, relevant if 'fieldUpdate' = TRUE */

    bool                                        custom;             /**< TRUE, to define custom manipulation */
    t_FmPcdManipHdrCustomParams                 customParams;       /**< Parameters for custom manipulation, relevant if 'custom' = TRUE */

    bool                                        dontParseAfterManip;/**< TRUE to de-activate the parser after the manipulation defined in this node.
                                                                                          Restrictions:
                                                                                          1. MUST be set if the next engine after the CC is not another CC node
                                                                                          (but rather Policer or Keygen), and this is the last (no h_NextManip) in a chain
                                                                                          of manipulation nodes. This includes single nodes (i.e. no h_NextManip and
                                                                                          also never pointed as h_NextManip of other manipulation nodes)
                                                                                          2. MUST be set if the next engine after the CC is another CC node, and
                                                                                          this is NOT the last manipulation node (i.e. it has h_NextManip).*/
} t_FmPcdManipHdrParams;

/**************************************************************************//**
 @Description   Parameters for defining fragmentation manipulation
*//***************************************************************************/
typedef struct t_FmPcdManipFragParams {
    e_NetHeaderType                     hdr;          /**< Header selection */
    union {
#if (DPAA_VERSION >= 11)
        t_FmPcdManipFragCapwapParams    capwapFrag;   /**< Parameters for defining CAPWAP fragmentation,
                                                           relevant if 'hdr' = HEADER_TYPE_CAPWAP */
#endif /* (DPAA_VERSION >= 11) */
        t_FmPcdManipFragIpParams        ipFrag;       /**< Parameters for defining IP fragmentation,
                                                           relevant if 'hdr' = HEADER_TYPE_Ipv4 or HEADER_TYPE_Ipv6 */
    } u;
} t_FmPcdManipFragParams;

/**************************************************************************//**
 @Description   Parameters for defining reassembly manipulation
*//***************************************************************************/
typedef struct t_FmPcdManipReassemParams {
    e_NetHeaderType                     hdr;          /**< Header selection */
    union {
#if (DPAA_VERSION >= 11)
        t_FmPcdManipReassemCapwapParams capwapReassem;  /**< Parameters for defining CAPWAP reassembly,
                                                           relevant if 'hdr' = HEADER_TYPE_CAPWAP */
#endif /* (DPAA_VERSION >= 11) */

        t_FmPcdManipReassemIpParams     ipReassem;    /**< Parameters for defining IP reassembly,
                                                           relevant if 'hdr' = HEADER_TYPE_Ipv4 or HEADER_TYPE_Ipv6 */
    } u;
} t_FmPcdManipReassemParams;

/**************************************************************************//**
 @Description   Parameters for defining a manipulation node
*//***************************************************************************/
typedef struct t_FmPcdManipParams {
    e_FmPcdManipType                        type;               /**< Selects type of manipulation node */
    union{
        t_FmPcdManipHdrParams               hdr;                /**< Parameters for defining header manipulation node */
        t_FmPcdManipReassemParams           reassem;            /**< Parameters for defining reassembly manipulation node */
        t_FmPcdManipFragParams              frag;               /**< Parameters for defining fragmentation manipulation node */
        t_FmPcdManipSpecialOffloadParams    specialOffload;     /**< Parameters for defining special offload manipulation node */
    } u;

    t_Handle                                h_NextManip;        /**< Supported for Header Manipulation only;
                                                                     Handle to another (previously defined) manipulation node;
                                                                     Allows concatenation of manipulation actions;
                                                                     This parameter is optional and may be NULL. */
#if ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT))
    bool                                    fragOrReasm;        /**< TRUE, if defined fragmentation/reassembly manipulation */
    t_FmPcdManipFragOrReasmParams           fragOrReasmParams;  /**< Parameters for fragmentation/reassembly manipulation,
                                                                     relevant if fragOrReasm = TRUE */
#endif /* ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT)) */
} t_FmPcdManipParams;

/**************************************************************************//**
 @Description   Structure for retrieving IP reassembly statistics
*//***************************************************************************/
typedef struct t_FmPcdManipReassemIpStats {
    /* common counters for both IPv4 and IPv6 */
    uint32_t        timeout;                    /**< Counts the number of timeout occurrences */
    uint32_t        rfdPoolBusy;                /**< Counts the number of failed attempts to allocate
                                                     a Reassembly Frame Descriptor */
    uint32_t        internalBufferBusy;         /**< Counts the number of times an internal buffer busy occurred */
    uint32_t        externalBufferBusy;         /**< Counts the number of times external buffer busy occurred */
    uint32_t        sgFragments;                /**< Counts the number of Scatter/Gather fragments */
    uint32_t        dmaSemaphoreDepletion;      /**< Counts the number of failed attempts to allocate a DMA semaphore */
#if (DPAA_VERSION >= 11)
    uint32_t        nonConsistentSp;            /**< Counts the number of Non Consistent Storage Profile events for
                                                     successfully reassembled frames */
#endif /* (DPAA_VERSION >= 11) */
    struct {
        uint32_t    successfullyReassembled;    /**< Counts the number of successfully reassembled frames */
        uint32_t    validFragments;             /**< Counts the total number of valid fragments that
                                                     have been processed for all frames */
        uint32_t    processedFragments;         /**< Counts the number of processed fragments
                                                     (valid and error fragments) for all frames */
        uint32_t    malformedFragments;         /**< Counts the number of malformed fragments processed for all frames */
        uint32_t    discardedFragments;         /**< Counts the number of fragments discarded by the reassembly process */
        uint32_t    autoLearnBusy;              /**< Counts the number of times a busy condition occurs when attempting
                                                     to access an IP-Reassembly Automatic Learning Hash set */
        uint32_t    moreThan16Fragments;        /**< Counts the fragment occurrences in which the number of fragments-per-frame
                                                     exceeds 16 */
    } specificHdrStatistics[2];                 /**< slot '0' is for IPv4, slot '1' is for IPv6 */
} t_FmPcdManipReassemIpStats;

/**************************************************************************//**
 @Description   Structure for retrieving IP fragmentation statistics
*//***************************************************************************/
typedef struct t_FmPcdManipFragIpStats {
    uint32_t    totalFrames;            /**< Number of frames that passed through the manipulation node */
    uint32_t    fragmentedFrames;       /**< Number of frames that were fragmented */
    uint32_t    generatedFragments;     /**< Number of fragments that were generated */
} t_FmPcdManipFragIpStats;

#if (DPAA_VERSION >= 11)
/**************************************************************************//**
 @Description   Structure for retrieving CAPWAP reassembly statistics
*//***************************************************************************/
typedef struct t_FmPcdManipReassemCapwapStats {
    uint32_t    timeout;                    /**< Counts the number of timeout occurrences */
    uint32_t    rfdPoolBusy;                /**< Counts the number of failed attempts to allocate
                                                 a Reassembly Frame Descriptor */
    uint32_t    internalBufferBusy;         /**< Counts the number of times an internal buffer busy occurred */
    uint32_t    externalBufferBusy;         /**< Counts the number of times external buffer busy occurred */
    uint32_t    sgFragments;                /**< Counts the number of Scatter/Gather fragments */
    uint32_t    dmaSemaphoreDepletion;      /**< Counts the number of failed attempts to allocate a DMA semaphore */
    uint32_t    successfullyReassembled;    /**< Counts the number of successfully reassembled frames */
    uint32_t    validFragments;             /**< Counts the total number of valid fragments that
                                                 have been processed for all frames */
    uint32_t    processedFragments;         /**< Counts the number of processed fragments
                                                 (valid and error fragments) for all frames */
    uint32_t    malformedFragments;         /**< Counts the number of malformed fragments processed for all frames */
    uint32_t    autoLearnBusy;              /**< Counts the number of times a busy condition occurs when attempting
                                                 to access an Reassembly Automatic Learning Hash set */
    uint32_t    discardedFragments;         /**< Counts the number of fragments discarded by the reassembly process */
    uint32_t    moreThan16Fragments;        /**< Counts the fragment occurrences in which the number of fragments-per-frame
                                                 exceeds 16 */
    uint32_t    exceedMaxReassemblyFrameLen;/**< ounts the number of times that a successful reassembled frame
                                                 length exceeds MaxReassembledFrameLength value */
} t_FmPcdManipReassemCapwapStats;

/**************************************************************************//**
 @Description   Structure for retrieving CAPWAP fragmentation statistics
*//***************************************************************************/
typedef struct t_FmPcdManipFragCapwapStats {
    uint32_t    totalFrames;            /**< Number of frames that passed through the manipulation node */
    uint32_t    fragmentedFrames;       /**< Number of frames that were fragmented */
    uint32_t    generatedFragments;     /**< Number of fragments that were generated */
#if (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0))
    uint8_t     sgAllocationFailure;    /**< Number of allocation failure of s/g buffers */
#endif /* (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0)) */
} t_FmPcdManipFragCapwapStats;
#endif /* (DPAA_VERSION >= 11) */

/**************************************************************************//**
 @Description   Structure for retrieving reassembly statistics
*//***************************************************************************/
typedef struct t_FmPcdManipReassemStats {
    union {
        t_FmPcdManipReassemIpStats  ipReassem;  /**< Structure for IP reassembly statistics */
#if (DPAA_VERSION >= 11)
        t_FmPcdManipReassemCapwapStats  capwapReassem;  /**< Structure for CAPWAP reassembly statistics */
#endif /* (DPAA_VERSION >= 11) */
    } u;
} t_FmPcdManipReassemStats;

/**************************************************************************//**
 @Description   Structure for retrieving fragmentation statistics
*//***************************************************************************/
typedef struct t_FmPcdManipFragStats {
    union {
        t_FmPcdManipFragIpStats     ipFrag;     /**< Structure for IP fragmentation statistics */
#if (DPAA_VERSION >= 11)
        t_FmPcdManipFragCapwapStats capwapFrag; /**< Structure for CAPWAP fragmentation statistics */
#endif /* (DPAA_VERSION >= 11) */
    } u;
} t_FmPcdManipFragStats;

/**************************************************************************//**
 @Description   Structure for selecting manipulation statistics
*//***************************************************************************/
typedef struct t_FmPcdManipStats {
    union {
        t_FmPcdManipReassemStats    reassem;    /**< Structure for reassembly statistics */
        t_FmPcdManipFragStats       frag;       /**< Structure for fragmentation statistics */
    } u;
} t_FmPcdManipStats;

#if (DPAA_VERSION >= 11)
/**************************************************************************//**
 @Description   Parameters for defining frame replicator group and its members
*//***************************************************************************/
typedef struct t_FmPcdFrmReplicGroupParams {
    uint8_t                     maxNumOfEntries;    /**< Maximal number of members in the group;
                                                         Must be at least 2. */
    uint8_t                     numOfEntries;       /**< Number of members in the group;
                                                         Must be at least 1. */
    t_FmPcdCcNextEngineParams   nextEngineParams[FM_PCD_FRM_REPLIC_MAX_NUM_OF_ENTRIES];
                                                    /**< Array of members' parameters */
} t_FmPcdFrmReplicGroupParams;
#endif /* (DPAA_VERSION >= 11) */

#if ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT))
/**************************************************************************//**
 @Description   structure for defining statistics node
*//***************************************************************************/
typedef struct t_FmPcdStatsParams {
    e_FmPcdStatsType    type;   /**< type of statistics node */
} t_FmPcdStatsParams;
#endif /* ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT)) */

/**************************************************************************//**
 @Function      FM_PCD_NetEnvCharacteristicsSet

 @Description   Define a set of Network Environment Characteristics.

                When setting an environment it is important to understand its
                application. It is not meant to describe the flows that will run
                on the ports using this environment, but what the user means TO DO
                with the PCD mechanisms in order to parse-classify-distribute those
                frames.
                By specifying a distinction unit, the user means it would use that option
                for distinction between frames at either a KeyGen scheme or a coarse
                classification action descriptor. Using interchangeable headers to define a
                unit means that the user is indifferent to which of the interchangeable
                headers is present in the frame, and wants the distinction to be based
                on the presence of either one of them.

                Depending on context, there are limitations to the use of environments. A
                port using the PCD functionality is bound to an environment. Some or even
                all ports may share an environment but also an environment per port is
                possible. When initializing a scheme, a classification plan group (see below),
                or a coarse classification tree, one of the initialized environments must be
                stated and related to. When a port is bound to a scheme, a classification
                plan group, or a coarse classification tree, it MUST be bound to the same
                environment.

                The different PCD modules, may relate (for flows definition) ONLY on
                distinction units as defined by their environment. When initializing a
                scheme for example, it may not choose to select IPV4 as a match for
                recognizing flows unless it was defined in the relating environment. In
                fact, to guide the user through the configuration of the PCD, each module's
                characterization in terms of flows is not done using protocol names, but using
                environment indexes.

                In terms of HW implementation, the list of distinction units sets the LCV vectors
                and later used for match vector, classification plan vectors and coarse classification
                indexing.

 @Param[in]     h_FmPcd         FM PCD module descriptor.
 @Param[in]     p_NetEnvParams  A structure of parameters for the initialization of
                                the network environment.

 @Return        A handle to the initialized object on success; NULL code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
*//***************************************************************************/
t_Handle FM_PCD_NetEnvCharacteristicsSet(t_Handle h_FmPcd, t_FmPcdNetEnvParams *p_NetEnvParams);

/**************************************************************************//**
 @Function      FM_PCD_NetEnvCharacteristicsDelete

 @Description   Deletes a set of Network Environment Characteristics.

 @Param[in]     h_NetEnv        A handle to the Network environment.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error FM_PCD_NetEnvCharacteristicsDelete(t_Handle h_NetEnv);

/**************************************************************************//**
 @Function      FM_PCD_KgSchemeSet

 @Description   Initializing or modifying and enabling a scheme for the KeyGen.
                This routine should be called for adding or modifying a scheme.
                When a scheme needs modifying, the API requires that it will be
                rewritten. In such a case 'modify' should be TRUE. If the
                routine is called for a valid scheme and 'modify' is FALSE,
                it will return error.

 @Param[in]     h_FmPcd         If this is a new scheme - A handle to an FM PCD Module.
                                Otherwise NULL (ignored by driver).
 @Param[in,out] p_SchemeParams  A structure of parameters for defining the scheme

 @Return        A handle to the initialized scheme on success; NULL code otherwise.
                When used as "modify" (rather than for setting a new scheme),
                p_SchemeParams->id.h_Scheme will return NULL if action fails due to scheme
                BUSY state.

 @Cautions      Allowed only following FM_PCD_Init().
*//***************************************************************************/
t_Handle FM_PCD_KgSchemeSet(t_Handle                h_FmPcd,
                            t_FmPcdKgSchemeParams   *p_SchemeParams);

/**************************************************************************//**
 @Function      FM_PCD_KgSchemeDelete

 @Description   Deleting an initialized scheme.

 @Param[in]     h_Scheme        scheme handle as returned by FM_PCD_KgSchemeSet()

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init() & FM_PCD_KgSchemeSet().
*//***************************************************************************/
t_Error     FM_PCD_KgSchemeDelete(t_Handle h_Scheme);

/**************************************************************************//**
 @Function      FM_PCD_KgSchemeGetCounter

 @Description   Reads scheme packet counter.

 @Param[in]     h_Scheme        scheme handle as returned by FM_PCD_KgSchemeSet().

 @Return        Counter's current value.

 @Cautions      Allowed only following FM_PCD_Init() & FM_PCD_KgSchemeSet().
*//***************************************************************************/
uint32_t  FM_PCD_KgSchemeGetCounter(t_Handle h_Scheme);

/**************************************************************************//**
 @Function      FM_PCD_KgSchemeSetCounter

 @Description   Writes scheme packet counter.

 @Param[in]     h_Scheme        scheme handle as returned by FM_PCD_KgSchemeSet().
 @Param[in]     value           New scheme counter value - typically '0' for
                                resetting the counter.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init() & FM_PCD_KgSchemeSet().
*//***************************************************************************/
t_Error  FM_PCD_KgSchemeSetCounter(t_Handle h_Scheme, uint32_t value);

/**************************************************************************//**
 @Function      FM_PCD_PlcrProfileSet

 @Description   Sets a profile entry in the policer profile table.
                The routine overrides any existing value.

 @Param[in]     h_FmPcd           A handle to an FM PCD Module.
 @Param[in]     p_Profile         A structure of parameters for defining a
                                  policer profile entry.

 @Return        A handle to the initialized object on success; NULL code otherwise.
                When used as "modify" (rather than for setting a new profile),
                p_Profile->id.h_Profile will return NULL if action fails due to profile
                BUSY state.
 @Cautions      Allowed only following FM_PCD_Init().
*//***************************************************************************/
t_Handle FM_PCD_PlcrProfileSet(t_Handle                  h_FmPcd,
                               t_FmPcdPlcrProfileParams  *p_Profile);

/**************************************************************************//**
 @Function      FM_PCD_PlcrProfileDelete

 @Description   Delete a profile entry in the policer profile table.
                The routine set entry to invalid.

 @Param[in]     h_Profile       A handle to the profile.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
*//***************************************************************************/
t_Error FM_PCD_PlcrProfileDelete(t_Handle h_Profile);

/**************************************************************************//**
 @Function      FM_PCD_PlcrProfileGetCounter

 @Description   Sets an entry in the classification plan.
                The routine overrides any existing value.

 @Param[in]     h_Profile       A handle to the profile.
 @Param[in]     counter         Counter selector.

 @Return        specific counter value.

 @Cautions      Allowed only following FM_PCD_Init().
*//***************************************************************************/
uint32_t FM_PCD_PlcrProfileGetCounter(t_Handle                      h_Profile,
                                      e_FmPcdPlcrProfileCounters    counter);

/**************************************************************************//**
 @Function      FM_PCD_PlcrProfileSetCounter

 @Description   Sets an entry in the classification plan.
                The routine overrides any existing value.

 @Param[in]     h_Profile       A handle to the profile.
 @Param[in]     counter         Counter selector.
 @Param[in]     value           value to set counter with.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
*//***************************************************************************/
t_Error FM_PCD_PlcrProfileSetCounter(t_Handle                   h_Profile,
                                     e_FmPcdPlcrProfileCounters counter,
                                     uint32_t                   value);

/**************************************************************************//**
 @Function      FM_PCD_CcRootBuild

 @Description   This routine must be called to define a complete coarse
                classification tree. This is the way to define coarse
                classification to a certain flow - the KeyGen schemes
                may point only to trees defined in this way.

 @Param[in]     h_FmPcd         FM PCD module descriptor.
 @Param[in]     p_Params        A structure of parameters to define the tree.

 @Return        A handle to the initialized object on success; NULL code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
*//***************************************************************************/
t_Handle FM_PCD_CcRootBuild (t_Handle             h_FmPcd,
                             t_FmPcdCcTreeParams  *p_Params);

/**************************************************************************//**
 @Function      FM_PCD_CcRootDelete

 @Description   Deleting an built tree.

 @Param[in]     h_CcTree        A handle to a CC tree.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
*//***************************************************************************/
t_Error FM_PCD_CcRootDelete(t_Handle h_CcTree);

/**************************************************************************//**
 @Function      FM_PCD_CcRootModifyNextEngine

 @Description   Modify the Next Engine Parameters in the entry of the tree.

 @Param[in]     h_CcTree                    A handle to the tree
 @Param[in]     grpId                       A Group index in the tree
 @Param[in]     index                       Entry index in the group defined by grpId
 @Param[in]     p_FmPcdCcNextEngineParams   Pointer to new next engine parameters

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_CcBuildTree().
*//***************************************************************************/
t_Error FM_PCD_CcRootModifyNextEngine(t_Handle                  h_CcTree,
                                      uint8_t                   grpId,
                                      uint8_t                   index,
                                      t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableSet

 @Description   This routine should be called for each CC (coarse classification)
                node. The whole CC tree should be built bottom up so that each
                node points to already defined nodes.

 @Param[in]     h_FmPcd         FM PCD module descriptor.
 @Param[in]     p_Param         A structure of parameters defining the CC node

 @Return        A handle to the initialized object on success; NULL code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
*//***************************************************************************/
t_Handle   FM_PCD_MatchTableSet(t_Handle h_FmPcd, t_FmPcdCcNodeParams *p_Param);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableDelete

 @Description   Deleting an built node.

 @Param[in]     h_CcNode        A handle to a CC node.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
*//***************************************************************************/
t_Error FM_PCD_MatchTableDelete(t_Handle h_CcNode);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableModifyMissNextEngine

 @Description   Modify the Next Engine Parameters of the Miss key case of the node.

 @Param[in]     h_CcNode                    A handle to the node
 @Param[in]     p_FmPcdCcNextEngineParams   Parameters for defining next engine

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_MatchTableSet();
                Not relevant in the case the node is of type 'INDEXED_LOOKUP'.
                When configuring nextEngine = e_FM_PCD_CC, note that
                p_FmPcdCcNextEngineParams->ccParams.h_CcNode must be different
                from the currently changed table.

*//***************************************************************************/
t_Error FM_PCD_MatchTableModifyMissNextEngine(t_Handle                  h_CcNode,
                                              t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableRemoveKey

 @Description   Remove the key (including next engine parameters of this key)
                defined by the index of the relevant node.

 @Param[in]     h_CcNode                    A handle to the node
 @Param[in]     keyIndex                    Key index for removing

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_MatchTableSet() was called for this
                node and the nodes that lead to it.
*//***************************************************************************/
t_Error FM_PCD_MatchTableRemoveKey(t_Handle h_CcNode, uint16_t keyIndex);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableAddKey

 @Description   Add the key (including next engine parameters of this key in the
                index defined by the keyIndex. Note that 'FM_PCD_LAST_KEY_INDEX'
                may be used by user that don't care about the position of the
                key in the table - in that case, the key will be automatically
                added by the driver in the last available entry.

 @Param[in]     h_CcNode     A handle to the node
 @Param[in]     keyIndex     Key index for adding.
 @Param[in]     keySize      Key size of added key
 @Param[in]     p_KeyParams  A pointer to the parameters includes
                             new key with Next Engine Parameters

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_MatchTableSet() was called for this
                node and the nodes that lead to it.
*//***************************************************************************/
t_Error FM_PCD_MatchTableAddKey(t_Handle            h_CcNode,
                                uint16_t            keyIndex,
                                uint8_t             keySize,
                                t_FmPcdCcKeyParams  *p_KeyParams);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableModifyNextEngine

 @Description   Modify the Next Engine Parameters in the relevant key entry of the node.

 @Param[in]     h_CcNode                    A handle to the node
 @Param[in]     keyIndex                    Key index for Next Engine modifications
 @Param[in]     p_FmPcdCcNextEngineParams   Parameters for defining next engine

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_MatchTableSet().
                When configuring nextEngine = e_FM_PCD_CC, note that
                p_FmPcdCcNextEngineParams->ccParams.h_CcNode must be different
                from the currently changed table.

*//***************************************************************************/
t_Error FM_PCD_MatchTableModifyNextEngine(t_Handle                  h_CcNode,
                                          uint16_t                  keyIndex,
                                          t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableModifyKeyAndNextEngine

 @Description   Modify the key and Next Engine Parameters of this key in the
                index defined by the keyIndex.

 @Param[in]     h_CcNode                    A handle to the node
 @Param[in]     keyIndex                    Key index for adding
 @Param[in]     keySize                     Key size of added key
 @Param[in]     p_KeyParams                 A pointer to the parameters includes
                                            modified key and modified Next Engine Parameters

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_MatchTableSet() was called for this
                node and the nodes that lead to it.
                When configuring nextEngine = e_FM_PCD_CC, note that
                p_FmPcdCcNextEngineParams->ccParams.h_CcNode must be different
                from the currently changed table.
*//***************************************************************************/
t_Error FM_PCD_MatchTableModifyKeyAndNextEngine(t_Handle            h_CcNode,
                                                uint16_t            keyIndex,
                                                uint8_t             keySize,
                                                t_FmPcdCcKeyParams  *p_KeyParams);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableModifyKey

 @Description   Modify the key in the index defined by the keyIndex.

 @Param[in]     h_CcNode                    A handle to the node
 @Param[in]     keyIndex                    Key index for adding
 @Param[in]     keySize                     Key size of added key
 @Param[in]     p_Key                       A pointer to the new key
 @Param[in]     p_Mask                      A pointer to the new mask if relevant,
                                            otherwise pointer to NULL

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_MatchTableSet() was called for this
                node and the nodes that lead to it.
*//***************************************************************************/
t_Error FM_PCD_MatchTableModifyKey(t_Handle h_CcNode,
                                   uint16_t keyIndex,
                                   uint8_t  keySize,
                                   uint8_t  *p_Key,
                                   uint8_t  *p_Mask);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableFindNRemoveKey

 @Description   Remove the key (including next engine parameters of this key)
                defined by the key and mask. Note that this routine will search
                the node to locate the index of the required key (& mask) to remove.

 @Param[in]     h_CcNode                    A handle to the node
 @Param[in]     keySize                     Key size of the one to remove.
 @Param[in]     p_Key                       A pointer to the requested key to remove.
 @Param[in]     p_Mask                      A pointer to the mask if relevant,
                                            otherwise pointer to NULL

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_MatchTableSet() was called for this
                node and the nodes that lead to it.
*//***************************************************************************/
t_Error FM_PCD_MatchTableFindNRemoveKey(t_Handle h_CcNode,
                                        uint8_t  keySize,
                                        uint8_t  *p_Key,
                                        uint8_t  *p_Mask);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableFindNModifyNextEngine

 @Description   Modify the Next Engine Parameters in the relevant key entry of
                the node. Note that this routine will search the node to locate
                the index of the required key (& mask) to modify.

 @Param[in]     h_CcNode                    A handle to the node
 @Param[in]     keySize                     Key size of the one to modify.
 @Param[in]     p_Key                       A pointer to the requested key to modify.
 @Param[in]     p_Mask                      A pointer to the mask if relevant,
                                            otherwise pointer to NULL
 @Param[in]     p_FmPcdCcNextEngineParams   Parameters for defining next engine

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_MatchTableSet().
                When configuring nextEngine = e_FM_PCD_CC, note that
                p_FmPcdCcNextEngineParams->ccParams.h_CcNode must be different
                from the currently changed table.
*//***************************************************************************/
t_Error FM_PCD_MatchTableFindNModifyNextEngine(t_Handle                  h_CcNode,
                                               uint8_t                   keySize,
                                               uint8_t                   *p_Key,
                                               uint8_t                   *p_Mask,
                                               t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableFindNModifyKeyAndNextEngine

 @Description   Modify the key and Next Engine Parameters of this key in the
                index defined by the keyIndex. Note that this routine will search
                the node to locate the index of the required key (& mask) to modify.

 @Param[in]     h_CcNode                    A handle to the node
 @Param[in]     keySize                     Key size of the one to modify.
 @Param[in]     p_Key                       A pointer to the requested key to modify.
 @Param[in]     p_Mask                      A pointer to the mask if relevant,
                                            otherwise pointer to NULL
 @Param[in]     p_KeyParams                 A pointer to the parameters includes
                                            modified key and modified Next Engine Parameters

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_MatchTableSet() was called for this
                node and the nodes that lead to it.
                When configuring nextEngine = e_FM_PCD_CC, note that
                p_FmPcdCcNextEngineParams->ccParams.h_CcNode must be different
                from the currently changed table.
*//***************************************************************************/
t_Error FM_PCD_MatchTableFindNModifyKeyAndNextEngine(t_Handle            h_CcNode,
                                                     uint8_t             keySize,
                                                     uint8_t             *p_Key,
                                                     uint8_t             *p_Mask,
                                                     t_FmPcdCcKeyParams  *p_KeyParams);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableFindNModifyKey

 @Description   Modify the key  in the index defined by the keyIndex. Note that
                this routine will search the node to locate the index of the
                required key (& mask) to modify.

 @Param[in]     h_CcNode                    A handle to the node
 @Param[in]     keySize                     Key size of the one to modify.
 @Param[in]     p_Key                       A pointer to the requested key to modify.
 @Param[in]     p_Mask                      A pointer to the mask if relevant,
                                            otherwise pointer to NULL
 @Param[in]     p_NewKey                    A pointer to the new key
 @Param[in]     p_NewMask                   A pointer to the new mask if relevant,
                                            otherwise pointer to NULL

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_MatchTableSet() was called for this
                node and the nodes that lead to it.
*//***************************************************************************/
t_Error FM_PCD_MatchTableFindNModifyKey(t_Handle h_CcNode,
                                        uint8_t  keySize,
                                        uint8_t  *p_Key,
                                        uint8_t  *p_Mask,
                                        uint8_t  *p_NewKey,
                                        uint8_t  *p_NewMask);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableGetKeyCounter

 @Description   This routine may be used to get a counter of specific key in a CC
                Node; This counter reflects how many frames passed that were matched
                this key.

 @Param[in]     h_CcNode        A handle to the node
 @Param[in]     keyIndex        Key index for adding

 @Return        The specific key counter.

 @Cautions      Allowed only following FM_PCD_MatchTableSet().
*//***************************************************************************/
uint32_t FM_PCD_MatchTableGetKeyCounter(t_Handle h_CcNode, uint16_t keyIndex);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableGetKeyStatistics

 @Description   This routine may be used to get statistics counters of specific key
                in a CC Node.

                If 'e_FM_PCD_CC_STATS_MODE_FRAME' and
                'e_FM_PCD_CC_STATS_MODE_BYTE_AND_FRAME' were set for this node,
                these counters reflect how many frames passed that were matched
                this key; The total frames count will be returned in the counter
                of the first range (as only one frame length range was defined).
                If 'e_FM_PCD_CC_STATS_MODE_RMON' was set for this node, the total
                frame count will be separated to frame length counters, based on
                provided frame length ranges.

 @Param[in]     h_CcNode        A handle to the node
 @Param[in]     keyIndex        Key index for adding
 @Param[out]    p_KeyStatistics Key statistics counters

 @Return        The specific key statistics.

 @Cautions      Allowed only following FM_PCD_MatchTableSet().
*//***************************************************************************/
t_Error FM_PCD_MatchTableGetKeyStatistics(t_Handle                  h_CcNode,
                                          uint16_t                  keyIndex,
                                          t_FmPcdCcKeyStatistics    *p_KeyStatistics);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableGetMissStatistics

 @Description   This routine may be used to get statistics counters of miss entry
                in a CC Node.

                If 'e_FM_PCD_CC_STATS_MODE_FRAME' and
                'e_FM_PCD_CC_STATS_MODE_BYTE_AND_FRAME' were set for this node,
                these counters reflect how many frames were not matched to any
                existing key and therefore passed through the miss entry; The
                total frames count will be returned in the counter of the
                first range (as only one frame length range was defined).

 @Param[in]     h_CcNode            A handle to the node
 @Param[out]    p_MissStatistics    Statistics counters for 'miss'

 @Return        The statistics for 'miss'.

 @Cautions      Allowed only following FM_PCD_MatchTableSet().
*//***************************************************************************/
t_Error FM_PCD_MatchTableGetMissStatistics(t_Handle                  h_CcNode,
                                           t_FmPcdCcKeyStatistics    *p_MissStatistics);

/**************************************************************************//**
 @Function      FM_PCD_MatchTableFindNGetKeyStatistics

 @Description   This routine may be used to get statistics counters of specific key
                in a CC Node.

                If 'e_FM_PCD_CC_STATS_MODE_FRAME' and
                'e_FM_PCD_CC_STATS_MODE_BYTE_AND_FRAME' were set for this node,
                these counters reflect how many frames passed that were matched
                this key; The total frames count will be returned in the counter
                of the first range (as only one frame length range was defined).
                If 'e_FM_PCD_CC_STATS_MODE_RMON' was set for this node, the total
                frame count will be separated to frame length counters, based on
                provided frame length ranges.
                Note that this routine will search the node to locate the index
                of the required key based on received key parameters.

 @Param[in]     h_CcNode        A handle to the node
 @Param[in]     keySize         Size of the requested key
 @Param[in]     p_Key           A pointer to the requested key
 @Param[in]     p_Mask          A pointer to the mask if relevant,
                                otherwise pointer to NULL
 @Param[out]    p_KeyStatistics Key statistics counters

 @Return        The specific key statistics.

 @Cautions      Allowed only following FM_PCD_MatchTableSet().
*//***************************************************************************/
t_Error FM_PCD_MatchTableFindNGetKeyStatistics(t_Handle                 h_CcNode,
                                               uint8_t                  keySize,
                                               uint8_t                  *p_Key,
                                               uint8_t                  *p_Mask,
                                               t_FmPcdCcKeyStatistics   *p_KeyStatistics);

/**************************************************************************//*
 @Function      FM_PCD_MatchTableGetNextEngine

 @Description   Gets NextEngine of the relevant keyIndex.

 @Param[in]     h_CcNode                    A handle to the node.
 @Param[in]     keyIndex                    keyIndex in the relevant node.
 @Param[out]    p_FmPcdCcNextEngineParams   here updated nextEngine parameters for
                                            the relevant keyIndex of the CC Node
                                            received as parameter to this function

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
*//***************************************************************************/
t_Error FM_PCD_MatchTableGetNextEngine(t_Handle                     h_CcNode,
                                       uint16_t                     keyIndex,
                                       t_FmPcdCcNextEngineParams    *p_FmPcdCcNextEngineParams);

/**************************************************************************//*
 @Function      FM_PCD_MatchTableGetIndexedHashBucket

 @Description   This routine simulates KeyGen operation on the provided key and
                calculates to which hash bucket it will be mapped.

 @Param[in]     h_CcNode                A handle to the node.
 @Param[in]     kgKeySize               Key size as it was configured in the KG
                                        scheme that leads to this hash.
 @Param[in]     p_KgKey                 Pointer to the key; must be like the key
                                        that the KG is generated, i.e. the same
                                        extraction and with mask if exist.
 @Param[in]     kgHashShift             Hash-shift as it was configured in the KG
                                        scheme that leads to this hash.
 @Param[out]    p_CcNodeBucketHandle    Pointer to the bucket of the provided key.
 @Param[out]    p_BucketIndex           Index to the bucket of the provided key
 @Param[out]    p_LastIndex             Pointer to last index in the bucket of the
                                        provided key.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_HashTableSet()
*//***************************************************************************/
t_Error FM_PCD_MatchTableGetIndexedHashBucket(t_Handle    h_CcNode,
                                              uint8_t     kgKeySize,
                                              uint8_t     *p_KgKey,
                                              uint8_t     kgHashShift,
                                              t_Handle    *p_CcNodeBucketHandle,
                                              uint8_t     *p_BucketIndex,
                                              uint16_t    *p_LastIndex);

/**************************************************************************//**
 @Function      FM_PCD_HashTableSet

 @Description   This routine initializes a hash table structure.
                KeyGen hash result determines the hash bucket.
                Next, KeyGen key is compared against all keys of this
                bucket (exact match).
                Number of sets (number of buckets) of the hash equals to the
                number of 1-s in 'hashResMask' in the provided parameters.
                Number of hash table ways is then calculated by dividing
                'maxNumOfKeys' equally between the hash sets. This is the maximal
                number of keys that a hash bucket may hold.
                The hash table is initialized empty and keys may be
                added to it following the initialization. Keys masks are not
                supported in current hash table implementation.
                The initialized hash table can be integrated as a node in a
                CC tree.

 @Param[in]     h_FmPcd     FM PCD module descriptor.
 @Param[in]     p_Param     A structure of parameters defining the hash table

 @Return        A handle to the initialized object on success; NULL code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
*//***************************************************************************/
t_Handle FM_PCD_HashTableSet(t_Handle h_FmPcd, t_FmPcdHashTableParams *p_Param);

/**************************************************************************//**
 @Function      FM_PCD_HashTableDelete

 @Description   This routine deletes the provided hash table and released all
                its allocated resources.

 @Param[in]     h_HashTbl       A handle to a hash table

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_HashTableSet().
*//***************************************************************************/
t_Error FM_PCD_HashTableDelete(t_Handle h_HashTbl);

/**************************************************************************//**
 @Function      FM_PCD_HashTableAddKey

 @Description   This routine adds the provided key (including next engine
                parameters of this key) to the hash table.
                The key is added as the last key of the bucket that it is
                mapped to.

 @Param[in]     h_HashTbl    A handle to a hash table
 @Param[in]     keySize      Key size of added key
 @Param[in]     p_KeyParams  A pointer to the parameters includes
                             new key with next engine parameters; The pointer
                             to the key mask must be NULL, as masks are not
                             supported in hash table implementation.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_HashTableSet().
*//***************************************************************************/
t_Error FM_PCD_HashTableAddKey(t_Handle            h_HashTbl,
                               uint8_t             keySize,
                               t_FmPcdCcKeyParams  *p_KeyParams);

/**************************************************************************//**
 @Function      FM_PCD_HashTableRemoveKey

 @Description   This routine removes the requested key (including next engine
                parameters of this key) from the hash table.

 @Param[in]     h_HashTbl    A handle to a hash table
 @Param[in]     keySize      Key size of the one to remove.
 @Param[in]     p_Key        A pointer to the requested key to remove.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_HashTableSet().
*//***************************************************************************/
t_Error FM_PCD_HashTableRemoveKey(t_Handle h_HashTbl,
                                  uint8_t  keySize,
                                  uint8_t  *p_Key);

/**************************************************************************//**
 @Function      FM_PCD_HashTableModifyNextEngine

 @Description   This routine modifies the next engine for the provided key. The
                key should be previously added to the hash table.

 @Param[in]     h_HashTbl                   A handle to a hash table
 @Param[in]     keySize                     Key size of the key to modify.
 @Param[in]     p_Key                       A pointer to the requested key to modify.
 @Param[in]     p_FmPcdCcNextEngineParams   A structure for defining new next engine
                                            parameters.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_HashTableSet().
                When configuring nextEngine = e_FM_PCD_CC, note that
                p_FmPcdCcNextEngineParams->ccParams.h_CcNode must be different
                from the currently changed table.
*//***************************************************************************/
t_Error FM_PCD_HashTableModifyNextEngine(t_Handle                  h_HashTbl,
                                         uint8_t                   keySize,
                                         uint8_t                   *p_Key,
                                         t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams);

/**************************************************************************//**
 @Function      FM_PCD_HashTableModifyMissNextEngine

 @Description   This routine modifies the next engine on key match miss.

 @Param[in]     h_HashTbl                   A handle to a hash table
 @Param[in]     p_FmPcdCcNextEngineParams   A structure for defining new next engine
                                            parameters.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_HashTableSet().
                When configuring nextEngine = e_FM_PCD_CC, note that
                p_FmPcdCcNextEngineParams->ccParams.h_CcNode must be different
                from the currently changed table.
*//***************************************************************************/
t_Error FM_PCD_HashTableModifyMissNextEngine(t_Handle                  h_HashTbl,
                                             t_FmPcdCcNextEngineParams *p_FmPcdCcNextEngineParams);

/**************************************************************************//*
 @Function      FM_PCD_HashTableGetMissNextEngine

 @Description   Gets NextEngine in case of key match miss.

 @Param[in]     h_HashTbl                   A handle to a hash table
 @Param[out]    p_FmPcdCcNextEngineParams   Next engine parameters for the specified
                                            hash table.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_HashTableSet().
*//***************************************************************************/
t_Error FM_PCD_HashTableGetMissNextEngine(t_Handle                     h_HashTbl,
                                          t_FmPcdCcNextEngineParams    *p_FmPcdCcNextEngineParams);

/**************************************************************************//**
 @Function      FM_PCD_HashTableFindNGetKeyStatistics

 @Description   This routine may be used to get statistics counters of specific key
                in a hash table.

                If 'e_FM_PCD_CC_STATS_MODE_FRAME' and
                'e_FM_PCD_CC_STATS_MODE_BYTE_AND_FRAME' were set for this node,
                these counters reflect how many frames passed that were matched
                this key; The total frames count will be returned in the counter
                of the first range (as only one frame length range was defined).
                If 'e_FM_PCD_CC_STATS_MODE_RMON' was set for this node, the total
                frame count will be separated to frame length counters, based on
                provided frame length ranges.
                Note that this routine will identify the bucket of this key in
                the hash table and will search the bucket to locate the index
                of the required key based on received key parameters.

 @Param[in]     h_HashTbl       A handle to a hash table
 @Param[in]     keySize         Size of the requested key
 @Param[in]     p_Key           A pointer to the requested key
 @Param[out]    p_KeyStatistics Key statistics counters

 @Return        The specific key statistics.

 @Cautions      Allowed only following FM_PCD_HashTableSet().
*//***************************************************************************/
t_Error FM_PCD_HashTableFindNGetKeyStatistics(t_Handle                 h_HashTbl,
                                              uint8_t                  keySize,
                                              uint8_t                  *p_Key,
                                              t_FmPcdCcKeyStatistics   *p_KeyStatistics);

/**************************************************************************//**
 @Function      FM_PCD_HashTableGetMissStatistics

 @Description   This routine may be used to get statistics counters of 'miss'
                entry of the a hash table.

                If 'e_FM_PCD_CC_STATS_MODE_FRAME' and
                'e_FM_PCD_CC_STATS_MODE_BYTE_AND_FRAME' were set for this node,
                these counters reflect how many frames were not matched to any
                existing key and therefore passed through the miss entry;

 @Param[in]     h_HashTbl           A handle to a hash table
 @Param[out]    p_MissStatistics    Statistics counters for 'miss'

 @Return        The statistics for 'miss'.

 @Cautions      Allowed only following FM_PCD_HashTableSet().
*//***************************************************************************/
t_Error FM_PCD_HashTableGetMissStatistics(t_Handle                 h_HashTbl,
                                          t_FmPcdCcKeyStatistics   *p_MissStatistics);

/**************************************************************************//**
 @Function      FM_PCD_ManipNodeSet

 @Description   This routine should be called for defining a manipulation
                node. A manipulation node must be defined before the CC node
                that precedes it.

 @Param[in]     h_FmPcd             FM PCD module descriptor.
 @Param[in]     p_FmPcdManipParams  A structure of parameters defining the manipulation

 @Return        A handle to the initialized object on success; NULL code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
*//***************************************************************************/
t_Handle FM_PCD_ManipNodeSet(t_Handle h_FmPcd, t_FmPcdManipParams *p_FmPcdManipParams);

/**************************************************************************//**
 @Function      FM_PCD_ManipNodeDelete

 @Description   Delete an existing manipulation node.

 @Param[in]     h_ManipNode     A handle to a manipulation node.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_ManipNodeSet().
*//***************************************************************************/
t_Error  FM_PCD_ManipNodeDelete(t_Handle h_ManipNode);

/**************************************************************************//**
 @Function      FM_PCD_ManipGetStatistics

 @Description   Retrieve the manipulation statistics.

 @Param[in]     h_ManipNode         A handle to a manipulation node.
 @Param[out]    p_FmPcdManipStats   A structure for retrieving the manipulation statistics

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_ManipNodeSet().
*//***************************************************************************/
t_Error FM_PCD_ManipGetStatistics(t_Handle h_ManipNode, t_FmPcdManipStats *p_FmPcdManipStats);

/**************************************************************************//**
 @Function      FM_PCD_ManipNodeReplace

 @Description   Change existing manipulation node to be according to new requirement.

 @Param[in]     h_ManipNode         A handle to a manipulation node.
 @Param[out]    p_ManipParams       A structure of parameters defining the change requirement

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_ManipNodeSet().
*//***************************************************************************/
t_Error FM_PCD_ManipNodeReplace(t_Handle h_ManipNode, t_FmPcdManipParams *p_ManipParams);

#if (DPAA_VERSION >= 11)
/**************************************************************************//**
 @Function      FM_PCD_FrmReplicSetGroup

 @Description   Initialize a Frame Replicator group.

 @Param[in]     h_FmPcd                FM PCD module descriptor.
 @Param[in]     p_FrmReplicGroupParam  A structure of parameters for the initialization of
                                       the frame replicator group.

 @Return        A handle to the initialized object on success; NULL code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
*//***************************************************************************/
t_Handle FM_PCD_FrmReplicSetGroup(t_Handle h_FmPcd, t_FmPcdFrmReplicGroupParams *p_FrmReplicGroupParam);

/**************************************************************************//**
 @Function      FM_PCD_FrmReplicDeleteGroup

 @Description   Delete a Frame Replicator group.

 @Param[in]     h_FrmReplicGroup  A handle to the frame replicator group.

 @Return        E_OK on success;  Error code otherwise.

 @Cautions      Allowed only following FM_PCD_FrmReplicSetGroup().
*//***************************************************************************/
t_Error FM_PCD_FrmReplicDeleteGroup(t_Handle h_FrmReplicGroup);

/**************************************************************************//**
 @Function      FM_PCD_FrmReplicAddMember

 @Description   Add the member in the index defined by the memberIndex.

 @Param[in]     h_FrmReplicGroup   A handle to the frame replicator group.
 @Param[in]     memberIndex        member index for adding.
 @Param[in]     p_MemberParams     A pointer to the new member parameters.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_FrmReplicSetGroup() of this group.
*//***************************************************************************/
t_Error FM_PCD_FrmReplicAddMember(t_Handle                   h_FrmReplicGroup,
                                  uint16_t                   memberIndex,
                                  t_FmPcdCcNextEngineParams *p_MemberParams);

/**************************************************************************//**
 @Function      FM_PCD_FrmReplicRemoveMember

 @Description   Remove the member defined by the index from the relevant group.

 @Param[in]     h_FrmReplicGroup   A handle to the frame replicator group.
 @Param[in]     memberIndex        member index for removing.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following FM_PCD_FrmReplicSetGroup() of this group.
*//***************************************************************************/
t_Error FM_PCD_FrmReplicRemoveMember(t_Handle h_FrmReplicGroup,
                                     uint16_t memberIndex);
#endif /* (DPAA_VERSION >= 11) */

#if ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT))
/**************************************************************************//**
 @Function      FM_PCD_StatisticsSetNode

 @Description   This routine should be called for defining a statistics node.

 @Param[in]     h_FmPcd             FM PCD module descriptor.
 @Param[in]     p_FmPcdstatsParams  A structure of parameters defining the statistics

 @Return        A handle to the initialized object on success; NULL code otherwise.

 @Cautions      Allowed only following FM_PCD_Init().
*//***************************************************************************/
t_Handle FM_PCD_StatisticsSetNode(t_Handle h_FmPcd, t_FmPcdStatsParams *p_FmPcdstatsParams);
#endif /* ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT)) */

/** @} */ /* end of FM_PCD_Runtime_build_grp group */
/** @} */ /* end of FM_PCD_Runtime_grp group */
/** @} */ /* end of FM_PCD_grp group */
/** @} */ /* end of FM_grp group */


#ifdef NCSW_BACKWARD_COMPATIBLE_API
#define FM_PCD_MAX_NUM_OF_INTERCHANGABLE_HDRS   FM_PCD_MAX_NUM_OF_INTERCHANGEABLE_HDRS
#define e_FM_PCD_MANIP_ONE_WAYS_HASH            e_FM_PCD_MANIP_ONE_WAY_HASH
#define e_FM_PCD_MANIP_TOW_WAYS_HASH            e_FM_PCD_MANIP_TWO_WAYS_HASH

#define e_FM_PCD_MANIP_FRAGMENT_PACKECT         e_FM_PCD_MANIP_FRAGMENT_PACKET /* Feb13 */

#define FM_PCD_SetNetEnvCharacteristics(_pcd, _params)  \
    FM_PCD_NetEnvCharacteristicsSet(_pcd, _params)
#define FM_PCD_KgSetScheme(_pcd, _params)       FM_PCD_KgSchemeSet(_pcd, _params)
#define FM_PCD_CcBuildTree(_pcd, _params)       FM_PCD_CcRootBuild(_pcd, _params)
#define FM_PCD_CcSetNode(_pcd, _params)         FM_PCD_MatchTableSet(_pcd, _params)
#define FM_PCD_PlcrSetProfile(_pcd, _params)    FM_PCD_PlcrProfileSet(_pcd, _params)
#define FM_PCD_ManipSetNode(_pcd, _params)      FM_PCD_ManipNodeSet(_pcd, _params)

#define FM_PCD_DeleteNetEnvCharacteristics(_pcd, ...)   \
    FM_PCD_NetEnvCharacteristicsDelete(__VA_ARGS__)
#define FM_PCD_KgDeleteScheme(_pcd, ...)   \
    FM_PCD_KgSchemeDelete(__VA_ARGS__)
#define FM_PCD_KgGetSchemeCounter(_pcd, ...)   \
    FM_PCD_KgSchemeGetCounter(__VA_ARGS__)
#define FM_PCD_KgSetSchemeCounter(_pcd, ...)   \
    FM_PCD_KgSchemeSetCounter(__VA_ARGS__)
#define FM_PCD_PlcrDeleteProfile(_pcd, ...)   \
    FM_PCD_PlcrProfileDelete(__VA_ARGS__)
#define FM_PCD_PlcrGetProfileCounter(_pcd, ...)   \
    FM_PCD_PlcrProfileGetCounter(__VA_ARGS__)
#define FM_PCD_PlcrSetProfileCounter(_pcd, ...)   \
    FM_PCD_PlcrProfileSetCounter(__VA_ARGS__)
#define FM_PCD_CcDeleteTree(_pcd, ...)   \
    FM_PCD_CcRootDelete(__VA_ARGS__)
#define FM_PCD_CcTreeModifyNextEngine(_pcd, ...)   \
    FM_PCD_CcRootModifyNextEngine(__VA_ARGS__)
#define FM_PCD_CcDeleteNode(_pcd, ...)   \
    FM_PCD_MatchTableDelete(__VA_ARGS__)
#define FM_PCD_CcNodeModifyMissNextEngine(_pcd, ...)   \
    FM_PCD_MatchTableModifyMissNextEngine(__VA_ARGS__)
#define FM_PCD_CcNodeRemoveKey(_pcd, ...)   \
    FM_PCD_MatchTableRemoveKey(__VA_ARGS__)
#define FM_PCD_CcNodeAddKey(_pcd, ...)   \
    FM_PCD_MatchTableAddKey(__VA_ARGS__)
#define FM_PCD_CcNodeModifyNextEngine(_pcd, ...)   \
    FM_PCD_MatchTableModifyNextEngine(__VA_ARGS__)
#define FM_PCD_CcNodeModifyKeyAndNextEngine(_pcd, ...)   \
    FM_PCD_MatchTableModifyKeyAndNextEngine(__VA_ARGS__)
#define FM_PCD_CcNodeModifyKey(_pcd, ...)   \
    FM_PCD_MatchTableModifyKey(__VA_ARGS__)
#define FM_PCD_CcNodeFindNRemoveKey(_pcd, ...)   \
    FM_PCD_MatchTableFindNRemoveKey(__VA_ARGS__)
#define FM_PCD_CcNodeFindNModifyNextEngine(_pcd, ...)   \
    FM_PCD_MatchTableFindNModifyNextEngine(__VA_ARGS__)
#define FM_PCD_CcNodeFindNModifyKeyAndNextEngine(_pcd, ...) \
    FM_PCD_MatchTableFindNModifyKeyAndNextEngine(__VA_ARGS__)
#define FM_PCD_CcNodeFindNModifyKey(_pcd, ...)   \
    FM_PCD_MatchTableFindNModifyKey(__VA_ARGS__)
#define FM_PCD_CcIndexedHashNodeGetBucket(_pcd, ...)   \
    FM_PCD_MatchTableGetIndexedHashBucket(__VA_ARGS__)
#define FM_PCD_CcNodeGetNextEngine(_pcd, ...)   \
    FM_PCD_MatchTableGetNextEngine(__VA_ARGS__)
#define FM_PCD_CcNodeGetKeyCounter(_pcd, ...)   \
    FM_PCD_MatchTableGetKeyCounter(__VA_ARGS__)
#define FM_PCD_ManipDeleteNode(_pcd, ...)   \
    FM_PCD_ManipNodeDelete(__VA_ARGS__)
#endif /* NCSW_BACKWARD_COMPATIBLE_API */


#endif /* __FM_PCD_EXT */
