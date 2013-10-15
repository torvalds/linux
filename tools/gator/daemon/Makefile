#
# Makefile for ARM Streamline - Gator Daemon
#

# Uncomment and define CROSS_COMPILE if it is not already defined
# CROSS_COMPILE=/path/to/cross-compiler/arm-linux-gnueabihf-
# NOTE: This toolchain uses the hardfloat abi by default. For non-hardfloat
# targets run 'make SOFTFLOAT=1 SYSROOT=/path/to/sysroot', see
# README_Streamline.txt for more details

CPP = $(CROSS_COMPILE)g++
GCC = $(CROSS_COMPILE)gcc

# -mthumb-interwork is required for interworking to ARM or Thumb stdlibc
CFLAGS += -mthumb-interwork

ifeq ($(SOFTFLOAT),1)
	CFLAGS += -marm -march=armv4t -mfloat-abi=soft
	LDFLAGS += -marm -march=armv4t -mfloat-abi=soft
endif
ifneq ($(SYSROOT),)
	LDFLAGS += --sysroot=$(SYSROOT)
endif

include common.mk
