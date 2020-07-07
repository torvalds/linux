# SPDX-License-Identifier: GPL-2.0-only

ccflags-remove-$(CONFIG_FUNCTION_TRACER) += $(CC_FLAGS_FTRACE)

obj-$(CONFIG_XZ_DEC) += xz_dec.o
xz_dec-y := xz_dec_syms.o xz_dec_stream.o xz_dec_lzma2.o
xz_dec-$(CONFIG_XZ_DEC_BCJ) += xz_dec_bcj.o

obj-$(CONFIG_XZ_DEC_TEST) += xz_dec_test.o
