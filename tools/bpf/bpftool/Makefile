include ../../scripts/Makefile.include

include ../../scripts/utilities.mak

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
#$(info Determined 'srctree' to be $(srctree))
endif

ifneq ($(objtree),)
#$(info Determined 'objtree' to be $(objtree))
endif

ifneq ($(OUTPUT),)
#$(info Determined 'OUTPUT' to be $(OUTPUT))
# Adding $(OUTPUT) as a directory to look for source files,
# because use generated output files as sources dependency
# for flex/bison parsers.
VPATH += $(OUTPUT)
export VPATH
endif

ifeq ($(V),1)
  Q =
else
  Q = @
endif

BPF_DIR	= $(srctree)/tools/lib/bpf/

ifneq ($(OUTPUT),)
  BPF_PATH=$(OUTPUT)
else
  BPF_PATH=$(BPF_DIR)
endif

LIBBPF = $(BPF_PATH)libbpf.a

$(LIBBPF): FORCE
	$(Q)$(MAKE) -C $(BPF_DIR) OUTPUT=$(OUTPUT) $(OUTPUT)libbpf.a FEATURES_DUMP=$(FEATURE_DUMP_EXPORT)

$(LIBBPF)-clean:
	$(call QUIET_CLEAN, libbpf)
	$(Q)$(MAKE) -C $(BPF_DIR) OUTPUT=$(OUTPUT) clean >/dev/null

prefix = /usr

CC = gcc

CFLAGS += -O2
CFLAGS += -W -Wall -Wextra -Wno-unused-parameter -Wshadow
CFLAGS += -D__EXPORTED_HEADERS__ -I$(srctree)/tools/include/uapi -I$(srctree)/tools/include -I$(srctree)/tools/lib/bpf
LIBS = -lelf -lbfd -lopcodes $(LIBBPF)

include $(wildcard *.d)

all: $(OUTPUT)bpftool

SRCS=$(wildcard *.c)
OBJS=$(patsubst %.c,$(OUTPUT)%.o,$(SRCS))

$(OUTPUT)bpftool: $(OBJS) $(LIBBPF)
	$(QUIET_LINK)$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(OUTPUT)%.o: %.c
	$(QUIET_CC)$(COMPILE.c) -MMD -o $@ $<

clean: $(LIBBPF)-clean
	$(call QUIET_CLEAN, bpftool)
	$(Q)rm -rf $(OUTPUT)bpftool $(OUTPUT)*.o $(OUTPUT)*.d

install:
	install $(OUTPUT)bpftool $(prefix)/sbin/bpftool

doc:
	$(Q)$(MAKE) -C Documentation/

doc-install:
	$(Q)$(MAKE) -C Documentation/ install

FORCE:

.PHONY: all clean FORCE
.DEFAULT_GOAL := all
