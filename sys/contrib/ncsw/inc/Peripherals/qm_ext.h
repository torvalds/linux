/******************************************************************************

 © 1995-2003, 2004, 2005-2011 Freescale Semiconductor, Inc.
 All rights reserved.

 This is proprietary source code of Freescale Semiconductor Inc.,
 and its use is subject to the NetComm Device Drivers EULA.
 The copyright notice above does not evidence any actual or intended
 publication of such source code.

 ALTERNATIVELY, redistribution and use in source and binary forms, with
 or without modification, are permitted provided that the following
 conditions are met:
     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of Freescale Semiconductor nor the
       names of its contributors may be used to endorse or promote products
       derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *

 **************************************************************************/
/******************************************************************************
 @File          qm_ext.h

 @Description   QM & Portal API
*//***************************************************************************/
#ifndef __QM_EXT_H
#define __QM_EXT_H

#include "error_ext.h"
#include "std_ext.h"
#include "dpaa_ext.h"
#include "part_ext.h"


/**************************************************************************//**
 @Group         QM_grp Queue Manager API

 @Description   QM API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   This callback type is used when receiving frame.

                User provides this function. Driver invokes it.

 @Param[in]     h_App       A user argument to the callback
 @Param[in]     h_QmFqr     A handle to an QM-FQR Module.
 @Param[in]     fqidOffset  fqid offset from the FQR's fqid base.
 @Param[in]     p_Frame     The Received Frame

 @Retval        e_RX_STORE_RESPONSE_CONTINUE - order the driver to continue Rx
                                               operation for all ready data.
 @Retval        e_RX_STORE_RESPONSE_PAUSE    - order the driver to stop Rx operation.

 @Cautions      p_Frame is local parameter; i.e. users must NOT access or use
                this parameter in any means outside this callback context.
*//***************************************************************************/
typedef e_RxStoreResponse (t_QmReceivedFrameCallback)(t_Handle h_App,
                                                      t_Handle h_QmFqr,
                                                      t_Handle h_QmPortal,
                                                      uint32_t fqidOffset,
                                                      t_DpaaFD *p_Frame);

/**************************************************************************//**
 @Description   This callback type is used when the FQR is completely was drained.

                User provides this function. Driver invokes it.

 @Param[in]     h_App       A user argument to the callback
 @Param[in]     h_QmFqr     A handle to an QM-FQR Module.

 @Retval        E_OK on success; Error code otherwise.
*//***************************************************************************/
typedef t_Error (t_QmFqrDrainedCompletionCB)(t_Handle h_App,
                                             t_Handle h_QmFqr);

/**************************************************************************//**
 @Description   QM Rejection code enum
*//***************************************************************************/
typedef enum e_QmRejectionCode
{
    e_QM_RC_NONE,

    e_QM_RC_CG_TAILDROP,    /**< This frames was rejected due to congestion
                                     group taildrop situation */
    e_QM_RC_CG_WRED,            /**< This frames was rejected due to congestion
                                     group WRED situation */
    e_QM_RC_FQ_TAILDROP         /**< This frames was rejected due to FQID TD
                                     situation */
/*  e_QM_RC_ERROR
    e_QM_RC_ORPWINDOW_EARLY
    e_QM_RC_ORPWINDOW_LATE
    e_QM_RC_ORPWINDOW_RETIRED */
} e_QmRejectionCode;

/**************************************************************************//**
 @Description   QM Rejected frame information
*//***************************************************************************/
typedef struct t_QmRejectedFrameInfo
{
    e_QmRejectionCode    rejectionCode; /**< Rejection code */
    union
    {
        struct
        {
            uint8_t cgId;               /**< congestion group id*/
        } cg;                           /**< rejection parameters when rejectionCode =
                                             e_QM_RC_CG_TAILDROP or e_QM_RC_CG_WRED. */
    };
} t_QmRejectedFrameInfo;

/**************************************************************************//**
 @Description   This callback type is used when receiving rejected frames.

                User provides this function. Driver invokes it.

 @Param[in]     h_App                   A user argument to the callback
 @Param[in]     h_QmFqr                 A handle to an QM-FQR Module.
 @Param[in]     fqidOffset              fqid offset from the FQR's fqid base.
 @Param[in]     p_Frame                 The Rejected Frame
 @Param[in]     p_QmRejectedFrameInfo   Rejected Frame information

 @Retval        e_RX_STORE_RESPONSE_CONTINUE - order the driver to continue Rx
                                               operation for all ready data.
 @Retval        e_RX_STORE_RESPONSE_PAUSE    - order the driver to stop Rx operation.

 @Cautions      p_Frame is local parameter; i.e. users must NOT access or use
                this parameter in any means outside this callback context.
*//***************************************************************************/
typedef e_RxStoreResponse (t_QmRejectedFrameCallback)(t_Handle h_App,
                                                      t_Handle h_QmFqr,
                                                      t_Handle h_QmPortal,
                                                      uint32_t fqidOffset,
                                                      t_DpaaFD *p_Frame,
                                                      t_QmRejectedFrameInfo *p_QmRejectedFrameInfo);



