#define TRACE_RESUME(user) do {					\
	if (pm_trace_enabled) {					\
		void *tracedata;				\
		asm volatile("movl $1f,%0\n"			\
			".section .tracedata,\"a\"\n"		\
			"1:\t.word %c1\n"			\
			"\t.long %c2\n"				\
			".previous"				\
			:"=r" (tracedata)			\
			: "i" (__LINE__), "i" (__FILE__));	\
		generate_resume_trace(tracedata, user);		\
	}							\
} while (0)
