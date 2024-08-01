// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2024 Takashi Sakamoto

#undef TRACE_SYSTEM
#define TRACE_SYSTEM	firewire_ohci

#if !defined(_FIREWIRE_OHCI_TRACE_EVENT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _FIREWIRE_OHCI_TRACE_EVENT_H

#include <linux/tracepoint.h>

// Some macros and helper functions are defined in 'drivers/firewire/ohci.c'.

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

#define QUADLET_SIZE	4

#define SELF_ID_COUNT_IS_ERROR(reg)	\
	(!!(((reg) & OHCI1394_SelfIDCount_selfIDError_MASK) >> OHCI1394_SelfIDCount_selfIDError_SHIFT))

#define SELF_ID_COUNT_GET_GENERATION(reg)	\
	(((reg) & OHCI1394_SelfIDCount_selfIDGeneration_MASK) >> OHCI1394_SelfIDCount_selfIDGeneration_SHIFT)

#define SELF_ID_RECEIVE_Q0_GET_GENERATION(quadlet)	\
	(((quadlet) & OHCI1394_SELF_ID_RECEIVE_Q0_GENERATION_MASK) >> OHCI1394_SELF_ID_RECEIVE_Q0_GENERATION_SHIFT)

#define SELF_ID_RECEIVE_Q0_GET_TIMESTAMP(quadlet)	\
	(((quadlet) & OHCI1394_SELF_ID_RECEIVE_Q0_TIMESTAMP_MASK) >> OHCI1394_SELF_ID_RECEIVE_Q0_TIMESTAMP_SHIFT)

TRACE_EVENT(self_id_complete,
	TP_PROTO(unsigned int card_index, u32 reg, const __le32 *self_id_receive, bool has_be_header_quirk),
	TP_ARGS(card_index, reg, self_id_receive, has_be_header_quirk),
	TP_STRUCT__entry(
		__field(u8, card_index)
		__field(u32, reg)
		__dynamic_array(u32, self_id_receive, ohci1394_self_id_count_get_size(reg))
	),
	TP_fast_assign(
		__entry->card_index = card_index;
		__entry->reg = reg;
		{
			u32 *ptr = __get_dynamic_array(self_id_receive);
			int i;

			for (i = 0; i < __get_dynamic_array_len(self_id_receive) / QUADLET_SIZE; ++i)
				ptr[i] = cond_le32_to_cpu(self_id_receive[i], has_be_header_quirk);
		}
	),
	TP_printk(
		"card_index=%u is_error=%s generation_at_bus_reset=%u generation_at_completion=%u timestamp=0x%04x packet_data=%s",
		__entry->card_index,
		SELF_ID_COUNT_IS_ERROR(__entry->reg) ? "true" : "false",
		SELF_ID_COUNT_GET_GENERATION(__entry->reg),
		SELF_ID_RECEIVE_Q0_GET_GENERATION(((const u32 *)__get_dynamic_array(self_id_receive))[0]),
		SELF_ID_RECEIVE_Q0_GET_TIMESTAMP(((const u32 *)__get_dynamic_array(self_id_receive))[0]),
		__print_array(((const u32 *)__get_dynamic_array(self_id_receive)) + 1,
			      (__get_dynamic_array_len(self_id_receive) / QUADLET_SIZE) - 1, QUADLET_SIZE)
	)
);

#undef SELF_ID_COUNT_IS_ERROR
#undef SELF_ID_COUNT_GET_GENERATION
#undef SELF_ID_RECEIVE_Q0_GET_GENERATION
#undef SELF_ID_RECEIVE_Q0_GET_TIMESTAMP

#undef QUADLET_SIZE

#endif // _FIREWIRE_OHCI_TRACE_EVENT_H

#include <trace/define_trace.h>
