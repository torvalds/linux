/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/isci/isci.h>

#include <dev/isci/scil/scif_user_callback.h>

static void
isci_timer_timeout(void *arg)
{
	struct ISCI_TIMER *timer = (struct ISCI_TIMER *)arg;

	isci_log_message(3, "TIMER", "timeout %p\n", timer);

	/* callout_stop() will *not* keep the timer from running if it is
	 *  pending.  callout_drain() cannot be called from interrupt context,
	 *  because it may cause thread to sleep which is not allowed in
	 *  interrupt context.  So instead, check the is_started flag to see if
	 *  the timer routine should actually be run or not.
	 */
	if (timer->is_started == TRUE)
		timer->callback(timer->cookie);
}

/**
 * @brief This callback method asks the user to start the supplied timer.
 *
 * @warning All timers in the system started by the SCI Framework are one
 *          shot timers.  Therefore, the SCI user should make sure that it
 *          removes the timer from it's list when a timer actually fires.
 *          Additionally, SCI Framework user's should be able to handle
 *          calls from the SCI Framework to stop a timer that may already
 *          be stopped.
 *
 * @param[in]  controller This parameter specifies the controller with
 *             which this timer is to associated.
 * @param[in]  timer This parameter specifies the timer to be started.
 * @param[in]  milliseconds This parameter specifies the number of
 *             milliseconds for which to stall.  The operating system driver
 *             is allowed to round this value up where necessary.
 *
 * @return none
 */
void
scif_cb_timer_start(SCI_CONTROLLER_HANDLE_T controller, void *timer,
    uint32_t milliseconds)
{
	struct ISCI_TIMER *isci_timer = (struct ISCI_TIMER *)timer;

	isci_timer->is_started = TRUE;
	isci_log_message(3, "TIMER", "start %p %d\n", timer, milliseconds);
	callout_reset_sbt(&isci_timer->callout, SBT_1MS * milliseconds, 0,
	    isci_timer_timeout, timer, 0);
}

/**
 * @brief This callback method asks the user to stop the supplied timer.
 *
 * @param[in]  controller This parameter specifies the controller with
 *             which this timer is to associated.
 * @param[in]  timer This parameter specifies the timer to be stopped.
 *
 * @return none
 */
void
scif_cb_timer_stop(SCI_CONTROLLER_HANDLE_T controller, void *timer)
{
	struct ISCI_TIMER *isci_timer = (struct ISCI_TIMER *)timer;

	isci_log_message(3, "TIMER", "stop %p\n", timer);
	isci_timer->is_started = FALSE;
	callout_stop(&isci_timer->callout);
}

/**
 * @brief This callback method asks the user to create a timer and provide
 *        a handle for this timer for use in further timer interactions.
 *
 * @warning The "timer_callback" method should be executed in a mutually
 *          exlusive manner from the controller completion handler
 *          handler (refer to scic_controller_get_handler_methods()).
 *
 * @param[in]  timer_callback This parameter specifies the callback method
 *             to be invoked whenever the timer expires.
 * @param[in]  controller This parameter specifies the controller with
 *             which this timer is to be associated.
 * @param[in]  cookie This parameter specifies a piece of information that
 *             the user must retain.  This cookie is to be supplied by the
 *             user anytime a timeout occurs for the created timer.
 *
 * @return This method returns a handle to a timer object created by the
 *         user.  The handle will be utilized for all further interactions
 *         relating to this timer.
 */
void *
scif_cb_timer_create(SCI_CONTROLLER_HANDLE_T scif_controller,
    SCI_TIMER_CALLBACK_T timer_callback, void *cookie)
{
	struct ISCI_CONTROLLER *isci_controller = (struct ISCI_CONTROLLER *)
	    sci_object_get_association(scif_controller);
	struct ISCI_TIMER *timer;

	sci_pool_get(isci_controller->timer_pool, timer);

	callout_init_mtx(&timer->callout, &isci_controller->lock, FALSE);

	timer->callback = timer_callback;
	timer->cookie = cookie;
	timer->is_started = FALSE;

	isci_log_message(3, "TIMER", "create %p %p %p\n", timer, timer_callback, cookie);

	return (timer);
}

/**
 * @brief This callback method asks the user to destroy the supplied timer.
 *
 * @param[in]  controller This parameter specifies the controller with
 *             which this timer is to associated.
 * @param[in]  timer This parameter specifies the timer to be destroyed.
 *
 * @return none
 */
void
scif_cb_timer_destroy(SCI_CONTROLLER_HANDLE_T scif_controller,
    void *timer_handle)
{
	struct ISCI_CONTROLLER *isci_controller = (struct ISCI_CONTROLLER *)
	    sci_object_get_association(scif_controller);

	scif_cb_timer_stop(scif_controller, timer_handle);
	sci_pool_put(isci_controller->timer_pool, (struct ISCI_TIMER *)timer_handle);

	isci_log_message(3, "TIMER", "destroy %p\n", timer_handle);
}
