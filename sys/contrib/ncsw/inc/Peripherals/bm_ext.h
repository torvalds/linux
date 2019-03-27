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
 @File          bm_ext.h

 @Description   BM API
*//***************************************************************************/
#ifndef __BM_EXT_H
#define __BM_EXT_H

#include "error_ext.h"
#include "std_ext.h"


/**************************************************************************//**
 @Group         BM_grp Buffer Manager API

 @Description   BM API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   This callback type is used when handling pool depletion entry/exit.

                User provides this function. Driver invokes it.

 @Param[in]     h_App       - User's application descriptor.
 @Param[in]     in          - TRUE when entered depletion state
                              FALSE when exit the depletion state.
 *//***************************************************************************/
typedef void (t_BmDepletionCallback)(t_Handle h_App, bool in);

/**************************************************************************//**
 @Group         BM_lib_grp BM common API

 @Description   BM common API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   BM Exceptions
*//***************************************************************************/
typedef enum e_BmExceptions {
    e_BM_EX_INVALID_COMMAND = 0 ,   /**< Invalid Command Verb Interrupt */
    e_BM_EX_FBPR_THRESHOLD,         /**< FBPR Low Watermark Interrupt. */
    e_BM_EX_SINGLE_ECC,             /**< Single Bit ECC Error Interrupt. */
    e_BM_EX_MULTI_ECC               /**< Multi Bit ECC Error Interrupt */
} e_BmExceptions;


/**************************************************************************//**
 @Group         BM_init_grp BM (common) Initialization Unit

 @Description   BM (common) Initialization Unit

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      t_BmExceptionsCallback

 @Description   Exceptions user callback routine, will be called upon an
                exception passing the exception identification.

 @Param[in]     h_App      - User's application descriptor.
 @Param[in]     exception  - The exception.
*//***************************************************************************/
typedef void (t_BmExceptionsCallback) (t_Handle              h_App,
                                       e_BmExceptions        exception);

/**************************************************************************//**
 @Description   structure representing BM initialization parameters
*//***************************************************************************/
typedef struct {
    uint8_t                 guestId;                /**< BM Partition Id */

    uintptr_t               baseAddress;            /**< Bm base address (virtual).
                                                         NOTE: this parameter relevant only for BM in master mode ('guestId'=NCSW_MASTER_ID). */
    uint16_t                liodn;                  /**< This value is attached to every transaction initiated by
                                                         BMan when accessing its private data structures
                                                         NOTE: this parameter relevant only for BM in master mode ('guestId'=NCSW_MASTER_ID). */
    uint32_t                totalNumOfBuffers;      /**< Total number of buffers
                                                         NOTE: this parameter relevant only for BM in master mode ('guestId'=NCSW_MASTER_ID). */
    uint32_t                fbprMemPartitionId;     /**< FBPR's mem partition id;
                                                         NOTE: The memory partition must be non-cacheable and no-coherent area.
                                                         NOTE: this parameter relevant only for BM in master mode ('guestId'=NCSW_MASTER_ID). */
    t_BmExceptionsCallback  *f_Exception;           /**< An application callback routine to handle exceptions.
                                                         NOTE: this parameter relevant only for BM in master mode ('guestId'=NCSW_MASTER_ID). */
    t_Handle                h_App;                  /**< A handle to an application layer object; This handle will
                                                         be passed by the driver upon calling the above callbacks.
                                                         NOTE: this parameter relevant only for BM in master mode ('guestId'=NCSW_MASTER_ID). */
    uintptr_t               errIrq;                 /**< BM error interrupt line; NO_IRQ if interrupts not used.
                                                         NOTE: this parameter relevant only for BM in master mode ('guestId'=NCSW_MASTER_ID). */

    uint8_t                 partBpidBase;           /**< The first buffer-pool-id dedicated to this partition.
                                                         NOTE: this parameter relevant only when working with multiple partitions. */
    uint8_t                 partNumOfPools;         /**< Number of Pools dedicated to this partition.
                                                         NOTE: this parameter relevant only when working with multiple partitions. */
} t_BmParam;


