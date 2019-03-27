/**
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "vchiq_connected.h"
#include "vchiq_core.h"
#include "vchiq_killable.h"

#define  MAX_CALLBACKS  10

static   int                        g_connected;
static   int                        g_num_deferred_callbacks;
static   VCHIQ_CONNECTED_CALLBACK_T g_deferred_callback[MAX_CALLBACKS];
static   int                        g_once_init;
static   struct mutex               g_connected_mutex;

/****************************************************************************
*
* Function to initialize our lock.
*
***************************************************************************/

static void connected_init(void)
{
	if (!g_once_init) {
		lmutex_init(&g_connected_mutex);
		g_once_init = 1;
	}
}

/****************************************************************************
*
* This function is used to defer initialization until the vchiq stack is
* initialized. If the stack is already initialized, then the callback will
* be made immediately, otherwise it will be deferred until
* vchiq_call_connected_callbacks is called.
*
***************************************************************************/

void vchiq_add_connected_callback(VCHIQ_CONNECTED_CALLBACK_T callback)
{
	connected_init();

	if (lmutex_lock_interruptible(&g_connected_mutex) != 0)
		return;

	if (g_connected)
		/* We're already connected. Call the callback immediately. */

		callback();
	else {
		if (g_num_deferred_callbacks >= MAX_CALLBACKS)
			vchiq_log_error(vchiq_core_log_level,
				"There are already %d callbacks registered - "
				"please increase MAX_CALLBACKS",
				g_num_deferred_callbacks);
		else {
			g_deferred_callback[g_num_deferred_callbacks] =
				callback;
			g_num_deferred_callbacks++;
		}
	}
	lmutex_unlock(&g_connected_mutex);
}

/****************************************************************************
*
* This function is called by the vchiq stack once it has been connected to
* the videocore and clients can start to use the stack.
*
***************************************************************************/

void vchiq_call_connected_callbacks(void)
{
	int i;

	connected_init();

	if (lmutex_lock_interruptible(&g_connected_mutex) != 0)
		return;

	for (i = 0; i <  g_num_deferred_callbacks; i++)
		g_deferred_callback[i]();

	g_num_deferred_callbacks = 0;
	g_connected = 1;
	lmutex_unlock(&g_connected_mutex);
}
EXPORT_SYMBOL(vchiq_add_connected_callback);
