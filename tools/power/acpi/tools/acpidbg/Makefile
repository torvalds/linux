# tools/power/acpi/tools/acpidbg/Makefile - ACPI tool Makefile
#
# Copyright (c) 2015, Intel Corporation
#   Author: Lv Zheng <lv.zheng@intel.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 2
# of the License.

include ../../Makefile.config

TOOL = acpidbg
vpath %.c \
	../../../../../drivers/acpi/acpica\
	../../common\
	../../os_specific/service_layers\
	.
CFLAGS += -DACPI_APPLICATION -DACPI_SINGLE_THREAD -DACPI_DEBUGGER\
	-I.\
	-I../../../../../drivers/acpi/acpica\
	-I../../../../../include
LDFLAGS += -lpthread
TOOL_OBJS = \
	acpidbg.o

include ../../Makefile.rules
