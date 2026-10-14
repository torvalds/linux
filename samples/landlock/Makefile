# SPDX-License-Identifier: BSD-3-Clause

userprogs-always-y := sandboxer

userccflags += -I usr/include

ifeq ($(CONFIG_SAMPLE_LANDLOCK_LIBCAP),y)
userccflags += -DUSE_LIBCAP
userldlibs += -lcap
endif

.PHONY: all clean

all:
	$(MAKE) -C ../.. samples/landlock/

clean:
	$(MAKE) -C ../.. M=samples/landlock/ clean
