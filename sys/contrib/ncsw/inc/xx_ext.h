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
 @File          xx_ext.h

 @Description   Prototypes, externals and typedefs for system-supplied
                (external) routines
*//***************************************************************************/

#ifndef __XX_EXT_H
#define __XX_EXT_H

#include "std_ext.h"
#include "xx_common.h"
#include "part_ext.h"



/**************************************************************************//**
 @Group         xx_id  XX Interface (System call hooks)

 @Description   Prototypes, externals and typedefs for system-supplied
                (external) routines

 @{
*//***************************************************************************/

#ifdef DEBUG_XX_MALLOC
void * XX_MallocDebug(uint32_t size, char *fname, int line);

void * XX_MallocSmartDebug(uint32_t size,
                           int      memPartitionId,
                           uint32_t alignment,
                           char     *fname,
                           int      line);

#define XX_Malloc(sz) \
    XX_MallocDebug((sz), __FILE__, __LINE__)

#define XX_MallocSmart(sz, memt, al) \
    XX_MallocSmartDebug((sz), (memt), (al), __FILE__, __LINE__)

#else /* not DEBUG_XX_MALLOC */
/**************************************************************************//**
 @Function      XX_Malloc

 @Description   allocates contiguous block of memory.

 @Param[in]     size - Number of bytes to allocate.

 @Return        The address of the newly allocated block on success, NULL on failure.
*//***************************************************************************/
void * XX_Malloc(uint32_t size);

/**************************************************************************//**
 @Function      XX_MallocSmart

 @Description   Allocates contiguous block of memory in a specified
                alignment and from the specified segment.

 @Param[in]     size            - Number of bytes to allocate.
 @Param[in]     memPartitionId  - Memory partition ID; The value zero must
                                  be mapped to the default heap partition.
 @Param[in]     alignment       - Required memory alignment (in bytes).

 @Return        The address of the newly allocated block on success, NULL on failure.
*//***************************************************************************/
void * XX_MallocSmart(uint32_t size, int memPartitionId, uint32_t alignment);

int XX_MallocSmartInit(void);
#endif /* not DEBUG_XX_MALLOC */

/**************************************************************************//**
 @Function      XX_FreeSmart

 @Description   Frees the memory block pointed to by "p".
                Only for memory allocated by XX_MallocSmart

 @Param[in]     p_Memory - pointer to the memory block.

 @Return        None.
*//***************************************************************************/
void XX_FreeSmart(void *p_Memory);

/**************************************************************************//**
 @Function      XX_Free

 @Description   frees the memory block pointed to by "p".

 @Param[in]     p_Memory - pointer to the memory block.

 @Return        None.
*//***************************************************************************/
void XX_Free(void *p_Memory);

/**************************************************************************//**
 @Function      XX_Print

 @Description   print a string.

 @Param[in]     str - string to print.

 @Return        None.
*//***************************************************************************/
void XX_Print(char *str, ...);

/**************************************************************************//**
 @Function      XX_SetIntr

 @Description   Set an interrupt service routine for a specific interrupt source.

 @Param[in]     irq     - Interrupt ID (system-specific number).
 @Param[in]     f_Isr   - Callback routine that will be called when the interrupt occurs.
 @Param[in]     handle  - The argument for the user callback routine.

 @Return        E_OK on success; error code otherwise..
*//***************************************************************************/
t_Error XX_SetIntr(uintptr_t irq, t_Isr *f_Isr, t_Handle handle);

/**************************************************************************//**
 @Function      XX_FreeIntr

 @Description   Free a specific interrupt and a specific callback routine.

 @Param[in]     irq - Interrupt ID (system-specific number).

 @Return        E_OK on success; error code otherwise..
*//***************************************************************************/
t_Error XX_FreeIntr(uintptr_t irq);

/**************************************************************************//**
 @Function      XX_EnableIntr

 @Description   Enable a specific interrupt.

 @Param[in]     irq - Interrupt ID (system-specific number).

 @Return        E_OK on success; error code otherwise..
*//***************************************************************************/
t_Error XX_EnableIntr(uintptr_t irq);

/**************************************************************************//**
 @Function      XX_DisableIntr

 @Description   Disable a specific interrupt.

 @Param[in]     irq - Interrupt ID (system-specific number).

 @Return        E_OK on success; error code otherwise..
*//***************************************************************************/
t_Error XX_DisableIntr(uintptr_t irq);

