
obj-y += mutex.o

ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_mutex-debug.o = -pg
CFLAGS_REMOVE_rtmutex-debug.o = -pg
endif

obj-$(CONFIG_DEBUG_MUTEXES) += mutex-debug.o