/**************************************************************************//**
 @Group         QM_lib_grp QM common API

 @Description   QM common API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   QM Exceptions
*//***************************************************************************/
typedef enum e_QmExceptions {
    e_QM_EX_CORENET_INITIATOR_DATA = 0,         /**< Initiator Data Error */
    e_QM_EX_CORENET_TARGET_DATA,                /**< CoreNet Target Data Error */
    e_QM_EX_CORENET_INVALID_TARGET_TRANSACTION, /**< Invalid Target Transaction */
    e_QM_EX_PFDR_THRESHOLD,                     /**< PFDR Low Watermark Interrupt */
    e_QM_EX_PFDR_ENQUEUE_BLOCKED,               /**< PFDR Enqueues Blocked Interrupt */
    e_QM_EX_SINGLE_ECC,                         /**< Single Bit ECC Error Interrupt */
    e_QM_EX_MULTI_ECC,                          /**< Multi Bit ECC Error Interrupt */
    e_QM_EX_INVALID_COMMAND,                    /**< Invalid Command Verb Interrupt */
    e_QM_EX_DEQUEUE_DCP,                        /**< Invalid Dequeue Direct Connect Portal Interrupt */
    e_QM_EX_DEQUEUE_FQ,                         /**< Invalid Dequeue FQ Interrupt */
    e_QM_EX_DEQUEUE_SOURCE,                     /**< Invalid Dequeue Source Interrupt */
    e_QM_EX_DEQUEUE_QUEUE,                      /**< Invalid Dequeue Queue Interrupt */
    e_QM_EX_ENQUEUE_OVERFLOW,                   /**< Invalid Enqueue Overflow Interrupt */
    e_QM_EX_ENQUEUE_STATE,                      /**< Invalid Enqueue State Interrupt */
    e_QM_EX_ENQUEUE_CHANNEL,                    /**< Invalid Enqueue Channel Interrupt */
    e_QM_EX_ENQUEUE_QUEUE,                      /**< Invalid Enqueue Queue Interrupt */
    e_QM_EX_CG_STATE_CHANGE                     /**< CG change state notification */
} e_QmExceptions;

/**************************************************************************//**
 @Group         QM_init_grp QM (common) Initialization Unit

 @Description   QM (common) Initialization Unit

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      t_QmExceptionsCallback

 @Description   Exceptions user callback routine, will be called upon an
                exception passing the exception identification.

 @Param[in]     h_App      - User's application descriptor.
 @Param[in]     exception  - The exception.
*//***************************************************************************/
typedef void (t_QmExceptionsCallback) ( t_Handle              h_App,
                                        e_QmExceptions        exception);

/**************************************************************************//**
 @Description    Frame's Type to poll
*//***************************************************************************/
typedef enum e_QmPortalPollSource {
    e_QM_PORTAL_POLL_SOURCE_DATA_FRAMES = 0,    /**< Poll only data frames */
    e_QM_PORTAL_POLL_SOURCE_CONTROL_FRAMES,     /**< Poll only control frames */
    e_QM_PORTAL_POLL_SOURCE_BOTH                /**< Poll both */
} e_QmPortalPollSource;

/**************************************************************************//**
 @Description   structure representing QM contextA of FQ initialization parameters
                Note that this is only "space-holder" for the Context-A. The "real"
                Context-A is described in each specific driver (E.g. FM driver
                has its own Context-A API).
*//***************************************************************************/
typedef struct {
    uint32_t    res[2];     /**< reserved size for context-a */
} t_QmContextA;

/**************************************************************************//**
 @Description   structure representing QM contextB of FQ initialization parameters
                Note that this is only "space-holder" for the Context-B. The "real"
                Context-B is described in each specific driver (E.g. FM driver
                has its own Context-B API).
*//***************************************************************************/
typedef  uint32_t   t_QmContextB;

/**************************************************************************//**
 @Description   structure representing QM initialization parameters
*//***************************************************************************/
typedef struct {
    uint8_t                 guestId;                /**< QM Partition Id */

    uintptr_t               baseAddress;            /**< Qm base address (virtual)
                                                         NOTE: this parameter relevant only for BM in master mode ('guestId'=NCSW_MASTER_ID). */
    uintptr_t               swPortalsBaseAddress;   /**< QM Software Portals Base Address (virtual) */
    uint16_t                liodn;                  /**< This value is attached to every transaction initiated by QMan when accessing its private data structures */
    uint32_t                totalNumOfFqids;        /**< Total number of frame-queue-ids in the system */
    uint32_t                fqdMemPartitionId;      /**< FQD's mem partition id;
                                                         NOTE: The memory partition must be non-cacheable and no-coherent area. */
    uint32_t                pfdrMemPartitionId;     /**< PFDR's mem partition id;
                                                         NOTE: The memory partition must be non-cacheable and no-coherent area. */
    t_QmExceptionsCallback  *f_Exception;           /**< An application callback routine to handle exceptions.*/
    t_Handle                h_App;                  /**< A handle to an application layer object; This handle will
                                                         be passed by the driver upon calling the above callbacks */
    uintptr_t               errIrq;                 /**< error interrupt line; NO_IRQ if interrupts not used */
    uint32_t                partFqidBase;           /**< The first frame-queue-id dedicated to this partition.
                                                         NOTE: this parameter relevant only when working with multiple partitions. */
    uint32_t                partNumOfFqids;         /**< Number of frame-queue-ids dedicated to this partition.
                                                         NOTE: this parameter relevant only when working with multiple partitions. */
    uint16_t                partCgsBase;            /**< The first cgr dedicated to this partition.
                                                         NOTE: this parameter relevant only when working with multiple partitions. */
    uint16_t                partNumOfCgs;           /**< Number of cgr's dedicated to this partition.
                                                         NOTE: this parameter relevant only when working with multiple partitions. */
} t_QmParam;


/**************************************************************************//**
 @Function      QM_Config

 @Description   Creates descriptor for the QM module.

                The routine returns a handle (descriptor) to the QM object.
                This descriptor must be passed as first parameter to all other
                QM function calls.

                No actual initialization or configuration of QM hardware is
                done by this routine.

 @Param[in]     p_QmParam   - Pointer to data structure of parameters

 @Retval        Handle to the QM object, or NULL for Failure.
*//***************************************************************************/
t_Handle QM_Config(t_QmParam *p_QmParam);

