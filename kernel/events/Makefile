ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_core.o = -pg
endif

obj-y := core.o
obj-$(CONFIG_HAVE_HW_BREAKPOINT) += hw_breakpoint.o
