
#define __app__(x, y) str__##x##y
#define __app(x, y) __app__(x, y)

#define TRACE_SYSTEM_STRING __app(TRACE_SYSTEM_VAR,__trace_system_name)

#define TRACE_MAKE_SYSTEM_STR()				\
	static const char TRACE_SYSTEM_STRING[] =	\
		__stringify(TRACE_SYSTEM)

TRACE_MAKE_SYSTEM_STR();

#undef TRACE_DEFINE_ENUM
#define TRACE_DEFINE_ENUM(a)				\
	static struct trace_eval_map __used __initdata	\
	__##TRACE_SYSTEM##_##a =			\
	{						\
		.system = TRACE_SYSTEM_STRING,		\
		.eval_string = #a,			\
		.eval_value = a				\
	};						\
	static struct trace_eval_map __used		\
	__section("_ftrace_eval_map")			\
	*TRACE_SYSTEM##_##a = &__##TRACE_SYSTEM##_##a

#undef TRACE_DEFINE_SIZEOF
#define TRACE_DEFINE_SIZEOF(a)				\
	static struct trace_eval_map __used __initdata	\
	__##TRACE_SYSTEM##_##a =			\
	{						\
		.system = TRACE_SYSTEM_STRING,		\
		.eval_string = "sizeof(" #a ")",	\
		.eval_value = sizeof(a)			\
	};						\
	static struct trace_eval_map __used		\
	__section("_ftrace_eval_map")			\
	*TRACE_SYSTEM##_##a = &__##TRACE_SYSTEM##_##a
