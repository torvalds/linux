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
#undef __print_hex_dump
#undef __get_buf

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
