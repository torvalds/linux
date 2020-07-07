#
# DIM Dynamic Interrupt Moderation library
#

ccflags-remove-$(CONFIG_FUNCTION_TRACER) += $(CC_FLAGS_FTRACE)

obj-$(CONFIG_DIMLIB) += dim.o

dim-y := dim.o net_dim.o rdma_dim.o
