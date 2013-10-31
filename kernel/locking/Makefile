
obj-y += mutex.o

ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_lockdep.o = -pg
CFLAGS_REMOVE_lockdep_proc.o = -pg
CFLAGS_REMOVE_mutex-debug.o = -pg
CFLAGS_REMOVE_rtmutex-debug.o = -pg
endif

obj-$(CONFIG_DEBUG_MUTEXES) += mutex-debug.o
obj-$(CONFIG_LOCKDEP) += lockdep.o
ifeq ($(CONFIG_PROC_FS),y)
obj-$(CONFIG_LOCKDEP) += lockdep_proc.o
endif
