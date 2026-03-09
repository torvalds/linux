/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/trace_events.h>
#include <linux/trace_remote_event.h>
#include <linux/trace_seq.h>
#include <linux/stringify.h>

#define REMOTE_EVENT_INCLUDE(__file) __stringify(../../__file)

#ifdef REMOTE_EVENT_SECTION
# define __REMOTE_EVENT_SECTION(__name) __used __section(REMOTE_EVENT_SECTION"."#__name)
#else
# define __REMOTE_EVENT_SECTION(__name)
#endif

#define REMOTE_PRINTK_COUNT_ARGS(__args...) \
	__COUNT_ARGS(, ##__args, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0)

#define __remote_printk0()								\
	trace_seq_putc(seq, '\n')

#define __remote_printk1(__fmt)								\
	trace_seq_puts(seq, " " __fmt "\n")						\

#define __remote_printk2(__fmt, __args...)						\
do {											\
	trace_seq_putc(seq, ' ');							\
	trace_seq_printf(seq, __fmt, __args);						\
	trace_seq_putc(seq, '\n');							\
} while (0)

/* Apply the appropriate trace_seq sequence according to the number of arguments */
#define remote_printk(__args...)							\
	CONCATENATE(__remote_printk, REMOTE_PRINTK_COUNT_ARGS(__args))(__args)

#define RE_PRINTK(__args...) __args

#define REMOTE_EVENT(__name, __id, __struct, __printk)					\
	REMOTE_EVENT_FORMAT(__name, __struct);						\
	static void remote_event_print_##__name(void *evt, struct trace_seq *seq)	\
	{										\
		struct remote_event_format_##__name __maybe_unused *__entry = evt;	\
		trace_seq_puts(seq, #__name);						\
		remote_printk(__printk);						\
	}
#include REMOTE_EVENT_INCLUDE(REMOTE_EVENT_INCLUDE_FILE)

#undef REMOTE_EVENT
#undef RE_PRINTK
#undef re_field
#define re_field(__type, __field)							\
	{										\
		.type = #__type, .name = #__field,					\
		.size = sizeof(__type), .align = __alignof__(__type),			\
		.is_signed = is_signed_type(__type),					\
	},
#define __entry REC
#define RE_PRINTK(__fmt, __args...) "\"" __fmt "\", " __stringify(__args)
#define REMOTE_EVENT(__name, __id, __struct, __printk)					\
	static struct trace_event_fields remote_event_fields_##__name[] = {		\
		__struct								\
		{}									\
	};										\
	static char remote_event_print_fmt_##__name[] = __printk;			\
	static struct remote_event __REMOTE_EVENT_SECTION(__name)			\
	remote_event_##__name = {							\
		.name		= #__name,						\
		.id		= __id,							\
		.fields		= remote_event_fields_##__name,				\
		.print_fmt	= remote_event_print_fmt_##__name,			\
		.print		= remote_event_print_##__name,				\
	}
#include REMOTE_EVENT_INCLUDE(REMOTE_EVENT_INCLUDE_FILE)
