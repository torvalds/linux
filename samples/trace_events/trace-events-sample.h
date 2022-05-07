/* SPDX-License-Identifier: GPL-2.0 */
/*
 * If TRACE_SYSTEM is defined, that will be the directory created
 * in the ftrace directory under /sys/kernel/tracing/events/<system>
 *
 * The define_trace.h below will also look for a file name of
 * TRACE_SYSTEM.h where TRACE_SYSTEM is what is defined here.
 * In this case, it would look for sample-trace.h
 *
 * If the header name will be different than the system name
 * (as in this case), then you can override the header name that
 * define_trace.h will look up by defining TRACE_INCLUDE_FILE
 *
 * This file is called trace-events-sample.h but we want the system
 * to be called "sample-trace". Therefore we must define the name of this
 * file:
 *
 * #define TRACE_INCLUDE_FILE trace-events-sample
 *
 * As we do an the bottom of this file.
 *
 * Notice that TRACE_SYSTEM should be defined outside of #if
 * protection, just like TRACE_INCLUDE_FILE.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sample-trace

/*
 * TRACE_SYSTEM is expected to be a C valid variable (alpha-numeric
 * and underscore), although it may start with numbers. If for some
 * reason it is not, you need to add the following lines:
 */
#undef TRACE_SYSTEM_VAR
#define TRACE_SYSTEM_VAR sample_trace
/*
 * But the above is only needed if TRACE_SYSTEM is not alpha-numeric
 * and underscored. By default, TRACE_SYSTEM_VAR will be equal to
 * TRACE_SYSTEM. As TRACE_SYSTEM_VAR must be alpha-numeric, if
 * TRACE_SYSTEM is not, then TRACE_SYSTEM_VAR must be defined with
 * only alpha-numeric and underscores.
 *
 * The TRACE_SYSTEM_VAR is only used internally and not visible to
 * user space.
 */

/*
 * Notice that this file is not protected like a normal header.
 * We also must allow for rereading of this file. The
 *
 *  || defined(TRACE_HEADER_MULTI_READ)
 *
 * serves this purpose.
 */
#if !defined(_TRACE_EVENT_SAMPLE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EVENT_SAMPLE_H

/*
 * All trace headers should include tracepoint.h, until we finally
 * make it into a standard header.
 */
#include <linux/tracepoint.h>

