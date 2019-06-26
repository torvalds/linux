# SPDX-License-Identifier: GPL-2.0
# Font handling

font-objs := fonts.o

font-objs-$(CONFIG_FONT_SUN8x16)   += font_sun8x16.o
font-objs-$(CONFIG_FONT_SUN12x22)  += font_sun12x22.o
font-objs-$(CONFIG_FONT_8x8)       += font_8x8.o
font-objs-$(CONFIG_FONT_8x16)      += font_8x16.o
font-objs-$(CONFIG_FONT_6x11)      += font_6x11.o
font-objs-$(CONFIG_FONT_7x14)      += font_7x14.o
font-objs-$(CONFIG_FONT_10x18)     += font_10x18.o
font-objs-$(CONFIG_FONT_PEARL_8x8) += font_pearl_8x8.o
font-objs-$(CONFIG_FONT_ACORN_8x8) += font_acorn_8x8.o
font-objs-$(CONFIG_FONT_MINI_4x6)  += font_mini_4x6.o
font-objs-$(CONFIG_FONT_6x10)      += font_6x10.o
font-objs-$(CONFIG_FONT_TER16x32)  += font_ter16x32.o

font-objs += $(font-objs-y)

obj-$(CONFIG_FONT_SUPPORT)         += font.o