/**************************************************************************//**
 @Function      BM_Config

 @Description   Creates descriptor for the BM module and initializes the BM module.

                The routine returns a handle (descriptor) to the BM object.
                This descriptor must be passed as first parameter to all other
                BM function calls.

 @Param[in]     p_BmParam   - A pointer to data structure of parameters

 @Return        Handle to BM object, or NULL for Failure.
*//***************************************************************************/
t_Handle    BM_Config(t_BmParam *p_BmParam);

/**************************************************************************//**
 @Function      BM_Init

 @Description   Initializes the BM module

 @Param[in]     h_Bm            - A handle to the BM module

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following BM_Config().
*//***************************************************************************/
t_Error    BM_Init(t_Handle h_Bm);

/**************************************************************************//**
 @Function      BM_Free

 @Description   Frees all resources that were assigned to BM module.

                Calling this routine invalidates the descriptor.

 @Param[in]     h_Bm            - A handle to the BM module

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error     BM_Free(t_Handle h_Bm);

/**************************************************************************//**
 @Group         BM_advanced_init_grp    BM (common) Advanced Configuration Unit

 @Description   Configuration functions used to change default values.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      BM_ConfigFbprThreshold

 @Description   Change the fbpr threshold from its default
                configuration [0].
                An interrupt if enables is asserted when the number of FBPRs is below this threshold.
                NOTE: this parameter relevant only for BM in master mode ('guestId'=NCSW_MASTER_ID).

 @Param[in]     h_Bm            - A handle to the BM module
 @Param[in]     threshold       - threshold value.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following BM_Config() and before BM_Init().
*//***************************************************************************/
t_Error     BM_ConfigFbprThreshold(t_Handle h_Bm, uint32_t threshold);

/** @} */ /* end of BM_advanced_init_grp group */
/** @} */ /* end of BM_init_grp group */

/**************************************************************************//**
 @Group         BM_runtime_control_grp BM (common) Runtime Control Unit

 @Description   BM (common) Runtime control unit API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   enum for defining BM counters
*//***************************************************************************/
typedef enum e_BmCounters {
    e_BM_COUNTERS_FBPR = 0              /**< Total Free Buffer Proxy Record (FBPR) Free Pool Count in external memory */
} e_BmCounters;

/**************************************************************************//**
 @Description   structure for returning revision information
*//***************************************************************************/
typedef struct t_BmRevisionInfo {
    uint8_t         majorRev;               /**< Major revision */
    uint8_t         minorRev;               /**< Minor revision */
} t_BmRevisionInfo;

#if (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0))
/**************************************************************************//**
 @Function      BM_DumpRegs

 @Description   Dumps all BM registers
                NOTE: this parameter relevant only for BM in master mode ('guestId'=NCSW_MASTER_ID).

 @Param[in]     h_Bm      A handle to an BM Module.

 @Return        E_OK on success;

 @Cautions      Allowed only after BM_Init().
*//***************************************************************************/
t_Error BM_DumpRegs(t_Handle h_Bm);
#endif /* (defined(DEBUG_ERRORS) && ... */

/**************************************************************************//**
 @Function      BM_SetException

 @Description   Calling this routine enables/disables the specified exception.
                NOTE: this parameter relevant only for BM in master mode ('guestId'=NCSW_MASTER_ID).

 @Param[in]     h_Bm        - A handle to the BM Module.
 @Param[in]     exception   - The exception to be selected.
 @Param[in]     enable      - TRUE to enable interrupt, FALSE to mask it.

 @Cautions      Allowed only following BM_Init().
*//***************************************************************************/
t_Error     BM_SetException(t_Handle h_Bm, e_BmExceptions exception, bool enable);

/**************************************************************************//**
 @Function      BM_ErrorIsr

 @Description   BM interrupt-service-routine for errors.
                NOTE: this parameter relevant only for BM in master mode ('guestId'=NCSW_MASTER_ID).

 @Param[in]     h_Bm        - A handle to the BM Module.

 @Cautions      Allowed only following BM_Init().
*//***************************************************************************/
void        BM_ErrorIsr(t_Handle h_Bm);

/**************************************************************************//**
 @Function      BM_GetCounter

 @Description   Reads one of the BM counters.

 @Param[in]     h_Bm        - A handle to the BM Module.
 @Param[in]     counter     - The requested counter.

 @Return        Counter's current value.
*//***************************************************************************/
uint32_t    BM_GetCounter(t_Handle h_Bm, e_BmCounters counter);

