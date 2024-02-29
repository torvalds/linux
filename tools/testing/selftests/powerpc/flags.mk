#This checks for any ENV variables and add those.

ifeq ($(GIT_VERSION),)
GIT_VERSION := $(shell git describe --always --long --dirty || echo "unknown")
export GIT_VERSION
endif

ifeq ($(CFLAGS),)
CFLAGS := -std=gnu99 -O2 -Wall -Werror -DGIT_VERSION='"$(GIT_VERSION)"' -I$(selfdir)/powerpc/include $(CFLAGS)
export CFLAGS
endif