/**************************************************************************//**
 @Function      QM_Init

 @Description   Initializes the QM module

 @Param[in]     h_Qm - A handle to the QM module

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error QM_Init(t_Handle h_Qm);

/**************************************************************************//**
 @Function      QM_Free

 @Description   Frees all resources that were assigned to the QM module.

                Calling this routine invalidates the descriptor.

 @Param[in]     h_Qm - A handle to the QM module

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error QM_Free(t_Handle h_Qm);


/**************************************************************************//**
 @Group         QM_advanced_init_grp    QM (common) Advanced Configuration Unit

 @Description   Configuration functions used to change default values.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   structure for defining DC portal ERN destination
*//***************************************************************************/
typedef struct t_QmDcPortalParams {
    bool            sendToSw;
    e_DpaaSwPortal  swPortalId;
} t_QmDcPortalParams;


/**************************************************************************//**
 @Function      QM_ConfigRTFramesDepth

 @Description   Change the run-time frames depth (i.e. the maximum total number
                of frames that may be inside QM at a certain time) from its default
                configuration [30000].

 @Param[in]     h_Qm            - A handle to the QM module
 @Param[in]     rtFramesDepth   - run-time max num of frames.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_Config() and before QM_Init().
*//***************************************************************************/
t_Error QM_ConfigRTFramesDepth(t_Handle h_Qm, uint32_t rtFramesDepth);

/**************************************************************************//**
 @Function      QM_ConfigPfdrThreshold

 @Description   Change the pfdr threshold from its default
                configuration [0].
                An interrupt if enables is asserted when the number of PFDRs is below this threshold.

 @Param[in]     h_Qm            - A handle to the QM module
 @Param[in]     threshold       - threshold value.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_Config() and before QM_Init().
*//***************************************************************************/
t_Error QM_ConfigPfdrThreshold(t_Handle h_Qm, uint32_t threshold);

/**************************************************************************//**
 @Function      QM_ConfigSfdrReservationThreshold

 @Description   Change the sfdr threshold from its default
                configuration [0].

 @Param[in]     h_Qm            - A handle to the QM module
 @Param[in]     threshold       - threshold value.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_Config() and before QM_Init().
*//***************************************************************************/
t_Error QM_ConfigSfdrReservationThreshold(t_Handle h_Qm, uint32_t threshold);

/**************************************************************************//**
 @Function      QM_ConfigErrorRejectionNotificationDest

 @Description   Change the destination of rejected frames for DC portals.
                By default, depending on chip, some DC portals are set to reject
                frames to HW and some to SW.

 @Param[in]     h_Qm            - A handle to the QM module
 @Param[in]     id              - DC Portal id.
 @Param[in]     p_Params        - Destination parameters.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_Config() and before QM_Init().
*//***************************************************************************/
t_Error QM_ConfigErrorRejectionNotificationDest(t_Handle h_Qm, e_DpaaDcPortal id, t_QmDcPortalParams *p_Params);

/** @} */ /* end of QM_advanced_init_grp group */
/** @} */ /* end of QM_init_grp group */


/**************************************************************************//**
 @Group         QM_runtime_control_grp QM (common) Runtime Control Unit

 @Description   QM (common) Runtime control unit API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   enum for defining QM counters
*//***************************************************************************/
typedef enum e_QmCounters {
    e_QM_COUNTERS_SFDR_IN_USE = 0,          /**< Total Single Frame Descriptor Record (SFDR) currently in use */
    e_QM_COUNTERS_PFDR_IN_USE,              /**< Total Packed Frame Descriptor Record (PFDR) currently in use */
    e_QM_COUNTERS_PFDR_FREE_POOL            /**< Total Packed Frame Descriptor Record (PFDR) Free Pool Count in external memory */
} e_QmCounters;

/**************************************************************************//**
 @Description   structure for returning revision information
*//***************************************************************************/
typedef struct t_QmRevisionInfo {
    uint8_t         majorRev;               /**< Major revision */
    uint8_t         minorRev;               /**< Minor revision */
} t_QmRevisionInfo;

/**************************************************************************//**
 @Description   structure representing QM FQ-Range reservation parameters
*//***************************************************************************/
typedef struct t_QmRsrvFqrParams {
    bool                useForce;       /**< TRUE - force reservation of specific fqids;
                                             FALSE - reserve several fqids */
    uint32_t            numOfFqids;     /**< number of fqids to be reserved. */
    union{
        struct {
            uint32_t    align;          /**< alignment. will be used if useForce=FALSE */
        } nonFrcQs;
        struct {
            uint32_t    fqid;           /**< the fqid base of the forced fqids. will be used if useForce=TRUE */
        } frcQ;
    } qs;
} t_QmRsrvFqrParams;

/**************************************************************************//**
 @Description   structure representing QM Error information
*//***************************************************************************/
typedef struct t_QmErrorInfo {
    bool                portalValid;
    bool                hwPortal;
    e_DpaaSwPortal      swPortalId;         /**< Sw Portal id */
    e_DpaaDcPortal      dcpId;              /**< Dcp (hw Portal) id */
    bool                fqidValid;
    uint32_t            fqid;
} t_QmErrorInfo;


/**************************************************************************//**
 @Function      QM_ReserveQueues

 @Description   Request to Reserved queues for future use.

 @Param[in]     h_Qm            - A handle to the QM Module.
 @Param[in]     p_QmFqrParams   - A structure of parameters for defining the
                                  desired queues parameters.
 @Param[out]    p_BaseFqid      - base-fqid on success; '0' code otherwise.

 @Return        E_OK on success;

 @Cautions      Allowed only after QM_Init().
*//***************************************************************************/
t_Error QM_ReserveQueues(t_Handle h_Qm, t_QmRsrvFqrParams *p_QmFqrParams, uint32_t  *p_BaseFqid);

