# SPDX-License-Identifier: GPL-2.0
all: srcu.c store_buffering

LINUX_SOURCE = ../../../../../..

modified_srcu_input = $(LINUX_SOURCE)/include/linux/srcu.h \
		      $(LINUX_SOURCE)/kernel/rcu/srcu.c

modified_srcu_output = include/linux/srcu.h srcu.c

include/linux/srcu.h: srcu.c

srcu.c: modify_srcu.awk Makefile $(modified_srcu_input)
	awk -f modify_srcu.awk $(modified_srcu_input) $(modified_srcu_output)

store_buffering:
	@cd tests/store_buffering; make
