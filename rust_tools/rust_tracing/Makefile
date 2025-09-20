# SPDX-License-Identifier: GPL-2.0
include ../scripts/Makefile.include

all: latency rtla

clean: latency_clean rtla_clean

install: latency_install rtla_install

latency:
	$(call descend,latency)

latency_install:
	$(call descend,latency,install)

latency_clean:
	$(call descend,latency,clean)

rtla:
	$(call descend,rtla)

rtla_install:
	$(call descend,rtla,install)

rtla_clean:
	$(call descend,rtla,clean)

.PHONY: all install clean latency latency_install latency_clean \
	rtla rtla_install rtla_clean