#if (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0))
/**************************************************************************//**
 @Function      QM_DumpRegs

 @Description   Dumps all QM registers

 @Param[in]     h_Qm        - A handle to the QM Module.

 @Return        E_OK on success;

 @Cautions      Allowed only after QM_Init().
*//***************************************************************************/
t_Error QM_DumpRegs(t_Handle h_Qm);
#endif /* (defined(DEBUG_ERRORS) && ... */

/**************************************************************************//**
 @Function      QM_SetException

 @Description   Calling this routine enables/disables the specified exception.

 @Param[in]     h_Qm        - A handle to the QM Module.
 @Param[in]     exception   - The exception to be selected.
 @Param[in]     enable      - TRUE to enable interrupt, FALSE to mask it.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error QM_SetException(t_Handle h_Qm, e_QmExceptions exception, bool enable);

/**************************************************************************//**
 @Function      QM_ErrorIsr

 @Description   QM interrupt-service-routine for errors.

 @Param[in]     h_Qm            - A handle to the QM module

 @Cautions      Allowed only following QM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
void    QM_ErrorIsr(t_Handle h_Qm);

/**************************************************************************//**
 @Function      QM_GetErrorInformation

 @Description   Reads the last error information.

 @Param[in]     h_Qm        - A handle to the QM Module.
 @Param[out]    p_errInfo   - the information will be loaded to this struct.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_Init().
                This routine should NOT be called from guest-partition
                (i.e. guestId != NCSW_MASTER_ID)
*//***************************************************************************/
t_Error QM_GetErrorInformation(t_Handle h_Qm, t_QmErrorInfo *p_errInfo);

/**************************************************************************//**
 @Function      QM_GetCounter

 @Description   Reads one of the QM counters.

 @Param[in]     h_Qm        - A handle to the QM Module.
 @Param[in]     counter     - The requested counter.

 @Return        Counter's current value.

 @Cautions      Allowed only following QM_Init().
*//***************************************************************************/
uint32_t    QM_GetCounter(t_Handle h_Qm, e_QmCounters counter);

/**************************************************************************//**
 @Function      QM_GetRevision

 @Description   Returns the QM revision

 @Param[in]     h_Qm                A handle to a QM Module.
 @Param[out]    p_QmRevisionInfo    A structure of revision information parameters.

 @Return        None.

 @Cautions      Allowed only following QM_Init().
*//***************************************************************************/
t_Error QM_GetRevision(t_Handle h_Qm, t_QmRevisionInfo *p_QmRevisionInfo);

/** @} */ /* end of QM_runtime_control_grp group */


/**************************************************************************//**
 @Group         QM_runtime_data_grp QM (common) Runtime Data Unit

 @Description   QM (common) Runtime data unit API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      QM_Poll

 @Description   Poll frames from QM.

 @Param[in]     h_Qm            - A handle to the QM module
 @Param[in]     source          - The selected frames type to poll

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_Init().
*//***************************************************************************/
t_Error QM_Poll(t_Handle h_Qm, e_QmPortalPollSource source);

/** @} */ /* end of QM_runtime_data_grp group */
/** @} */ /* end of QM_lib_grp group */


/**************************************************************************//**
 @Group         QM_portal_grp QM-Portal API

 @Description   QM common API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         QM_portal_init_grp QM-Portal Initialization Unit

 @Description   QM-Portal Initialization Unit

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   structure representing QM-Portal Stash parameters
*//***************************************************************************/
typedef struct {
    uint8_t                         stashDestQueue;         /**< This value is used to direct all stashing transactions initiated on behalf of this software portal
                                                                 to the specific Stashing Request Queues (SRQ) */
    uint8_t                         eqcr;                   /**< If 0, disabled. If 1, for every EQCR entry consumed by QMan a new stash transaction is performed.
                                                                 If 2-7, after 2-7 EQCR entries being consumed by QMAN a new stash transaction is performed. */
    bool                            eqcrHighPri;            /**< EQCR entry stash transactions for this software portal will be signaled with higher priority. */
    bool                            dqrr;                   /**< DQRR entry stash enable/disable */
    uint16_t                        dqrrLiodn;              /**< This value is attached to every transaction initiated by QMan when performing DQRR entry or EQCR_CI stashing
                                                                 on behalf of this software portal */
    bool                            dqrrHighPri;            /**< DQRR entry stash transactions for this software portal will be signaled with higher priority. */
    bool                            fdFq;                   /**< Dequeued Frame Data, Annotation, and FQ Context Stashing enable/disable */
    uint16_t                        fdFqLiodn;              /**< This value is attached to every transaction initiated by QMan when performing dequeued frame data and
                                                                 annotation stashing, or FQ context stashing on behalf of this software portal */
    bool                            fdFqHighPri;            /**< Dequeued frame data, annotation, and FQ context stash transactions for this software portal will be signaled
                                                                 with higher priority. */
    bool                            fdFqDrop;               /**< If True, Dequeued frame data, annotation, and FQ context stash transactions for this software portal will be dropped
                                                                          by QMan if the target SRQ is almost full, to prevent QMan sequencer stalling. Stash transactions that are
                                                                          dropped will result in a fetch from main memory when a core reads the addressed coherency granule.
                                                                 If FALSE, Dequeued frame data, annotation, and FQ context stash transactions for this software portal will never be
                                                                           dropped by QMan. If the target SRQ is full a sequencer will stall until each stash transaction can be completed. */
} t_QmPortalStashParam;

