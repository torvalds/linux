# builds the trace events example kernel modules;
# then to use one (as root):  insmod <module_name.ko>

CFLAGS_trace-events-sample.o := -I$(src)

obj-$(CONFIG_SAMPLE_TRACE_EVENTS) += trace-events-sample.o