/**************************************************************************//**
 @Function      BM_GetRevision

 @Description   Returns the BM revision

 @Param[in]     h_Bm                A handle to a BM Module.
 @Param[out]    p_BmRevisionInfo    A structure of revision information parameters.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following QM_Init().
*//***************************************************************************/
t_Error  BM_GetRevision(t_Handle h_Bm, t_BmRevisionInfo *p_BmRevisionInfo);

/** @} */ /* end of BM_runtime_control_grp group */
/** @} */ /* end of BM_lib_grp group */


/**************************************************************************//**
 @Group         BM_portal_grp BM-Portal API

 @Description   BM-Portal API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         BM_portal_init_grp BM-Portal Initialization Unit

 @Description   BM-Portal Initialization Unit

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   structure representing BM Portal initialization parameters
*//***************************************************************************/
typedef struct {
    uintptr_t       ceBaseAddress;          /**< Cache-enabled base address (virtual) */
    uintptr_t       ciBaseAddress;          /**< Cache-inhibited base address (virtual) */
    t_Handle        h_Bm;                   /**< Bm Handle */
    e_DpaaSwPortal  swPortalId;             /**< Portal id */
    uintptr_t       irq;                    /**< portal interrupt line; NO_IRQ if interrupts not used */
} t_BmPortalParam;


/**************************************************************************//**
 @Function      BM_PORTAL_Config

 @Description   Creates descriptor for the BM Portal;

                The routine returns a handle (descriptor) to a BM-Portal object;
                This descriptor must be passed as first parameter to all other
                BM-Portal function calls.

                No actual initialization or configuration of QM-Portal hardware is
                done by this routine.

 @Param[in]     p_BmPortalParam   - Pointer to data structure of parameters

 @Retval        Handle to a BM-Portal object, or NULL for Failure.
*//***************************************************************************/
t_Handle BM_PORTAL_Config(t_BmPortalParam *p_BmPortalParam);

/**************************************************************************//**
 @Function      BM_PORTAL_Init

 @Description   Initializes a BM-Portal module

 @Param[in]     h_BmPortal - A handle to a BM-Portal module

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error  BM_PORTAL_Init(t_Handle h_BmPortal);

/**************************************************************************//**
 @Function      BM_PortalFree

 @Description   Frees all resources that were assigned to BM Portal module.

                Calling this routine invalidates the descriptor.

 @Param[in]     h_BmPortal  - BM Portal module descriptor

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error     BM_PORTAL_Free(t_Handle h_BmPortal);

/**************************************************************************//**
 @Function      BM_PORTAL_ConfigMemAttr

 @Description   Change the memory attributes
                from its default configuration [MEMORY_ATTR_CACHEABLE].

 @Param[in]     h_BmPortal          - A handle to a BM-Portal module
 @Param[in]     hwExtStructsMemAttr - memory attributes (cache/non-cache, etc.)

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following BM_PORTAL_Config() and before BM_PORTAL_Init().
*//***************************************************************************/
t_Error  BM_PORTAL_ConfigMemAttr(t_Handle h_BmPortal, uint32_t hwExtStructsMemAttr);

/** @} */ /* end of BM_portal_init_grp group */
/** @} */ /* end of BM_portal_grp group */


/**************************************************************************//**
 @Group         BM_pool_grp BM-Pool API

 @Description   BM-Pool API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         BM_pool_init_grp BM-Pool Initialization Unit

 @Description   BM-Pool Initialization Unit

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Collection    BM Pool Depletion Thresholds macros
                The thresholds are represent by an array of size MAX_DEPLETION_THRESHOLDS
                Use the following macros to access the appropriate location in the array.
*//***************************************************************************/
#define BM_POOL_DEP_THRESH_SW_ENTRY 0
#define BM_POOL_DEP_THRESH_SW_EXIT  1
#define BM_POOL_DEP_THRESH_HW_ENTRY 2
#define BM_POOL_DEP_THRESH_HW_EXIT  3

#define MAX_DEPLETION_THRESHOLDS    4
/* @} */