/**************************************************************************//**
 @Description   structure representing QM-Portal initialization parameters
*//***************************************************************************/
typedef struct {
    uintptr_t                       ceBaseAddress;          /**< Cache-enabled base address (virtual) */
    uintptr_t                       ciBaseAddress;          /**< Cache-inhibited base address (virtual) */
    t_Handle                        h_Qm;                   /**< Qm Handle */
    e_DpaaSwPortal                  swPortalId;             /**< Portal id */
    uintptr_t                       irq;                    /**< portal interrupt line; used only if useIrq set to TRUE */
    uint16_t                        fdLiodnOffset;                /**< liodn to be used for all frames enqueued via this software portal */
    t_QmReceivedFrameCallback       *f_DfltFrame;           /**< this callback will be called unless specific callback assigned to the FQ*/
    t_QmRejectedFrameCallback       *f_RejectedFrame;       /**< this callback will be called for rejected frames. */
    t_Handle                        h_App;                  /**< a handle to the upper layer; It will be passed by the driver upon calling the CB */
} t_QmPortalParam;


/**************************************************************************//**
 @Function      QM_PORTAL_Config

 @Description   Creates descriptor for a QM-Portal module.

                The routine returns a handle (descriptor) to a QM-Portal object.
                This descriptor must be passed as first parameter to all other
                QM-Portal function calls.

                No actual initialization or configuration of QM-Portal hardware is
                done by this routine.

 @Param[in]     p_QmPortalParam   - Pointer to data structure of parameters

 @Retval        Handle to a QM-Portal object, or NULL for Failure.
*//***************************************************************************/
t_Handle QM_PORTAL_Config(t_QmPortalParam *p_QmPortalParam);

/**************************************************************************//**
 @Function      QM_PORTAL_Init

 @Description   Initializes a QM-Portal module

 @Param[in]     h_QmPortal - A handle to a QM-Portal module

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error QM_PORTAL_Init(t_Handle h_QmPortal);

/**************************************************************************//**
 @Function      QM_PORTAL_Free

 @Description   Frees all resources that were assigned to a QM-Portal module.

                Calling this routine invalidates the descriptor.

 @Param[in]     h_QmPortal - A handle to a QM-Portal module

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error QM_PORTAL_Free(t_Handle h_QmPortal);

/**************************************************************************//**
 @Group         QM_portal_advanced_init_grp    QM-Portal Advanced Configuration Unit

 @Description   Configuration functions used to change default values.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      QM_PORTAL_ConfigDcaMode

 @Description   Change the Discrate Consumption Acknowledge mode
                from its default configuration [FALSE].

 @Param[in]     h_QmPortal  - A handle to a QM-Portal module
 @Param[in]     enable      - Enable/Disable DCA mode

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_PORTAL_Config() and before QM_PORTAL_Init().
*//***************************************************************************/
t_Error QM_PORTAL_ConfigDcaMode(t_Handle h_QmPortal, bool enable);

/**************************************************************************//**
 @Function      QM_PORTAL_ConfigStash

 @Description   Config the portal to active stash mode.

 @Param[in]     h_QmPortal      - A handle to a QM-Portal module
 @Param[in]     p_StashParams   - Pointer to data structure of parameters

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_PORTAL_Config() and before QM_PORTAL_Init().
*//***************************************************************************/
t_Error QM_PORTAL_ConfigStash(t_Handle h_QmPortal, t_QmPortalStashParam *p_StashParams);


/**************************************************************************//**
 @Function      QM_PORTAL_ConfigPullMode

 @Description   Change the Pull Mode from its default configuration [FALSE].

 @Param[in]     h_QmPortal  - A handle to a QM-Portal module
 @Param[in]     pullMode    - When TRUE, the Portal will work in pull mode.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_PORTAL_Config() and before QM_PORTAL_Init().
*//***************************************************************************/
t_Error QM_PORTAL_ConfigPullMode(t_Handle h_QmPortal, bool pullMode);

/** @} */ /* end of QM_portal_advanced_init_grp group */
/** @} */ /* end of QM_portal_init_grp group */


/**************************************************************************//**
 @Group         QM_portal_runtime_control_grp QM-Portal Runtime Control Unit

 @Description   QM-Portal Runtime control unit API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      QM_PORTAL_AddPoolChannel

 @Description   Adding the pool channel to the SW-Portal's scheduler.
                the sw-portal will get frames that came from the pool channel.

 @Param[in]     h_QmPortal      - A handle to a QM-Portal module
 @Param[in]     poolChannelId   - Pool channel id. must between '0' to QM_MAX_NUM_OF_POOL_CHANNELS

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_PORTAL_Init().
*//***************************************************************************/
t_Error QM_PORTAL_AddPoolChannel(t_Handle h_QmPortal, uint8_t poolChannelId);

/** @} */ /* end of QM_portal_runtime_control_grp group */


/**************************************************************************//**
 @Group         QM_portal_runtime_data_grp QM-Portal Runtime Data Unit

 @Description   QM-Portal Runtime data unit API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description structure representing QM Portal Frame Info
*//***************************************************************************/
typedef struct t_QmPortalFrameInfo {
    t_Handle    h_App;
    t_Handle    h_QmFqr;
    uint32_t    fqidOffset;
    t_DpaaFD    frame;
} t_QmPortalFrameInfo;

/**************************************************************************//**
 @Function      QM_PORTAL_Poll

 @Description   Poll frames from the specified sw-portal.

 @Param[in]     h_QmPortal      - A handle to a QM-Portal module
 @Param[in]     source          - The selected frames type to poll

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_PORTAL_Init().
*//***************************************************************************/
t_Error QM_PORTAL_Poll(t_Handle h_QmPortal, e_QmPortalPollSource source);

/**************************************************************************//**
 @Function      QM_PORTAL_PollFrame

 @Description   Poll frames from the specified sw-portal. will poll only data frames

 @Param[in]     h_QmPortal      - A handle to a QM-Portal module
 @Param[out]    p_frameInfo     - A structure to hold the dequeued frame information

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_PORTAL_Init().
*//***************************************************************************/
t_Error QM_PORTAL_PollFrame(t_Handle h_QmPortal, t_QmPortalFrameInfo *p_frameInfo);


