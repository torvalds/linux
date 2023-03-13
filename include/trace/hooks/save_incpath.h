/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Include this file from a header declaring vendor hooks to preserve and later
 * restore TRACE_INCLUDE_PATH value. Typical usage:
 *
 *   #ifdef PROTECT_TRACE_INCLUDE_PATH
 *   #undef PROTECT_TRACE_INCLUDE_PATH
 *
 *   #include <trace/hooks/save_incpath.h>
 *   #include <vendor hooks header>
 *   #include <trace/hooks/restore_incpath.h>
 *
 *   #else
 *
 *   <vendor hook definitions>
 *
 *   #endif
 *
 * The header that includes vendor hooks header file should define
 * PROTECT_TRACE_INCLUDE_PATH before including the vendor hook file like this:
 *
 *   #define PROTECT_TRACE_INCLUDE_PATH
 *   #include <vendor hooks header>
 */
#ifdef TRACE_INCLUDE_PATH
#define STORED_TRACE_INCLUDE_PATH TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_PATH
#endif