/**************************************************************************//**
 @Description   structure representing BM Pool initialization parameters
*//***************************************************************************/
typedef struct {
    t_Handle                    h_Bm;               /**< A handle to a BM Module. */
    t_Handle                    h_BmPortal;         /**< A handle to a BM Portal Module.
                                                         will be used only for Init and Free routines.
                                                         NOTE: if NULL, assuming affinity */
    uint32_t                    numOfBuffers;       /**< Number of buffers use by this pool
                                                         NOTE: If zero, empty pool buffer is created. */
    t_BufferPoolInfo            bufferPoolInfo;     /**< Data buffers pool information */
    t_Handle                    h_App;              /**< opaque user value passed as a parameter to callbacks */
    bool                        shadowMode;         /**< If TRUE, numOfBuffers will be set to '0'. */
    uint8_t                     bpid;               /**< index of the shadow buffer pool (0-BM_MAX_NUM_OF_POOLS).
                                                         valid only if shadowMode='TRUE'. */
} t_BmPoolParam;


/**************************************************************************//**
 @Function      BM_POOL_Config

 @Description   Creates descriptor for the BM Pool;

                The routine returns a handle (descriptor) to the BM Pool object.

 @Param[in]     p_BmPoolParam   - A pointer to data structure of parameters

 @Return        Handle to BM Portal object, or NULL for Failure.
*//***************************************************************************/
t_Handle    BM_POOL_Config(t_BmPoolParam *p_BmPoolParam);

/**************************************************************************//**
 @Function      BM_POOL_Init

 @Description   Initializes a BM-Pool module

 @Param[in]     h_BmPool - A handle to a BM-Pool module

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error     BM_POOL_Init(t_Handle h_BmPool);

/**************************************************************************//**
 @Function      BM_PoolFree

 @Description   Frees all resources that were assigned to BM Pool module.

                Calling this routine invalidates the descriptor.

 @Param[in]     h_BmPool    - BM Pool module descriptor

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error     BM_POOL_Free(t_Handle h_BmPool);

/**************************************************************************//**
 @Function      BM_POOL_ConfigBpid

 @Description   Config a specific pool id rather than dynamic pool id.

 @Param[in]     h_BmPool    - A handle to a BM-Pool module
 @Param[in]     bpid        - index of the buffer pool (0-BM_MAX_NUM_OF_POOLS).

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following BM_POOL_Config() and before BM_POOL_Init().
*//***************************************************************************/
t_Error  BM_POOL_ConfigBpid(t_Handle h_BmPool, uint8_t bpid);

/**************************************************************************//**
 @Function      BM_POOL_ConfigDepletion

 @Description   Config depletion-entry/exit thresholds and callback.

 @Param[in]     h_BmPool        - A handle to a BM-Pool module
 @Param[in]     f_Depletion     - depletion-entry/exit callback.
 @Param[in]     thresholds      - depletion-entry/exit thresholds.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following BM_POOL_Config() and before BM_POOL_Init();
                Allowed only if shadowMode='FALSE'.
                Allowed only if BM in master mode ('guestId'=NCSW_MASTER_ID), or
                the BM is in guest mode BUT than this routine will invoke IPC
                call to the master.
*//***************************************************************************/
t_Error  BM_POOL_ConfigDepletion(t_Handle               h_BmPool,
                                 t_BmDepletionCallback  *f_Depletion,
                                 uint32_t               thresholds[MAX_DEPLETION_THRESHOLDS]);

/**************************************************************************//**
 @Function      BM_POOL_ConfigStockpile

 @Description   Config software stockpile.

 @Param[in]     h_BmPool     - A handle to a BM-Pool module
 @Param[in]     maxBuffers   - the software data structure size saved for stockpile;
                               when reached this value, release to hw command performed.
 @Param[in]     minBuffers   - if current capacity is equal or lower then this value,
                               acquire from hw command performed.

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following BM_POOL_Config() and before BM_POOL_Init().
*//***************************************************************************/
t_Error  BM_POOL_ConfigStockpile(t_Handle h_BmPool, uint16_t maxBuffers, uint16_t minBuffers);

/**************************************************************************//**
 @Function      BM_POOL_ConfigBuffContextMode

 @Description   Config the BM pool to set/unset buffer-context

 @Param[in]     h_BmPool     - A handle to a BM-Pool module
 @Param[in]     en           - enable/disable buffer context mode

 @Return        E_OK on success; Error code otherwise.

 @Cautions      Allowed only following BM_POOL_Config() and before BM_POOL_Init().
*//***************************************************************************/
t_Error  BM_POOL_ConfigBuffContextMode(t_Handle h_BmPool, bool en);