/**************************************************************************//**
 @Function      XX_DisableAllIntr

 @Description   Disable all interrupts by masking them at the CPU.

 @Return        A value that represents the interrupts state before the
                operation, and should be passed to the matching
                XX_RestoreAllIntr() call.
*//***************************************************************************/
uint32_t XX_DisableAllIntr(void);

/**************************************************************************//**
 @Function      XX_RestoreAllIntr

 @Description   Restore previous state of interrupts level at the CPU.

 @Param[in]     flags - A value that represents the interrupts state to restore,
                        as returned by the matching call for XX_DisableAllIntr().

 @Return        None.
*//***************************************************************************/
void XX_RestoreAllIntr(uint32_t flags);


t_Error XX_PreallocAndBindIntr(uintptr_t irq, unsigned int cpu);
t_Error XX_DeallocIntr(uintptr_t irq);

/**************************************************************************//**
 @Function      XX_Exit

 @Description   Stop execution and report status (where it is applicable)

 @Param[in]     status - exit status
*//***************************************************************************/
void    XX_Exit(int status);


/*****************************************************************************/
/*                        Tasklet Service Routines                           */
/*****************************************************************************/
typedef t_Handle t_TaskletHandle;

/**************************************************************************//**
 @Function      XX_InitTasklet

 @Description   Create and initialize a tasklet object.

 @Param[in]     routine - A routine to be ran as a tasklet.
 @Param[in]     data    - An argument to pass to the tasklet.

 @Return        Tasklet handle is returned on success. NULL is returned otherwise.
*//***************************************************************************/
t_TaskletHandle XX_InitTasklet (void (*routine)(void *), void *data);

/**************************************************************************//**
 @Function      XX_FreeTasklet

 @Description   Free a tasklet object.

 @Param[in]     h_Tasklet - A handle to a tasklet to be free.

 @Return        None.
*//***************************************************************************/
void XX_FreeTasklet (t_TaskletHandle h_Tasklet);

/**************************************************************************//**
 @Function      XX_ScheduleTask

 @Description   Schedule a tasklet object.

 @Param[in]     h_Tasklet - A handle to a tasklet to be scheduled.
 @Param[in]     immediate - Indicate whether to schedule this tasklet on
                            the immediate queue or on the delayed one.

 @Return        0 - on success. Error code - otherwise.
*//***************************************************************************/
int XX_ScheduleTask(t_TaskletHandle h_Tasklet, int immediate);

/**************************************************************************//**
 @Function      XX_FlushScheduledTasks

 @Description   Flush all tasks there are in the scheduled tasks queue.

 @Return        None.
*//***************************************************************************/
void XX_FlushScheduledTasks(void);

/**************************************************************************//**
 @Function      XX_TaskletIsQueued

 @Description   Check if task is queued.

 @Param[in]     h_Tasklet - A handle to a tasklet to be scheduled.

 @Return        1 - task is queued. 0 - otherwise.
*//***************************************************************************/
int XX_TaskletIsQueued(t_TaskletHandle h_Tasklet);

/**************************************************************************//**
 @Function      XX_SetTaskletData

 @Description   Set data to a scheduled task. Used to change data of already
                scheduled task.

 @Param[in]     h_Tasklet - A handle to a tasklet to be scheduled.
 @Param[in]     data      - Data to be set.
*//***************************************************************************/
void XX_SetTaskletData(t_TaskletHandle h_Tasklet, t_Handle data);

/**************************************************************************//**
 @Function      XX_GetTaskletData

 @Description   Get the data of scheduled task.

 @Param[in]     h_Tasklet - A handle to a tasklet to be scheduled.

 @Return        handle to the data of the task.
*//***************************************************************************/
t_Handle XX_GetTaskletData(t_TaskletHandle h_Tasklet);

/**************************************************************************//**
 @Function      XX_BottomHalf

 @Description   Bottom half implementation, invoked by the interrupt handler.

                This routine handles all bottom-half tasklets with interrupts
                enabled.

 @Return        None.
*//***************************************************************************/
void XX_BottomHalf(void);


/*****************************************************************************/
/*                        Spinlock Service Routines                          */
/*****************************************************************************/

/**************************************************************************//**
 @Function      XX_InitSpinlock

 @Description   Creates a spinlock.

 @Return        Spinlock handle is returned on success; NULL otherwise.
*//***************************************************************************/
t_Handle XX_InitSpinlock(void);

