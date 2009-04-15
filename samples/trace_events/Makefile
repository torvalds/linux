# builds the trace events example kernel modules;
# then to use one (as root):  insmod <module_name.ko>

PWD := $(shell pwd)

CFLAGS_trace-events-sample.o := -I$(PWD)/samples/trace_events/

obj-$(CONFIG_SAMPLE_TRACE_EVENTS) += trace-events-sample.o
