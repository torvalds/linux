# SPDX-License-Identifier: GPL-2.0
include ../scripts/Makefile.include

all: latency

clean: latency_clean

install: latency_install

latency:
	$(call descend,latency)

latency_install:
	$(call descend,latency,install)

latency_clean:
	$(call descend,latency,clean)

.PHONY: all install clean latency latency_install latency_clean