/**************************************************************************//**
 @Function      XX_FreeSpinlock

 @Description   Frees the memory allocated for the spinlock creation.

 @Param[in]     h_Spinlock - A handle to a spinlock.

 @Return        None.
*//***************************************************************************/
void XX_FreeSpinlock(t_Handle h_Spinlock);

/**************************************************************************//**
 @Function      XX_LockSpinlock

 @Description   Locks a spinlock.

 @Param[in]     h_Spinlock - A handle to a spinlock.

 @Return        None.
*//***************************************************************************/
void XX_LockSpinlock(t_Handle h_Spinlock);

/**************************************************************************//**
 @Function      XX_UnlockSpinlock

 @Description   Unlocks a spinlock.

 @Param[in]     h_Spinlock - A handle to a spinlock.

 @Return        None.
*//***************************************************************************/
void XX_UnlockSpinlock(t_Handle h_Spinlock);

/**************************************************************************//**
 @Function      XX_LockIntrSpinlock

 @Description   Locks a spinlock (interrupt safe).

 @Param[in]     h_Spinlock - A handle to a spinlock.

 @Return        A value that represents the interrupts state before the
                operation, and should be passed to the matching
                XX_UnlockIntrSpinlock() call.
*//***************************************************************************/
uint32_t XX_LockIntrSpinlock(t_Handle h_Spinlock);

/**************************************************************************//**
 @Function      XX_UnlockIntrSpinlock

 @Description   Unlocks a spinlock (interrupt safe).

 @Param[in]     h_Spinlock  - A handle to a spinlock.
 @Param[in]     intrFlags   - A value that represents the interrupts state to
                              restore, as returned by the matching call for
                              XX_LockIntrSpinlock().

 @Return        None.
*//***************************************************************************/
void XX_UnlockIntrSpinlock(t_Handle h_Spinlock, uint32_t intrFlags);


/*****************************************************************************/
/*                        Timers Service Routines                            */
/*****************************************************************************/

/**************************************************************************//**
 @Function      XX_CurrentTime

 @Description   Returns current system time.

 @Return        Current system time (in milliseconds).
*//***************************************************************************/
uint32_t XX_CurrentTime(void);

/**************************************************************************//**
 @Function      XX_CreateTimer

 @Description   Creates a timer.

 @Return        Timer handle is returned on success; NULL otherwise.
*//***************************************************************************/
t_Handle XX_CreateTimer(void);

/**************************************************************************//**
 @Function      XX_FreeTimer

 @Description   Frees the memory allocated for the timer creation.

 @Param[in]     h_Timer - A handle to a timer.

 @Return        None.
*//***************************************************************************/
void XX_FreeTimer(t_Handle h_Timer);

/**************************************************************************//**
 @Function      XX_StartTimer

 @Description   Starts a timer.

                The user can select to start the timer as periodic timer or as
                one-shot timer. The user should provide a callback routine that
                will be called when the timer expires.

 @Param[in]     h_Timer         - A handle to a timer.
 @Param[in]     msecs           - Timer expiration period (in milliseconds).
 @Param[in]     periodic        - TRUE for a periodic timer;
                                  FALSE for a one-shot timer..
 @Param[in]     f_TimerExpired  - A callback routine to be called when the
                                  timer expires.
 @Param[in]     h_Arg           - The argument to pass in the timer-expired
                                  callback routine.

 @Return        None.
*//***************************************************************************/
void XX_StartTimer(t_Handle h_Timer,
                   uint32_t msecs,
                   bool     periodic,
                   void     (*f_TimerExpired)(t_Handle h_Arg),
                   t_Handle h_Arg);

/**************************************************************************//**
 @Function      XX_StopTimer

 @Description   Frees the memory allocated for the timer creation.

 @Param[in]     h_Timer - A handle to a timer.

 @Return        None.
*//***************************************************************************/
void XX_StopTimer(t_Handle h_Timer);

/**************************************************************************//**
 @Function      XX_ModTimer

 @Description   Updates the expiration time of a timer.

                This routine adds the given time to the current system time,
                and sets this value as the new expiration time of the timer.

 @Param[in]     h_Timer - A handle to a timer.
 @Param[in]     msecs   - The new interval until timer expiration
                          (in milliseconds).

 @Return        None.
*//***************************************************************************/
void XX_ModTimer(t_Handle h_Timer, uint32_t msecs);