/** @} */ /* end of BM_pool_init_grp group */


/**************************************************************************//**
 @Group         BM_pool_runtime_control_grp BM-Pool Runtime Control Unit

 @Description   BM-Pool Runtime control unit API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   enum for defining BM Pool counters
*//***************************************************************************/
typedef enum e_BmPoolCounters {
    e_BM_POOL_COUNTERS_CONTENT = 0,         /**< number of free buffers for a particular pool */
    e_BM_POOL_COUNTERS_SW_DEPLETION,        /**< number of times pool entered sw depletion */
    e_BM_POOL_COUNTERS_HW_DEPLETION         /**< number of times pool entered hw depletion */
} e_BmPoolCounters;

/**************************************************************************//**
 @Function      BM_POOL_GetId

 @Description   return a buffer pool id.

 @Param[in]     h_BmPool    - A handle to a BM-pool

 @Return        Pool ID.
*//***************************************************************************/
uint8_t BM_POOL_GetId(t_Handle h_BmPool);

/**************************************************************************//**
 @Function      BM_POOL_GetBufferSize

 @Description   returns the pool's buffer size.

 @Param[in]     h_BmPool    - A handle to a BM-pool

 @Return        pool's buffer size.
*//***************************************************************************/
uint16_t BM_POOL_GetBufferSize(t_Handle h_BmPool);

/**************************************************************************//**
 @Function      BM_POOL_GetBufferContext

 @Description   Returns the user's private context that
                should be associated with the buffer.

 @Param[in]     h_BmPool    - A handle to a BM-pool
 @Param[in]     p_Buff      - A Pointer to the buffer

 @Return        user's private context.
*//***************************************************************************/
t_Handle BM_POOL_GetBufferContext(t_Handle h_BmPool, void *p_Buff);

/**************************************************************************//**
 @Function      BM_POOL_GetCounter

 @Description   Reads one of the BM Pool counters.

 @Param[in]     h_BmPool    - A handle to a BM-pool
 @Param[in]     counter     - The requested counter.

 @Return        Counter's current value.
*//***************************************************************************/
uint32_t BM_POOL_GetCounter(t_Handle h_BmPool, e_BmPoolCounters counter);

/** @} */ /* end of BM_pool_runtime_control_grp group */


/**************************************************************************//**
 @Group         BM_pool_runtime_data_grp BM-Pool Runtime Data Unit

 @Description   BM-Pool Runtime data unit API functions, definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      BM_POOL_GetBuf

 @Description   Allocate buffer from a buffer pool.

 @Param[in]     h_BmPool    - A handle to a BM-pool
 @Param[in]     h_BmPortal  - A handle to a BM Portal Module;
                              NOTE : if NULL, assuming affinity.

 @Return        A Pointer to the allocated buffer.
*//***************************************************************************/
void *      BM_POOL_GetBuf(t_Handle h_BmPool, t_Handle h_BmPortal);

/**************************************************************************//**
 @Function      BM_POOL_PutBuf

 @Description   Deallocate buffer to a buffer pool.

 @Param[in]     h_BmPool    - A handle to a BM-pool
 @Param[in]     h_BmPortal  - A handle to a BM Portal Module;
                              NOTE : if NULL, assuming affinity.
 @Param[in]     p_Buff      - A Pointer to the buffer.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error     BM_POOL_PutBuf(t_Handle h_BmPool, t_Handle h_BmPortal, void *p_Buff);

/**************************************************************************//**
 @Function      BM_POOL_FillBufs

 @Description   Fill a BM pool with new buffers.

 @Param[in]     h_BmPool    - A handle to a BM-pool
 @Param[in]     h_BmPortal  - A handle to a BM Portal Module;
                              NOTE : if NULL, assuming affinity.
 @Param[in]     numBufs     - How many buffers to fill into the pool.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error     BM_POOL_FillBufs(t_Handle h_BmPool, t_Handle h_BmPortal, uint32_t numBufs);

/** @} */ /* end of BM_pool_runtime_data_grp group */
/** @} */ /* end of BM_pool_grp group */
/** @} */ /* end of BM_grp group */

#endif /* __BM_EXT_H */