/** @} */ /* end of QM_portal_runtime_data_grp group */
/** @} */ /* end of QM_portal_grp group */


/**************************************************************************//**
 @Group         QM_fqr_grp QM Frame-Queue-Range API

 @Description   QM-FQR API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         QM_fqr_init_grp QM-FQR Initialization Unit

 @Description   QM-FQR Initialization Unit

 @{
*//***************************************************************************/


/**************************************************************************//**
 @Description structure representing QM FQ-Range congestion group parameters
*//***************************************************************************/
typedef struct {
    t_Handle    h_QmCg;                     /**< A handle to the congestion group. */
    int8_t      overheadAccountingLength;   /**< For each frame add this number for CG calculation
                                                 (may be negative), if 0 - disable feature */
    uint32_t    fqTailDropThreshold;        /**< if not "0" - enable tail drop on this FQR */
} t_QmFqrCongestionAvoidanceParams;

/**************************************************************************//**
 @Description   structure representing QM FQ-Range initialization parameters
*//***************************************************************************/
typedef struct {
    t_Handle            h_Qm;           /**< A handle to a QM module */
    t_Handle            h_QmPortal;     /**< A handle to a QM Portal Module;
                                             will be used only for Init and Free routines;
                                             NOTE : if NULL, assuming affinity */
    bool                initParked;     /**< This FQ-Range will be initialize in park state (un-schedule) */
    bool                holdActive;     /**< This FQ-Range can be parked (un-schedule);
                                             This affects only on queues destined to software portals*/
    bool                preferInCache;  /**< Prefer this FQ-Range to be in QMAN's internal cache for all states */
    bool                useContextAForStash;/**< This FQ-Range will use context A for stash */
    union {
        struct {
            uint8_t     frameAnnotationSize;/**< Size of Frame Annotation to be stashed */
            uint8_t     frameDataSize;      /**< Size of Frame Data to be stashed. */
            uint8_t     fqContextSize;      /**< Size of FQ context to be stashed. */
            uint64_t    fqContextAddr;      /**< 40 bit memory address containing the FQ context information to be stashed;
                                                 Must be cacheline-aligned */
        } stashingParams;
        t_QmContextA    *p_ContextA;    /**< context-A field to be written in the FQ structure */
    };
    t_QmContextB        *p_ContextB;    /**< context-B field to be written in the FQ structure;
                                             Note that this field may be used for Tx queues only! */
    e_QmFQChannel       channel;        /**< Qm Channel */
    uint8_t             wq;             /**< Work queue within the channel */
    bool                shadowMode;     /**< If TRUE, useForce MUST set to TRUE and numOfFqids MUST set to '1' */
    uint32_t            numOfFqids;     /**< number of fqids to be allocated*/
    bool                useForce;       /**< TRUE - force allocation of specific fqids;
                                             FALSE - allocate several fqids */
    union{
        struct {
            uint32_t    align;          /**< alignment. will be used if useForce=FALSE */
        } nonFrcQs;
        struct {
            uint32_t    fqid;           /**< the fqid base of the forced fqids. will be used if useForce=TRUE */
        } frcQ;
    } qs;
    bool                congestionAvoidanceEnable;
                                        /**< TRUE to enable congestion avoidance mechanism */
    t_QmFqrCongestionAvoidanceParams    congestionAvoidanceParams;
                                        /**< Parameters for congestion avoidance */
} t_QmFqrParams;


/**************************************************************************//**
 @Function      QM_FQR_Create

 @Description   Initializing and enabling a Frame-Queue-Range.
                This routine should be called for adding an FQR.

 @Param[in]     p_QmFqrParams   - A structure of parameters for defining the
                                  desired queues parameters.

 @Return        A handle to the initialized FQR on success; NULL code otherwise.

 @Cautions      Allowed only following QM_Init().
*//***************************************************************************/
t_Handle QM_FQR_Create(t_QmFqrParams *p_QmFqrParams);

/**************************************************************************//**
 @Function      QM_FQR_Free

 @Description   Deleting and free all resources of an initialized FQR.

 @Param[in]     h_QmFqr         - A handle to a QM-FQR Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_Init() and QM_FQR_Create() for this FQR.
*//***************************************************************************/
t_Error QM_FQR_Free(t_Handle h_QmFqr);

/**************************************************************************//**
 @Function      QM_FQR_FreeWDrain

 @Description   Deleting and free all resources of an initialized FQR
                with the option of draining.

 @Param[in]     h_QmFqr         - A handle to a QM-FQR Module.
 @Param[in]     f_CompletionCB  - Pointer to a completion callback to be used in non-blocking mode.
 @Param[in]     deliverFrame    - TRUE for deliver the drained frames to the user;
                                  FALSE for not deliver the frames.
 @Param[in]     f_CallBack      - Pointer to a callback to handle the delivered frames.
 @Param[in]     h_App           - User's application descriptor.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_Init() and QM_FQR_Create() for this FQR.
*//***************************************************************************/
t_Error QM_FQR_FreeWDrain(t_Handle                     h_QmFqr,
                          t_QmFqrDrainedCompletionCB   *f_CompletionCB,
                          bool                         deliverFrame,
                          t_QmReceivedFrameCallback    *f_CallBack,
                          t_Handle                     h_App);

/** @} */ /* end of QM_fqr_init_grp group */


/**************************************************************************//**
 @Group         QM_fqr_runtime_control_grp QM-FQR Runtime Control Unit

 @Description   QM-FQR Runtime control unit API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   enum for defining QM-FQR counters
*//***************************************************************************/
typedef enum e_QmFqrCounters {
    e_QM_FQR_COUNTERS_FRAME = 0,        /**< Total number of frames on this frame queue */
    e_QM_FQR_COUNTERS_BYTE              /**< Total number of bytes in all frames on this frame queue */
} e_QmFqrCounters;