/**************************************************************************//**
 @Function      XX_Sleep

 @Description   Non-busy wait until the desired time (in milliseconds) has passed.

 @Param[in]     msecs - The requested sleep time (in milliseconds).

 @Return        Zero if the requested time has elapsed; Otherwise, the value
                returned will be the unslept amount) in milliseconds.

 @Cautions      This routine enables interrupts during its wait time.
*//***************************************************************************/
uint32_t XX_Sleep(uint32_t msecs);

/**************************************************************************//**
 @Function      XX_UDelay

 @Description   Busy-wait until the desired time (in microseconds) has passed.

 @Param[in]     usecs - The requested delay time (in microseconds).

 @Return        None.

 @Cautions      It is highly unrecommended to call this routine during interrupt
                time, because the system time may not be updated properly during
                the delay loop. The behavior of this routine during interrupt
                time is unexpected.
*//***************************************************************************/
void XX_UDelay(uint32_t usecs);


/*****************************************************************************/
/*                         Other Service Routines                            */
/*****************************************************************************/

/**************************************************************************//**
 @Function      XX_PhysToVirt

 @Description   Translates a physical address to the matching virtual address.

 @Param[in]     addr - The physical address to translate.

 @Return        Virtual address.
*//***************************************************************************/
void * XX_PhysToVirt(physAddress_t addr);

/**************************************************************************//**
 @Function      XX_VirtToPhys

 @Description   Translates a virtual address to the matching physical address.

 @Param[in]     addr - The virtual address to translate.

 @Return        Physical address.
*//***************************************************************************/
physAddress_t XX_VirtToPhys(void *addr);


/**************************************************************************//**
 @Group         xx_ipc  XX Inter-Partition-Communication API

 @Description   The following API is to be used when working with multiple
                partitions configuration.

 @{
*//***************************************************************************/

#define XX_IPC_MAX_ADDR_NAME_LENGTH 16         /**< Maximum length of an endpoint name string;
                                                    The IPC service can use this constant to limit
                                                    the storage space for IPC endpoint names. */


/**************************************************************************//**
 @Function      t_IpcMsgCompletion

 @Description   Callback function used upon IPC non-blocking transaction completion
                to return message buffer to the caller and to forward reply if available.

                This callback function may be attached by the source endpoint to any outgoing
                IPC message to indicate a non-blocking send (see also XX_IpcSendMessage() routine).
                Upon completion of an IPC transaction (consisting of a message and an optional reply),
                the IPC service invokes this callback routine to return the message buffer to the sender
                and to provide the received reply, if requested.

                User provides this function. Driver invokes it.

 @Param[in]     h_Module        - Abstract handle to the sending module -  the same handle as was passed
                                  in the XX_IpcSendMessage() function; This handle is typically used to point
                                  to the internal data structure of the source endpoint.
 @Param[in]     p_Msg           - Pointer to original (sent) message buffer;
                                  The source endpoint can free (or reuse) this buffer when message
                                  completion callback is called.
 @Param[in]     p_Reply         - Pointer to (received) reply buffer;
                                  This pointer is the same as was provided by the source endpoint in
                                  XX_IpcSendMessage().
 @Param[in]     replyLength     - Length (in bytes) of actual data in the reply buffer.
 @Param[in]     status          - Completion status - E_OK or failure indication, e.g. IPC transaction completion
                                  timeout.

 @Return        None
 *//***************************************************************************/
typedef void    (t_IpcMsgCompletion)(t_Handle   h_Module,
                                     uint8_t    *p_Msg,
                                     uint8_t    *p_Reply,
                                     uint32_t   replyLength,
                                     t_Error    status);

