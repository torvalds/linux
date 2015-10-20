/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef _TEE_KERNEL_API_H
#define _TEE_KERNEL_API_H

#include <linux/tee_client_api.h>

/**
 * struct TEEC_Context - Represents a connection between a client application
 * and a TEE.
 */
/*typedef struct {
	char devname[256];
} TEEC_Context;*/

/**
 * struct TEEC_Session - Represents a connection between a client application
 * and a trusted application.
 */
/*typedef struct {
	void *session;
} TEEC_Session;*/

/**
 * TEEC_InitializeContext() - Initializes a context holding connection
 * information on the specific TEE, designated by the name string.

 * @param name    A zero-terminated string identifying the TEE to connect to.
 *                If name is set to NULL, the default TEE is connected to. NULL
 *                is the only supported value in this version of the API
 *                implementation.
 *
 * @param context The context structure which is to be initialized.
 *
 * @return TEEC_SUCCESS  The initialization was successful.
 * @return TEEC_Result   Something failed.
 */
TEEC_Result TEEC_InitializeContext(const char *name, TEEC_Context *context);

/**
 * TEEC_FinalizeContext() - Destroys a context holding connection information
 * on the specific TEE.
 *
 * This function destroys an initialized TEE context, closing the connection
 * between the client application and the TEE. This function must only be
 * called when all sessions related to this TEE context have been closed and
 * all shared memory blocks have been released.
 *
 * @param context       The context to be destroyed.
 */
void TEEC_FinalizeContext(TEEC_Context *context);

/**
 * TEEC_OpenSession() - Opens a new session with the specified trusted
 *                      application.
 *
 * @param context            The initialized TEE context structure in which
 *                           scope to open the session.
 * @param session            The session to initialize.
 * @param destination        A structure identifying the trusted application
 *                           with which to open a session.
 *
 * @param connectionMethod   The connection method to use.
 * @param connectionData     Any data necessary to connect with the chosen
 *                           connection method. Not supported, should be set to
 *                           NULL.
 * @param operation          An operation structure to use in the session. May
 *                           be set to NULL to signify no operation structure
 *                           needed.
 *
 * @param returnOrigin       A parameter which will hold the error origin if
 *                           this function returns any value other than
 *                           TEEC_SUCCESS.
 *
 * @return TEEC_SUCCESS      OpenSession successfully opened a new session.
 * @return TEEC_Result       Something failed.
 *
 */
TEEC_Result TEEC_OpenSession(TEEC_Context *context,
			     TEEC_Session *session,
			     const TEEC_UUID *destination,
			     uint32_t connectionMethod,
			     const void *connectionData,
			     TEEC_Operation *operation,
			     uint32_t *returnOrigin);

/**
 * TEEC_CloseSession() - Closes the session which has been opened with the
 * specific trusted application.
 *
 * @param session The opened session to close.
 */
void TEEC_CloseSession(TEEC_Session *session);

/**
 * TEEC_InvokeCommand() - Executes a command in the specified trusted
 * application.
 *
 * @param session        A handle to an open connection to the trusted
 *                       application.
 * @param commandID      Identifier of the command in the trusted application
 *                       to invoke.
 * @param operation      An operation structure to use in the invoke command.
 *                       May be set to NULL to signify no operation structure
 *                       needed.
 * @param returnOrigin   A parameter which will hold the error origin if this
 *                       function returns any value other than TEEC_SUCCESS.
 *
 * @return TEEC_SUCCESS  OpenSession successfully opened a new session.
 * @return TEEC_Result   Something failed.
 */
TEEC_Result TEEC_InvokeCommand(TEEC_Session *session,
			       uint32_t commandID,
			       TEEC_Operation *operation,
			       uint32_t *returnOrigin);

/**
 * TEEC_RegisterSharedMemory() - Register a block of existing memory as a
 * shared block within the scope of the specified context.
 *
 * @param context    The initialized TEE context structure in which scope to
 *                   open the session.
 * @param sharedMem  pointer to the shared memory structure to register.
 *
 * @return TEEC_SUCCESS              The registration was successful.
 * @return TEEC_ERROR_OUT_OF_MEMORY  Memory exhaustion.
 * @return TEEC_Result               Something failed.
 */
TEEC_Result TEEC_RegisterSharedMemory(TEEC_Context *context,
				      TEEC_SharedMemory *sharedMem);

/**
 * TEEC_AllocateSharedMemory() - Allocate shared memory for TEE.
 *
 * @param context     The initialized TEE context structure in which scope to
 *                    open the session.
 * @param sharedMem   Pointer to the allocated shared memory.
 *
 * @return TEEC_SUCCESS              The registration was successful.
 * @return TEEC_ERROR_OUT_OF_MEMORY  Memory exhaustion.
 * @return TEEC_Result               Something failed.
 */
TEEC_Result TEEC_AllocateSharedMemory(TEEC_Context *context,
				      TEEC_SharedMemory *sharedMem);

/**
 * TEEC_ReleaseSharedMemory() - Free or deregister the shared memory.
 *
 * @param sharedMem  Pointer to the shared memory to be freed.
 */
void TEEC_ReleaseSharedMemory(TEEC_SharedMemory *sharedMemory);

#if 0
/**
 * TEEC_RequestCancellation() - Request the cancellation of a pending open
 *                              session or command invocation.
 *
 * @param operation Pointer to an operation previously passed to open session
 *                  or invoke.
 */
void TEEC_RequestCancellation(TEEC_Operation *operation);
#endif

#endif