/*
 * The TRACE_EVENT macro is broken up into 5 parts.
 *
 * name: name of the trace point. This is also how to enable the tracepoint.
 *   A function called trace_foo_bar() will be created.
 *
 * proto: the prototype of the function trace_foo_bar()
 *   Here it is trace_foo_bar(char *foo, int bar).
 *
 * args:  must match the arguments in the prototype.
 *    Here it is simply "foo, bar".
 *
 * struct:  This defines the way the data will be stored in the ring buffer.
 *          The items declared here become part of a special structure
 *          called "__entry", which can be used in the fast_assign part of the
 *          TRACE_EVENT macro.
 *
 *      Here are the currently defined types you can use:
 *
 *   __field : Is broken up into type and name. Where type can be any
 *         primitive type (integer, long or pointer).
 *
 *        __field(int, foo)
 *
 *        __entry->foo = 5;
 *
 *   __field_struct : This can be any static complex data type (struct, union
 *         but not an array). Be careful using complex types, as each
 *         event is limited in size, and copying large amounts of data
 *         into the ring buffer can slow things down.
 *
 *         __field_struct(struct bar, foo)
 *
 *         __entry->bar.x = y;

 *   __array: There are three fields (type, name, size). The type is the
 *         type of elements in the array, the name is the name of the array.
 *         size is the number of items in the array (not the total size).
 *
 *         __array( char, foo, 10) is the same as saying: char foo[10];
 *
 *         Assigning arrays can be done like any array:
 *
 *         __entry->foo[0] = 'a';
 *
 *         memcpy(__entry->foo, bar, 10);
 *
 *   __dynamic_array: This is similar to array, but can vary its size from
 *         instance to instance of the tracepoint being called.
 *         Like __array, this too has three elements (type, name, size);
 *         type is the type of the element, name is the name of the array.
 *         The size is different than __array. It is not a static number,
 *         but the algorithm to figure out the length of the array for the
 *         specific instance of tracepoint. Again, size is the number of
 *         items in the array, not the total length in bytes.
 *
 *         __dynamic_array( int, foo, bar) is similar to: int foo[bar];
 *
 *         Note, unlike arrays, you must use the __get_dynamic_array() macro
 *         to access the array.
 *
 *         memcpy(__get_dynamic_array(foo), bar, 10);
 *
 *         Notice, that "__entry" is not needed here.
 *
 *   __string: This is a special kind of __dynamic_array. It expects to
 *         have a null terminated character array passed to it (it allows
 *         for NULL too, which would be converted into "(null)"). __string
 *         takes two parameter (name, src), where name is the name of
 *         the string saved, and src is the string to copy into the
 *         ring buffer.
 *
 *         __string(foo, bar)  is similar to:  strcpy(foo, bar)
 *
 *         To assign a string, use the helper macro __assign_str().
 *
 *         __assign_str(foo, bar);
 *
 *         In most cases, the __assign_str() macro will take the same
 *         parameters as the __string() macro had to declare the string.
 *
 *   __bitmask: This is another kind of __dynamic_array, but it expects
 *         an array of longs, and the number of bits to parse. It takes
 *         two parameters (name, nr_bits), where name is the name of the
 *         bitmask to save, and the nr_bits is the number of bits to record.
 *
 *         __bitmask(target_cpu, nr_cpumask_bits)
 *
 *         To assign a bitmask, use the __assign_bitmask() helper macro.
 *
 *         __assign_bitmask(target_cpus, cpumask_bits(bar), nr_cpumask_bits);
 *
 *
 * fast_assign: This is a C like function that is used to store the items
 *    into the ring buffer. A special variable called "__entry" will be the
 *    structure that points into the ring buffer and has the same fields as
 *    described by the struct part of TRACE_EVENT above.
 *
 * printk: This is a way to print out the data in pretty print. This is
 *    useful if the system crashes and you are logging via a serial line,
 *    the data can be printed to the console using this "printk" method.
 *    This is also used to print out the data from the trace files.
 *    Again, the __entry macro is used to access the data from the ring buffer.
 *
 *    Note, __dynamic_array, __string, and __bitmask require special helpers
 *       to access the data.
 *
 *      For __dynamic_array(int, foo, bar) use __get_dynamic_array(foo)
 *            Use __get_dynamic_array_len(foo) to get the length of the array
 *            saved. Note, __get_dynamic_array_len() returns the total allocated
 *            length of the dynamic array; __print_array() expects the second
 *            parameter to be the number of elements. To get that, the array length
 *            needs to be divided by the element size.
 *
 *      For __string(foo, bar) use __get_str(foo)
 *
 *      For __bitmask(target_cpus, nr_cpumask_bits) use __get_bitmask(target_cpus)
 *
 *
 * Note, that for both the assign and the printk, __entry is the handler
 * to the data structure in the ring buffer, and is defined by the
 * TP_STRUCT__entry.
 */

/*
 * It is OK to have helper functions in the file, but they need to be protected
 * from being defined more than once. Remember, this file gets included more
 * than once.
 */
#ifndef __TRACE_EVENT_SAMPLE_HELPER_FUNCTIONS
#define __TRACE_EVENT_SAMPLE_HELPER_FUNCTIONS
static inline int __length_of(const int *list)
{
	int i;

	if (!list)
		return 0;

	for (i = 0; list[i]; i++)
		;
	return i;
}

enum {
	TRACE_SAMPLE_FOO = 2,
	TRACE_SAMPLE_BAR = 4,
	TRACE_SAMPLE_ZOO = 8,
};
#endif

/*
 * If enums are used in the TP_printk(), their names will be shown in
 * format files and not their values. This can cause problems with user
 * space programs that parse the format files to know how to translate
 * the raw binary trace output into human readable text.
 *
 * To help out user space programs, any enum that is used in the TP_printk()
 * should be defined by TRACE_DEFINE_ENUM() macro. All that is needed to
 * be done is to add this macro with the enum within it in the trace
 * header file, and it will be converted in the output.
 */

TRACE_DEFINE_ENUM(TRACE_SAMPLE_FOO);
TRACE_DEFINE_ENUM(TRACE_SAMPLE_BAR);
TRACE_DEFINE_ENUM(TRACE_SAMPLE_ZOO);

