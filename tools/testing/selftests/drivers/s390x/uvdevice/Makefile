include ../../../../../build/Build.include

top_srcdir ?= ../../../../../..
include $(top_srcdir)/scripts/subarch.include
ARCH_PROCESSED := $(or $(ARCH),$(SUBARCH))

ifneq (,$(filter s390 s390x,$(ARCH_PROCESSED)))
TEST_GEN_PROGS := test_uvdevice

CFLAGS += -Wall -Werror -static $(KHDR_INCLUDES)
endif

include ../../../lib.mk
