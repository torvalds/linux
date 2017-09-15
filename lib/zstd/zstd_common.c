/**
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of https://github.com/facebook/zstd.
 * An additional grant of patent rights can be found in the PATENTS file in the
 * same directory.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation. This program is dual-licensed; you may select
 * either version 2 of the GNU General Public License ("GPL") or BSD license
 * ("BSD").
 */

/*-*************************************
*  Dependencies
***************************************/
#include "error_private.h"
#include "zstd_internal.h" /* declaration of ZSTD_isError, ZSTD_getErrorName, ZSTD_getErrorCode, ZSTD_getErrorString, ZSTD_versionNumber */
#include <linux/kernel.h>

/*=**************************************************************
*  Custom allocator
****************************************************************/

#define stack_push(stack, size)                                 \
	({                                                      \
		void *const ptr = ZSTD_PTR_ALIGN((stack)->ptr); \
		(stack)->ptr = (char *)ptr + (size);            \
		(stack)->ptr <= (stack)->end ? ptr : NULL;      \
	})

ZSTD_customMem ZSTD_initStack(void *workspace, size_t workspaceSize)
{
	ZSTD_customMem stackMem = {ZSTD_stackAlloc, ZSTD_stackFree, workspace};
	ZSTD_stack *stack = (ZSTD_stack *)workspace;
	/* Verify preconditions */
	if (!workspace || workspaceSize < sizeof(ZSTD_stack) || workspace != ZSTD_PTR_ALIGN(workspace)) {
		ZSTD_customMem error = {NULL, NULL, NULL};
		return error;
	}
	/* Initialize the stack */
	stack->ptr = workspace;
	stack->end = (char *)workspace + workspaceSize;
	stack_push(stack, sizeof(ZSTD_stack));
	return stackMem;
}

void *ZSTD_stackAllocAll(void *opaque, size_t *size)
{
	ZSTD_stack *stack = (ZSTD_stack *)opaque;
	*size = (BYTE const *)stack->end - (BYTE *)ZSTD_PTR_ALIGN(stack->ptr);
	return stack_push(stack, *size);
}

void *ZSTD_stackAlloc(void *opaque, size_t size)
{
	ZSTD_stack *stack = (ZSTD_stack *)opaque;
	return stack_push(stack, size);
}
void ZSTD_stackFree(void *opaque, void *address)
{
	(void)opaque;
	(void)address;
}

void *ZSTD_malloc(size_t size, ZSTD_customMem customMem) { return customMem.customAlloc(customMem.opaque, size); }

void ZSTD_free(void *ptr, ZSTD_customMem customMem)
{
	if (ptr != NULL)
		customMem.customFree(customMem.opaque, ptr);
}