TRACE_EVENT(foo_bar,

	TP_PROTO(const char *foo, int bar, const int *lst,
		 const char *string, const struct cpumask *mask),

	TP_ARGS(foo, bar, lst, string, mask),

	TP_STRUCT__entry(
		__array(	char,	foo,    10		)
		__field(	int,	bar			)
		__dynamic_array(int,	list,   __length_of(lst))
		__string(	str,	string			)
		__bitmask(	cpus,	num_possible_cpus()	)
	),

	TP_fast_assign(
		strlcpy(__entry->foo, foo, 10);
		__entry->bar	= bar;
		memcpy(__get_dynamic_array(list), lst,
		       __length_of(lst) * sizeof(int));
		__assign_str(str, string);
		__assign_bitmask(cpus, cpumask_bits(mask), num_possible_cpus());
	),

	TP_printk("foo %s %d %s %s %s %s (%s)", __entry->foo, __entry->bar,

/*
 * Notice here the use of some helper functions. This includes:
 *
 *  __print_symbolic( variable, { value, "string" }, ... ),
 *
 *    The variable is tested against each value of the { } pair. If
 *    the variable matches one of the values, then it will print the
 *    string in that pair. If non are matched, it returns a string
 *    version of the number (if __entry->bar == 7 then "7" is returned).
 */
		  __print_symbolic(__entry->bar,
				   { 0, "zero" },
				   { TRACE_SAMPLE_FOO, "TWO" },
				   { TRACE_SAMPLE_BAR, "FOUR" },
				   { TRACE_SAMPLE_ZOO, "EIGHT" },
				   { 10, "TEN" }
			  ),

/*
 *  __print_flags( variable, "delim", { value, "flag" }, ... ),
 *
 *    This is similar to __print_symbolic, except that it tests the bits
 *    of the value. If ((FLAG & variable) == FLAG) then the string is
 *    printed. If more than one flag matches, then each one that does is
 *    also printed with delim in between them.
 *    If not all bits are accounted for, then the not found bits will be
 *    added in hex format: 0x506 will show BIT2|BIT4|0x500
 */
		  __print_flags(__entry->bar, "|",
				{ 1, "BIT1" },
				{ 2, "BIT2" },
				{ 4, "BIT3" },
				{ 8, "BIT4" }
			  ),
/*
 *  __print_array( array, len, element_size )
 *
 *    This prints out the array that is defined by __array in a nice format.
 */
		  __print_array(__get_dynamic_array(list),
				__get_dynamic_array_len(list) / sizeof(int),
				sizeof(int)),
		  __get_str(str), __get_bitmask(cpus))
);

/*
 * There may be a case where a tracepoint should only be called if
 * some condition is set. Otherwise the tracepoint should not be called.
 * But to do something like:
 *
 *  if (cond)
 *     trace_foo();
 *
 * Would cause a little overhead when tracing is not enabled, and that
 * overhead, even if small, is not something we want. As tracepoints
 * use static branch (aka jump_labels), where no branch is taken to
 * skip the tracepoint when not enabled, and a jmp is placed to jump
 * to the tracepoint code when it is enabled, having a if statement
 * nullifies that optimization. It would be nice to place that
 * condition within the static branch. This is where TRACE_EVENT_CONDITION
 * comes in.
 *
 * TRACE_EVENT_CONDITION() is just like TRACE_EVENT, except it adds another
 * parameter just after args. Where TRACE_EVENT has:
 *
 * TRACE_EVENT(name, proto, args, struct, assign, printk)
 *
 * the CONDITION version has:
 *
 * TRACE_EVENT_CONDITION(name, proto, args, cond, struct, assign, printk)
 *
 * Everything is the same as TRACE_EVENT except for the new cond. Think
 * of the cond variable as:
 *
 *   if (cond)
 *      trace_foo_bar_with_cond();
 *
 * Except that the logic for the if branch is placed after the static branch.
 * That is, the if statement that processes the condition will not be
 * executed unless that traecpoint is enabled. Otherwise it still remains
 * a nop.
 */
TRACE_EVENT_CONDITION(foo_bar_with_cond,

	TP_PROTO(const char *foo, int bar),

	TP_ARGS(foo, bar),

	TP_CONDITION(!(bar % 10)),

	TP_STRUCT__entry(
		__string(	foo,    foo		)
		__field(	int,	bar			)
	),

	TP_fast_assign(
		__assign_str(foo, foo);
		__entry->bar	= bar;
	),

	TP_printk("foo %s %d", __get_str(foo), __entry->bar)
);

int foo_bar_reg(void);
void foo_bar_unreg(void);

/*
 * Now in the case that some function needs to be called when the
 * tracepoint is enabled and/or when it is disabled, the
 * TRACE_EVENT_FN() serves this purpose. This is just like TRACE_EVENT()
 * but adds two more parameters at the end:
 *
 * TRACE_EVENT_FN( name, proto, args, struct, assign, printk, reg, unreg)
 *
 * reg and unreg are functions with the prototype of:
 *
 *    void reg(void)
 *
 * The reg function gets called before the tracepoint is enabled, and
 * the unreg function gets called after the tracepoint is disabled.
 *
 * Note, reg and unreg are allowed to be NULL. If you only need to
 * call a function before enabling, or after disabling, just set one
 * function and pass in NULL for the other parameter.
 */
