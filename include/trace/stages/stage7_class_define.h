/* SPDX-License-Identifier: GPL-2.0 */

/* Stage 7 definitions for creating trace events */

#undef __entry
#define __entry REC

#undef __print_flags
#undef __print_symbolic
#undef __print_hex
#undef __print_hex_str
#undef __get_dynamic_array
#undef __get_dynamic_array_len
#undef __get_str
#undef __get_bitmask
#undef __get_cpumask
#undef __get_sockaddr
#undef __get_rel_dynamic_array
#undef __get_rel_dynamic_array_len
#undef __get_rel_str
#undef __get_rel_bitmask
#undef __get_rel_cpumask
#undef __get_rel_sockaddr
#undef __print_array
#undef __print_dynamic_array
#undef __print_hex_dump
#undef __get_buf

#undef __event_in_hardirq
#undef __event_in_softirq
#undef __event_in_irq

/*
 * The TRACE_FLAG_* are enums. Instead of using TRACE_DEFINE_ENUM(),
 * use their hardcoded values. These values are parsed by user space
 * tooling elsewhere so they will never change.
 *
 * See "enum trace_flag_type" in linux/trace_events.h:
 *   TRACE_FLAG_HARDIRQ
 *   TRACE_FLAG_SOFTIRQ
 */

/* This is what is displayed in the format files */
#define __event_in_hardirq()	(REC->common_flags & 0x8)
#define __event_in_softirq()	(REC->common_flags & 0x10)
#define __event_in_irq()	(REC->common_flags & 0x18)

/*
 * The below is not executed in the kernel. It is only what is
 * displayed in the print format for userspace to parse.
 */
#undef __print_ns_to_secs
#define __print_ns_to_secs(val) (val) / 1000000000UL

#undef __print_ns_without_secs
#define __print_ns_without_secs(val) (val) % 1000000000UL

#undef TP_printk
#define TP_printk(fmt, args...) "\"" fmt "\", "  __stringify(args)
