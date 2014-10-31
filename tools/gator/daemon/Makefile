#
# Makefile for ARM Streamline - Gator Daemon
#

# Uncomment and define CROSS_COMPILE if it is not already defined
# CROSS_COMPILE=/path/to/cross-compiler/arm-linux-gnueabihf-
# NOTE: This toolchain uses the hardfloat abi by default. For non-hardfloat
# targets run 'make SOFTFLOAT=1 SYSROOT=/path/to/sysroot', see
# README_Streamline.txt for more details

CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++

ifeq ($(SOFTFLOAT),1)
	CPPFLAGS += -marm -mthumb-interwork -march=armv4t -mfloat-abi=soft
	LDFLAGS += -marm -march=armv4t -mfloat-abi=soft
endif
ifneq ($(SYSROOT),)
	LDFLAGS += --sysroot=$(SYSROOT)
endif

include common.mk
