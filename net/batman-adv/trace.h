/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) B.A.T.M.A.N. contributors:
 *
 * Sven Eckelmann
 */

#if !defined(_NET_BATMAN_ADV_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _NET_BATMAN_ADV_TRACE_H_

#include "main.h"

#include <linux/netdevice.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM batadv

/* provide dummy function when tracing is disabled */
#if !defined(CONFIG_BATMAN_ADV_TRACING)

#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, ...) \
	static inline void trace_ ## name(proto) {}

#endif /* CONFIG_BATMAN_ADV_TRACING */

TRACE_EVENT(batadv_dbg,

	    TP_PROTO(struct batadv_priv *bat_priv,
		     struct va_format *vaf),

	    TP_ARGS(bat_priv, vaf),

	    TP_STRUCT__entry(
		    __string(device, bat_priv->mesh_iface->name)
		    __string(driver, KBUILD_MODNAME)
		    __vstring(msg, vaf->fmt, vaf->va)
	    ),

	    TP_fast_assign(
		    __assign_str(device);
		    __assign_str(driver);
		    __assign_vstr(msg, vaf->fmt, vaf->va);
	    ),

	    TP_printk(
		    "%s %s %s",
		    __get_str(driver),
		    __get_str(device),
		    __get_str(msg)
	    )
);

#endif /* _NET_BATMAN_ADV_TRACE_H_ || TRACE_HEADER_MULTI_READ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