/**************************************************************************//**
 @Function      QM_FQR_RegisterCB

 @Description   Register a callback routine to be called when a frame comes from this FQ-Range

 @Param[in]     h_QmFqr         - A handle to a QM-FQR Module.
 @Param[in]     f_CallBack      - An application callback
 @Param[in]     h_App           - User's application descriptor

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_FQR_Create().
*//***************************************************************************/
t_Error QM_FQR_RegisterCB(t_Handle h_QmFqr, t_QmReceivedFrameCallback *f_CallBack, t_Handle h_App);

/**************************************************************************//**
 @Function      QM_FQR_Resume

 @Description   Request to Re-Schedule this Fqid.

 @Param[in]     h_QmFqr         - A handle to a QM-FQR Module.
 @Param[in]     h_QmPortal      - A handle to a QM Portal Module;
                                  NOTE : if NULL, assuming affinity.
 @Param[in]     fqidOffset      - Fqid offset within the FQ-Range.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_FQR_Create().
*//***************************************************************************/
t_Error QM_FQR_Resume(t_Handle h_QmFqr, t_Handle h_QmPortal, uint32_t fqidOffset);

/**************************************************************************//**
 @Function      QM_FQR_Suspend

 @Description   Request to Un-Schedule this Fqid.

 @Param[in]     h_QmFqr         - A handle to a QM-FQR Module.
 @Param[in]     h_QmPortal      - A handle to a QM Portal Module;
                                  NOTE : if NULL, assuming affinity.
 @Param[in]     fqidOffset      - Fqid offset within the FQ-Range.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_FQR_Create().
*//***************************************************************************/
t_Error QM_FQR_Suspend(t_Handle h_QmFqr, t_Handle h_QmPortal, uint32_t fqidOffset);

/**************************************************************************//**
 @Function      QM_FQR_GetFqid

 @Description   Returned the Fqid base of the FQ-Range

 @Param[in]     h_QmFqr         - A handle to a QM-FQR Module.

 @Return        Fqid base.

 @Cautions      Allowed only following QM_FQR_Create().
*//***************************************************************************/
uint32_t QM_FQR_GetFqid(t_Handle h_QmFqr);

/**************************************************************************//**
 @Function      QM_FQR_GetCounter

 @Description   Reads one of the QM-FQR counters.

 @Param[in]     h_QmFqr         - A handle to a QM-FQR Module.
 @Param[in]     h_QmPortal      - A handle to a QM Portal Module;
                                  NOTE : if NULL, assuming affinity.
 @Param[in]     fqidOffset      - Fqid offset within the FQ-Range.
 @Param[in]     counter         - The requested counter.

 @Return        Counter's current value.

 @Cautions      Allowed only following QM_FQR_Create().
*//***************************************************************************/
uint32_t QM_FQR_GetCounter(t_Handle h_QmFqr, t_Handle h_QmPortal, uint32_t fqidOffset, e_QmFqrCounters counter);

/** @} */ /* end of QM_fqr_runtime_control_grp group */


/**************************************************************************//**
 @Group         QM_fqr_runtime_data_grp QM-FQR Runtime Data Unit

 @Description   QM-FQR Runtime data unit API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      QM_FQR_Enqueue

 @Description   Enqueue the frame into the FQ to be transmitted.

 @Param[in]     h_QmFqr         - A handle to a QM-FQR Module.
 @Param[in]     h_QmPortal      - A handle to a QM Portal Module;
                                  NOTE : if NULL, assuming affinity.
 @Param[in]     fqidOffset      - Fqid offset within the FQ-Range.
 @Param[in]     p_Frame         - Pointer to the frame to be enqueued.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_FQR_Create().
*//***************************************************************************/
t_Error QM_FQR_Enqueue(t_Handle h_QmFqr, t_Handle h_QmPortal, uint32_t fqidOffset, t_DpaaFD *p_Frame);

/**************************************************************************//**
 @Function      QM_FQR_PullFrame

 @Description   Perform a Pull command.

 @Param[in]     h_QmFqr         - A handle to a QM-FQR Module.
 @Param[in]     h_QmPortal      - A handle to a QM Portal Module;
                                  NOTE : if NULL, assuming affinity.
 @Param[in]     fqidOffset      - Fqid offset within the FQ-Range.
 @Param[out]    p_Frame         - The Received Frame

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_PORTAL_Init().
*//***************************************************************************/
t_Error QM_FQR_PullFrame(t_Handle h_QmFqr, t_Handle h_QmPortal, uint32_t fqidOffset, t_DpaaFD *p_Frame);


/** @} */ /* end of QM_fqr_runtime_data_grp group */
/** @} */ /* end of QM_fqr_grp group */


/**************************************************************************//**
 @Group         QM_cg_grp QM Congestion Group API

 @Description   QM-CG API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         QM_cg_init_grp QM-Congestion Group Initialization Unit

 @Description   QM-CG Initialization Unit

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   structure representing QM CG WRED curve
*//***************************************************************************/
typedef struct t_QmCgWredCurve {
    uint32_t    maxTh;                  /**< minimum threshold - below this level
                                             all packets are rejected (approximated
                                             to be expressed as x*2^y due to HW
                                             implementation)*/
    uint32_t    minTh;                  /**< minimum threshold - below this level
                                             all packets are accepted (approximated
                                             due to HW implementation)*/
    uint8_t    probabilityDenominator;  /**< 1-64, the fraction of packets dropped
                                             when the average queue depth is at the
                                             maximum threshold.(approximated due to HW
                                             implementation). */
} t_QmCgWredCurve;

