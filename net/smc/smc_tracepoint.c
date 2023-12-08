// SPDX-License-Identifier: GPL-2.0-only

#define CREATE_TRACE_POINTS
#include "smc_tracepoint.h"

EXPORT_TRACEPOINT_SYMBOL(smc_switch_to_fallback);
EXPORT_TRACEPOINT_SYMBOL(smc_tx_sendmsg);
EXPORT_TRACEPOINT_SYMBOL(smc_rx_recvmsg);
EXPORT_TRACEPOINT_SYMBOL(smcr_link_down);