TRACE_EVENT_FN(foo_bar_with_fn,

	TP_PROTO(const char *foo, int bar),

	TP_ARGS(foo, bar),

	TP_STRUCT__entry(
		__string(	foo,    foo		)
		__field(	int,	bar		)
	),

	TP_fast_assign(
		__assign_str(foo, foo);
		__entry->bar	= bar;
	),

	TP_printk("foo %s %d", __get_str(foo), __entry->bar),

	foo_bar_reg, foo_bar_unreg
);

/*
 * Each TRACE_EVENT macro creates several helper functions to produce
 * the code to add the tracepoint, create the files in the trace
 * directory, hook it to perf, assign the values and to print out
 * the raw data from the ring buffer. To prevent too much bloat,
 * if there are more than one tracepoint that uses the same format
 * for the proto, args, struct, assign and printk, and only the name
 * is different, it is highly recommended to use the DECLARE_EVENT_CLASS
 *
 * DECLARE_EVENT_CLASS() macro creates most of the functions for the
 * tracepoint. Then DEFINE_EVENT() is use to hook a tracepoint to those
 * functions. This DEFINE_EVENT() is an instance of the class and can
 * be enabled and disabled separately from other events (either TRACE_EVENT
 * or other DEFINE_EVENT()s).
 *
 * Note, TRACE_EVENT() itself is simply defined as:
 *
 * #define TRACE_EVENT(name, proto, args, tstruct, assign, printk)  \
 *  DECLARE_EVENT_CLASS(name, proto, args, tstruct, assign, printk); \
 *  DEFINE_EVENT(name, name, proto, args)
 *
 * The DEFINE_EVENT() also can be declared with conditions and reg functions:
 *
 * DEFINE_EVENT_CONDITION(template, name, proto, args, cond);
 * DEFINE_EVENT_FN(template, name, proto, args, reg, unreg);
 */
DECLARE_EVENT_CLASS(foo_template,

	TP_PROTO(const char *foo, int bar),

	TP_ARGS(foo, bar),

	TP_STRUCT__entry(
		__string(	foo,    foo		)
		__field(	int,	bar		)
	),

	TP_fast_assign(
		__assign_str(foo, foo);
		__entry->bar	= bar;
	),

	TP_printk("foo %s %d", __get_str(foo), __entry->bar)
);

/*
 * Here's a better way for the previous samples (except, the first
 * example had more fields and could not be used here).
 */
DEFINE_EVENT(foo_template, foo_with_template_simple,
	TP_PROTO(const char *foo, int bar),
	TP_ARGS(foo, bar));

DEFINE_EVENT_CONDITION(foo_template, foo_with_template_cond,
	TP_PROTO(const char *foo, int bar),
	TP_ARGS(foo, bar),
	TP_CONDITION(!(bar % 8)));


DEFINE_EVENT_FN(foo_template, foo_with_template_fn,
	TP_PROTO(const char *foo, int bar),
	TP_ARGS(foo, bar),
	foo_bar_reg, foo_bar_unreg);

/*
 * Anytime two events share basically the same values and have
 * the same output, use the DECLARE_EVENT_CLASS() and DEFINE_EVENT()
 * when ever possible.
 */

/*
 * If the event is similar to the DECLARE_EVENT_CLASS, but you need
 * to have a different output, then use DEFINE_EVENT_PRINT() which
 * lets you override the TP_printk() of the class.
 */

DEFINE_EVENT_PRINT(foo_template, foo_with_template_print,
	TP_PROTO(const char *foo, int bar),
	TP_ARGS(foo, bar),
	TP_printk("bar %s %d", __get_str(foo), __entry->bar));

#endif

/***** NOTICE! The #if protection ends here. *****/


/*
 * There are several ways I could have done this. If I left out the
 * TRACE_INCLUDE_PATH, then it would default to the kernel source
 * include/trace/events directory.
 *
 * I could specify a path from the define_trace.h file back to this
 * file.
 *
 * #define TRACE_INCLUDE_PATH ../../samples/trace_events
 *
 * But the safest and easiest way to simply make it use the directory
 * that the file is in is to add in the Makefile:
 *
 * CFLAGS_trace-events-sample.o := -I$(src)
 *
 * This will make sure the current path is part of the include
 * structure for our file so that define_trace.h can find it.
 *
 * I could have made only the top level directory the include:
 *
 * CFLAGS_trace-events-sample.o := -I$(PWD)
 *
 * And then let the path to this directory be the TRACE_INCLUDE_PATH:
 *
 * #define TRACE_INCLUDE_PATH samples/trace_events
 *
 * But then if something defines "samples" or "trace_events" as a macro
 * then we could risk that being converted too, and give us an unexpected
 * result.
 */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
/*
 * TRACE_INCLUDE_FILE is not needed if the filename and TRACE_SYSTEM are equal
 */
#define TRACE_INCLUDE_FILE trace-events-sample
#include <trace/define_trace.h>
