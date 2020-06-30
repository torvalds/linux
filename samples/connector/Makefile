# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_SAMPLE_CONNECTOR) += cn_test.o

userprogs := ucon
always-$(CONFIG_CC_CAN_LINK) := $(userprogs)

userccflags += -I usr/include
