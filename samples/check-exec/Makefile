# SPDX-License-Identifier: BSD-3-Clause

userprogs-always-y := \
	inc \
	set-exec

userccflags += -I usr/include

.PHONY: all clean

all:
	$(MAKE) -C ../.. samples/check-exec/

clean:
	$(MAKE) -C ../.. M=samples/check-exec/ clean