/**************************************************************************//**
 @Function      t_IpcMsgHandler

 @Description   Callback function used as IPC message handler.

                The IPC service invokes message handlers for each IPC message received.
                The actual function pointer should be registered by each destination endpoint
                via the XX_IpcRegisterMsgHandler() routine.

                User provides this function. Driver invokes it.

 @Param[in]     h_Module        - Abstract handle to the message handling module -  the same handle as
                                  was passed in the XX_IpcRegisterMsgHandler() function; this handle is
                                  typically used to point to the internal data structure of the destination
                                  endpoint.
 @Param[in]     p_Msg           - Pointer to message buffer with data received from peer.
 @Param[in]     msgLength       - Length (in bytes) of message data.
 @Param[in]     p_Reply         - Pointer to reply buffer, to be filled by the message handler and then sent
                                  by the IPC service;
                                  The reply buffer is allocated by the IPC service with size equals to the
                                  replyLength parameter provided in message handler registration (see
                                  XX_IpcRegisterMsgHandler() function);
                                  If replyLength was initially specified as zero during message handler registration,
                                  the IPC service may set this pointer to NULL and assume that a reply is not needed;
                                  The IPC service is also responsible for freeing the reply buffer after the
                                  reply has been sent or dismissed.
 @Param[in,out] p_ReplyLength   - Pointer to reply length, which has a dual role in this function:
                                  [In] equals the replyLength parameter provided in message handler
                                  registration (see XX_IpcRegisterMsgHandler() function), and
                                  [Out] should be updated by message handler to the actual reply length; if
                                  this value is set to zero, the IPC service must assume that a reply should
                                  not be sent;
                                  Note: If p_Reply is not NULL, p_ReplyLength must not be NULL as well.

 @Return        E_OK on success; Error code otherwise.
 *//***************************************************************************/
typedef t_Error (t_IpcMsgHandler)(t_Handle  h_Module,
                                  uint8_t   *p_Msg,
                                  uint32_t  msgLength,
                                  uint8_t   *p_Reply,
                                  uint32_t  *p_ReplyLength);

/**************************************************************************//**
 @Function      XX_IpcRegisterMsgHandler

 @Description   IPC mailbox registration.

                This function is used for registering an IPC message handler in the IPC service.
                This function is called by each destination endpoint to indicate that it is ready
                to handle incoming messages. The IPC service invokes the message handler upon receiving
                a message addressed to the specified destination endpoint.

 @Param[in]     addr                - The address name string associated with the destination endpoint;
                                      This address must be unique across the IPC service domain to ensure
                                      correct message routing.
 @Param[in]     f_MsgHandler        - Pointer to the message handler callback for processing incoming
                                      message; invoked by the IPC service upon receiving a message
                                      addressed to the destination endpoint specified by the addr
                                      parameter.
 @Param[in]     h_Module            - Abstract handle to the message handling module, passed unchanged
                                      to f_MsgHandler callback function.
 @Param[in]     replyLength         - The maximal data length (in bytes) of any reply that the specified message handler
                                      may generate; the IPC service provides the message handler with buffer
                                      for reply according to the length specified here (refer also to the description
                                      of #t_IpcMsgHandler callback function type);
                                      This size shall be zero if the message handler never generates replies.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error XX_IpcRegisterMsgHandler(char                   addr[XX_IPC_MAX_ADDR_NAME_LENGTH],
                                 t_IpcMsgHandler        *f_MsgHandler,
                                 t_Handle               h_Module,
                                 uint32_t               replyLength);

/**************************************************************************//**
 @Function      XX_IpcUnregisterMsgHandler

 @Description   Release IPC mailbox routine.

                 This function is used for unregistering an IPC message handler from the IPC service.
                 This function is called by each destination endpoint to indicate that it is no longer
                 capable of handling incoming messages.

 @Param[in]     addr          - The address name string associated with the destination endpoint;
                                This address is the same as was used when the message handler was
                                registered via XX_IpcRegisterMsgHandler().

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error XX_IpcUnregisterMsgHandler(char addr[XX_IPC_MAX_ADDR_NAME_LENGTH]);

/**************************************************************************//**
 @Function      XX_IpcInitSession

 @Description   This function is used for creating an IPC session between the source endpoint
                and the destination endpoint.

                The actual implementation and representation of a session is left for the IPC service.
                The function returns an abstract handle to the created session. This handle shall be used
                by the source endpoint in subsequent calls to XX_IpcSendMessage().
                The IPC service assumes that before this function is called, no messages are sent from
                the specified source endpoint to the specified destination endpoint.

                The IPC service may use a connection-oriented approach or a connectionless approach (or both)
                as described below.

                @par Connection-Oriented Approach

                The IPC service may implement a session in a connection-oriented approach -  when this function is called,
                the IPC service should take the necessary steps to bring up a source-to-destination channel for messages
                and a destination-to-source channel for replies. The returned handle should represent the internal
                representation of these channels.

                @par Connectionless Approach

                The IPC service may implement a session in a connectionless approach -  when this function is called, the
                IPC service should not perform any particular steps, but it must store the pair of source and destination
                addresses in some session representation and return it as a handle. When XX_IpcSendMessage() shall be
                called, the IPC service may use this handle to provide the necessary identifiers for routing the messages
                through the connectionless medium.

 @Param[in]     destAddr      - The address name string associated with the destination endpoint.
 @Param[in]     srcAddr       - The address name string associated with the source endpoint.

 @Return        Abstract handle to the initialized session, or NULL on error.
*//***************************************************************************/
t_Handle XX_IpcInitSession(char destAddr[XX_IPC_MAX_ADDR_NAME_LENGTH],
                           char srcAddr[XX_IPC_MAX_ADDR_NAME_LENGTH]);

