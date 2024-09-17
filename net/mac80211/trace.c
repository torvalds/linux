// SPDX-License-Identifier: GPL-2.0
/* bug in tracepoint.h, it should include this */
#include <linux/module.h>

/* sparse isn't too happy with all macros... */
#ifndef __CHECKER__
#include <net/cfg80211.h>
#include "driver-ops.h"
#include "debug.h"
#define CREATE_TRACE_POINTS
#include "trace.h"
#include "trace_msg.h"

#ifdef CONFIG_MAC80211_MESSAGE_TRACING
void __sdata_info(const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;

	pr_info("%pV", &vaf);
	trace_mac80211_info(&vaf);
	va_end(args);
}

void __sdata_dbg(bool print, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;

	if (print)
		pr_debug("%pV", &vaf);
	trace_mac80211_dbg(&vaf);
	va_end(args);
}

void __sdata_err(const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;

	pr_err("%pV", &vaf);
	trace_mac80211_err(&vaf);
	va_end(args);
}

void __wiphy_dbg(struct wiphy *wiphy, bool print, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;

	if (print)
		wiphy_dbg(wiphy, "%pV", &vaf);
	trace_mac80211_dbg(&vaf);
	va_end(args);
}
#endif
#endif
