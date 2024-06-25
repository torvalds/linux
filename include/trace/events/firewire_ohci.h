// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2024 Takashi Sakamoto

#undef TRACE_SYSTEM
#define TRACE_SYSTEM	firewire_ohci

#if !defined(_FIREWIRE_OHCI_TRACE_EVENT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _FIREWIRE_OHCI_TRACE_EVENT_H

#include <linux/tracepoint.h>

TRACE_EVENT(irqs,
	TP_PROTO(unsigned int card_index, u32 events),
	TP_ARGS(card_index, events),
	TP_STRUCT__entry(
		__field(u8, card_index)
		__field(u32, events)
	),
	TP_fast_assign(
		__entry->card_index = card_index;
		__entry->events = events;
	),
	TP_printk(
		"card_index=%u events=%s",
		__entry->card_index,
		__print_flags(__entry->events, "|",
			{ OHCI1394_selfIDComplete,	"selfIDComplete" },
			{ OHCI1394_RQPkt,		"RQPkt" },
			{ OHCI1394_RSPkt,		"RSPkt" },
			{ OHCI1394_reqTxComplete,	"reqTxComplete" },
			{ OHCI1394_respTxComplete,	"respTxComplete" },
			{ OHCI1394_isochRx,		"isochRx" },
			{ OHCI1394_isochTx,		"isochTx" },
			{ OHCI1394_postedWriteErr,	"postedWriteErr" },
			{ OHCI1394_cycleTooLong,	"cycleTooLong" },
			{ OHCI1394_cycle64Seconds,	"cycle64Seconds" },
			{ OHCI1394_cycleInconsistent,	"cycleInconsistent" },
			{ OHCI1394_regAccessFail,	"regAccessFail" },
			{ OHCI1394_unrecoverableError,	"unrecoverableError" },
			{ OHCI1394_busReset,		"busReset" }
		)
	)
);

#endif // _FIREWIRE_OHCI_TRACE_EVENT_H

#include <trace/define_trace.h>