/**************************************************************************//**
 @Function      XX_IpcFreeSession

 @Description   This function is used for terminating an existing IPC session between a source endpoint
                and a destination endpoint.

                The IPC service assumes that after this function is called, no messages shall be sent from
                the associated source endpoint to the associated destination endpoint.

 @Param[in]     h_Session      - Abstract handle to the IPC session -  the same handle as was originally
                                 returned by the XX_IpcInitSession() function.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error XX_IpcFreeSession(t_Handle h_Session);

/**************************************************************************//**
 @Function      XX_IpcSendMessage

 @Description   IPC message send routine.

                This function may be used by a source endpoint to send an IPC message to a destination
                endpoint. The source endpoint cannot send a message to the destination endpoint without
                first initiating a session with that destination endpoint via XX_IpcInitSession() routine.

                The source endpoint must provide the buffer pointer and length of the outgoing message.
                Optionally, it may also provide a buffer for an expected reply. In the latter case, the
                transaction is not considered complete by the IPC service until the reply has been received.
                If the source endpoint does not provide a reply buffer, the transaction is considered
                complete after the message has been sent. The source endpoint must keep the message (and
                optional reply) buffers valid until the transaction is complete.

                @par Non-blocking mode

                The source endpoint may request a non-blocking send by providing a non-NULL pointer to a message
                completion callback function (f_Completion). Upon completion of the IPC transaction (consisting of a
                message and an optional reply), the IPC service invokes this callback routine to return the message
                buffer to the sender and to provide the received reply, if requested.

                @par Blocking mode

                The source endpoint may request a blocking send by setting f_Completion to NULL. The function is
                expected to block until the IPC transaction is complete -  either the reply has been received or (if no reply
                was requested) the message has been sent.

 @Param[in]     h_Session       - Abstract handle to the IPC session -  the same handle as was originally
                                  returned by the XX_IpcInitSession() function.
 @Param[in]     p_Msg           - Pointer to message buffer to send.
 @Param[in]     msgLength       - Length (in bytes) of actual data in the message buffer.
 @Param[in]     p_Reply         - Pointer to reply buffer -  if this buffer is not NULL, the IPC service
                                  fills this buffer with the received reply data;
                                  In blocking mode, the reply data must be valid when the function returns;
                                  In non-blocking mode, the reply data is valid when f_Completion is called;
                                  If this pointer is NULL, no reply is expected.
 @Param[in,out] p_ReplyLength   - Pointer to reply length, which has a dual role in this function:
                                  [In] specifies the maximal length (in bytes) of the reply buffer pointed by
                                  p_Reply, and
                                  [Out] in non-blocking mode this value is updated by the IPC service to the
                                  actual reply length (in bytes).
 @Param[in]     f_Completion    - Pointer to a completion callback to be used in non-blocking send mode;
                                  The completion callback is invoked by the IPC service upon
                                  completion of the IPC transaction (consisting of a message and an optional
                                  reply);
                                  If this pointer is NULL, the function is expected to block until the IPC
                                  transaction is complete.
 @Param[in]     h_Arg           - Abstract handle to the sending module; passed unchanged to the f_Completion
                                  callback function as the first argument.

 @Return        E_OK on success; Error code otherwise.
*//***************************************************************************/
t_Error XX_IpcSendMessage(t_Handle           h_Session,
                          uint8_t            *p_Msg,
                          uint32_t           msgLength,
                          uint8_t            *p_Reply,
                          uint32_t           *p_ReplyLength,
                          t_IpcMsgCompletion *f_Completion,
                          t_Handle           h_Arg);


/** @} */ /* end of xx_ipc group */
/** @} */ /* end of xx_id group */


void XX_PortalSetInfo(device_t dev);
void XX_FmanFixIntr(int irq);
#endif /* __XX_EXT_H */
