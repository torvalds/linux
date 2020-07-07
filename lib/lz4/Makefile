# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -O3
ccflags-remove-$(CONFIG_FUNCTION_TRACER) += $(CC_FLAGS_FTRACE)

obj-$(CONFIG_LZ4_COMPRESS) += lz4_compress.o
obj-$(CONFIG_LZ4HC_COMPRESS) += lz4hc_compress.o
obj-$(CONFIG_LZ4_DECOMPRESS) += lz4_decompress.o