/**************************************************************************//**
 @Description   structure representing QM CG WRED parameters
*//***************************************************************************/
typedef struct t_QmCgWredParams {
    bool            enableGreen;
    t_QmCgWredCurve greenCurve;
    bool            enableYellow;
    t_QmCgWredCurve yellowCurve;
    bool            enableRed;
    t_QmCgWredCurve redCurve;
} t_QmCgWredParams;

/**************************************************************************//**
 @Description   structure representing QM CG configuration parameters
*//***************************************************************************/
typedef struct t_QmCgParams {
    t_Handle                h_Qm;           /**< A handle to a QM module */
    t_Handle                h_QmPortal;     /**< A handle to a QM Portal Module;
                                                 will be used for Init, Free and as
                                                 an interrupt destination for cg state
                                                 change (if CgStateChangeEnable = TRUE) */
    bool                    frameCount;     /**< TRUE for frame count, FALSE - byte count */
    bool                    wredEnable;     /**< if TRUE - WRED enabled. Each color is enabled independently
                                                 so that some colors may use WRED, but others may use
                                                 Tail drop - if enabled, or none.  */
    t_QmCgWredParams        wredParams;     /**< WRED parameters, relevant if wredEnable = TRUE*/
    bool                    tailDropEnable; /**< if TRUE - Tail drop enabled */
    uint32_t                threshold;      /**< If Tail drop - used as Tail drop threshold, otherwise
                                                 'threshold' may still be used to receive notifications
                                                 when threshold is passed. If threshold and f_Exception
                                                 are set, interrupts are set defaultly by driver. */
    bool                    notifyDcPortal; /**< Relevant if this CG receives enqueues from a direct portal
                                                 e_DPAA_DCPORTAL0 or e_DPAA_DCPORTAL1. TRUE to notify
                                                 the DC portal, FALSE to notify this SW portal. */
    e_DpaaDcPortal          dcPortalId;     /**< relevant if notifyDcPortal=TRUE - DC Portal id */
    t_QmExceptionsCallback  *f_Exception;   /**< relevant and mandatory if threshold is configured and
                                                 notifyDcPortal = FALSE. If threshold and f_Exception
                                                 are set, interrupts are set defaultly by driver */
    t_Handle                h_App;          /**< A handle to the application layer, will be passed as
                                                 argument to f_Exception */
} t_QmCgParams;


/**************************************************************************//**
 @Function      QM_CG_Create

 @Description   Create and configure a congestion Group.

 @Param[in]     p_CgParams      - CG parameters

 @Return        A handle to the CG module

 @Cautions      Allowed only following QM_Init().
*//***************************************************************************/
t_Handle    QM_CG_Create(t_QmCgParams *p_CgParams);

/**************************************************************************//**
 @Function      QM_CG_Free

 @Description   Deleting and free all resources of an initialized CG.

 @Param[in]     h_QmCg         - A handle to a QM-CG Module.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_Init() and QM_CR_Create() for this CG.
*//***************************************************************************/
t_Error QM_CG_Free(t_Handle h_QmCg);

/** @} */ /* end of QM_cg_init_grp group */


/**************************************************************************//**
 @Group         QM_cg_runtime_control_grp QM-CG Runtime Control Unit

 @Description   QM-CG Runtime control unit API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   structure representing QM CG WRED colors
*//***************************************************************************/
typedef enum e_QmCgColor {
    e_QM_CG_COLOR_GREEN,
    e_QM_CG_COLOR_YELLOW,
    e_QM_CG_COLOR_RED
} e_QmCgColor;

/**************************************************************************//**
 @Description   structure representing QM CG modification parameters
*//***************************************************************************/
typedef struct t_QmCgModifyWredParams {
    e_QmCgColor         color;
    bool                enable;
    t_QmCgWredCurve     wredParams;
} t_QmCgModifyWredParams;


/**************************************************************************//**
 @Function      QM_CG_SetException

 @Description   Set CG exception.

 @Param[in]     h_QmCg         - A handle to a QM-CG Module.
 @Param[in]     exception      - exception enum
 @Param[in]     enable         - TRUE to enable, FALSE to disable.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_Init() and QM_CG_Create() for this CG.
*//***************************************************************************/
t_Error QM_CG_SetException(t_Handle h_QmCg, e_QmExceptions exception, bool enable);

/**************************************************************************//**
 @Function      QM_CG_ModifyWredCurve

 @Description   Change WRED curve parameters for a selected color.
                Note that this routine may be called only for valid CG's that
                already have been configured for WRED, and only need a change
                in the WRED parameters.

 @Param[in]     h_QmCg              - A handle to a QM-CG Module.
 @Param[in]     p_QmCgModifyParams  - A structure of new WRED parameters.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_Init() and QM_CG_Create() for this CG.
*//***************************************************************************/
t_Error QM_CG_ModifyWredCurve(t_Handle h_QmCg, t_QmCgModifyWredParams *p_QmCgModifyParams);

/**************************************************************************//**
 @Function      QM_CG_ModifyTailDropThreshold

 @Description   Change WRED curve parameters for a selected color.
                Note that this routine may be called only for valid CG's that
                already have been configured for tail drop, and only need a change
                in the threshold value.

 @Param[in]     h_QmCg              - A handle to a QM-CG Module.
 @Param[in]     threshold           - New threshold.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_Init() and QM_CG_Create() for this CG.
*//***************************************************************************/
t_Error QM_CG_ModifyTailDropThreshold(t_Handle h_QmCg, uint32_t threshold);


/** @} */ /* end of QM_cg_runtime_control_grp group */
/** @} */ /* end of QM_cg_grp group */
/** @} */ /* end of QM_grp group */


#endif /* __QM_EXT_H */
