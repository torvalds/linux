/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef MOD_HDCP_LOG_H_
#define MOD_HDCP_LOG_H_

#define HDCP_LOG_ERR(hdcp, ...) DRM_DEBUG_KMS(__VA_ARGS__)
#define HDCP_LOG_VER(hdcp, ...) DRM_DEBUG_KMS(__VA_ARGS__)
#define HDCP_LOG_FSM(hdcp, ...) DRM_DEBUG_KMS(__VA_ARGS__)
#define HDCP_LOG_TOP(hdcp, ...) pr_debug("[HDCP_TOP]:"__VA_ARGS__)
#define HDCP_LOG_DDC(hdcp, ...) pr_debug("[HDCP_DDC]:"__VA_ARGS__)

/* default logs */
#define HDCP_ERROR_TRACE(hdcp, status) \
		HDCP_LOG_ERR(hdcp, \
			"[Link %d] WARNING %s IN STATE %s STAY COUNT %d", \
			hdcp->config.index, \
			mod_hdcp_status_to_str(status), \
			mod_hdcp_state_id_to_str(hdcp->state.id), \
			hdcp->state.stay_count)
#define HDCP_HDCP1_ENABLED_TRACE(hdcp, displayIndex) \
		HDCP_LOG_VER(hdcp, \
			"[Link %d] HDCP 1.4 enabled on display %d", \
			hdcp->config.index, displayIndex)
#define HDCP_HDCP2_ENABLED_TRACE(hdcp, displayIndex) \
		HDCP_LOG_VER(hdcp, \
			"[Link %d] HDCP 2.2 enabled on display %d", \
			hdcp->config.index, displayIndex)
#define HDCP_HDCP1_DISABLED_TRACE(hdcp, displayIndex) \
		HDCP_LOG_VER(hdcp, \
			"[Link %d] HDCP 1.4 disabled on display %d", \
			hdcp->config.index, displayIndex)
#define HDCP_HDCP2_DISABLED_TRACE(hdcp, displayIndex) \
		HDCP_LOG_VER(hdcp, \
			"[Link %d] HDCP 2.2 disabled on display %d", \
			hdcp->config.index, displayIndex)

/* state machine logs */
#define HDCP_REMOVE_DISPLAY_TRACE(hdcp, displayIndex) \
		HDCP_LOG_FSM(hdcp, \
			"[Link %d] HDCP_REMOVE_DISPLAY index %d", \
			hdcp->config.index, displayIndex)
#define HDCP_INPUT_PASS_TRACE(hdcp, str) \
		HDCP_LOG_FSM(hdcp, \
			"[Link %d]\tPASS %s", \
			hdcp->config.index, str)
#define HDCP_INPUT_FAIL_TRACE(hdcp, str) \
		HDCP_LOG_FSM(hdcp, \
			"[Link %d]\tFAIL %s", \
			hdcp->config.index, str)
#define HDCP_NEXT_STATE_TRACE(hdcp, id, output) do { \
		if (output->watchdog_timer_needed) \
			HDCP_LOG_FSM(hdcp, \
				"[Link %d] > %s with %d ms watchdog", \
				hdcp->config.index, \
				mod_hdcp_state_id_to_str(id), output->watchdog_timer_delay); \
		else \
			HDCP_LOG_FSM(hdcp, \
				"[Link %d] > %s", hdcp->config.index, \
				mod_hdcp_state_id_to_str(id)); \
} while (0)
#define HDCP_TIMEOUT_TRACE(hdcp) \
		HDCP_LOG_FSM(hdcp, "[Link %d] --> TIMEOUT", hdcp->config.index)
#define HDCP_CPIRQ_TRACE(hdcp) \
		HDCP_LOG_FSM(hdcp, "[Link %d] --> CPIRQ", hdcp->config.index)
#define HDCP_EVENT_TRACE(hdcp, event) \
		do { \
			if (event == MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) \
				HDCP_TIMEOUT_TRACE(hdcp); \
			else if (event == MOD_HDCP_EVENT_CPIRQ) \
				HDCP_CPIRQ_TRACE(hdcp);	\
		} while (0)
/* TODO: find some way to tell if logging is off to save time */
#define HDCP_DDC_READ_TRACE(hdcp, msg_name, msg, msg_size) do { \
		mod_hdcp_dump_binary_message(msg, msg_size, hdcp->buf, \
				sizeof(hdcp->buf)); \
		HDCP_LOG_DDC(hdcp, "[Link %d] Read %s%s", hdcp->config.index, \
				msg_name, hdcp->buf); \
} while (0)
#define HDCP_DDC_WRITE_TRACE(hdcp, msg_name, msg, msg_size) do { \
		mod_hdcp_dump_binary_message(msg, msg_size, hdcp->buf, \
				sizeof(hdcp->buf)); \
		HDCP_LOG_DDC(hdcp, "[Link %d] Write %s%s", \
				hdcp->config.index, msg_name,\
				hdcp->buf); \
} while (0)
#define HDCP_TOP_ADD_DISPLAY_TRACE(hdcp, i) \
		HDCP_LOG_TOP(hdcp, "[Link %d]\tadd display %d", \
				hdcp->config.index, i)
#define HDCP_TOP_REMOVE_DISPLAY_TRACE(hdcp, i) \
		HDCP_LOG_TOP(hdcp, "[Link %d]\tremove display %d", \
				hdcp->config.index, i)
#define HDCP_TOP_HDCP1_DESTROY_SESSION_TRACE(hdcp) \
		HDCP_LOG_TOP(hdcp, "[Link %d]\tdestroy hdcp1 session", \
				hdcp->config.index)
#define HDCP_TOP_HDCP2_DESTROY_SESSION_TRACE(hdcp) \
		HDCP_LOG_TOP(hdcp, "[Link %d]\tdestroy hdcp2 session", \
				hdcp->config.index)
#define HDCP_TOP_RESET_AUTH_TRACE(hdcp) \
		HDCP_LOG_TOP(hdcp, "[Link %d]\treset authentication", hdcp->config.index)
#define HDCP_TOP_RESET_CONN_TRACE(hdcp) \
		HDCP_LOG_TOP(hdcp, "[Link %d]\treset connection", hdcp->config.index)
#define HDCP_TOP_INTERFACE_TRACE(hdcp) do { \
		HDCP_LOG_TOP(hdcp, "\n"); \
		HDCP_LOG_TOP(hdcp, "[Link %d] %s", hdcp->config.index, __func__); \
} while (0)
#define HDCP_TOP_INTERFACE_TRACE_WITH_INDEX(hdcp, i) do { \
		HDCP_LOG_TOP(hdcp, "\n"); \
		HDCP_LOG_TOP(hdcp, "[Link %d] %s display %d", hdcp->config.index, __func__, i); \
} while (0)

#endif // MOD_HDCP_LOG_H_
