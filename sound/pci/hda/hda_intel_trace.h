/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hda_intel
#define TRACE_INCLUDE_FILE hda_intel_trace

#if !defined(_TRACE_HDA_INTEL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HDA_INTEL_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(hda_pm,
	TP_PROTO(struct azx *chip),

	TP_ARGS(chip),

	TP_STRUCT__entry(
		__field(int, dev_index)
	),

	TP_fast_assign(
		__entry->dev_index = (chip)->dev_index;
	),

	TP_printk("card index: %d", __entry->dev_index)
);

DEFINE_EVENT(hda_pm, azx_suspend,
	TP_PROTO(struct azx *chip),
	TP_ARGS(chip)
);

DEFINE_EVENT(hda_pm, azx_resume,
	TP_PROTO(struct azx *chip),
	TP_ARGS(chip)
);

#ifdef CONFIG_PM
DEFINE_EVENT(hda_pm, azx_runtime_suspend,
	TP_PROTO(struct azx *chip),
	TP_ARGS(chip)
);

DEFINE_EVENT(hda_pm, azx_runtime_resume,
	TP_PROTO(struct azx *chip),
	TP_ARGS(chip)
);
#endif

#endif /* _TRACE_HDA_INTEL_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>
