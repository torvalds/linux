# Do not use make's built-in rules
# (this improves performance and avoids hard-to-debug behaviour);
# also do not print "Entering directory..." messages from make
.SUFFIXES:
MAKEFLAGS += -r --no-print-directory

KCONFIG?=defconfig

ifneq ($(silent),1)
  ifneq ($(V),1)
	QUIET_AUTOCONF       = @echo '  AUTOCONF '$@;
	Q = @
  endif
endif

PREFIX := /usr
BINDIR := $(PREFIX)/bin
INCDIR := $(PREFIX)/include
LIBDIR := $(PREFIX)/lib

ifeq (,$(srctree))
  srctree := $(patsubst %/,%,$(dir $(shell pwd)))
  srctree := $(patsubst %/,%,$(dir $(srctree)))
endif
export srctree

include ../scripts/Makefile.include

# OUTPUT fixup should be *after* include ../scripts/Makefile.include
ifneq ($(OUTPUT),)
  KERNEL_OUT_DIR := $(OUTPUT)
  KOPT += O=$(KERNEL_OUT_DIR)
  OUTPUT := $(OUTPUT)/tools/lkl/
else
  OUTPUT := $(CURDIR)/
endif
export OUTPUT


all:

conf: $(OUTPUT)Makefile.conf

$(OUTPUT)Makefile.conf $(OUTPUT)include/kernel_config.h $(OUTPUT)/kernel.config: Makefile.autoconf
	$(call QUIET_AUTOCONF, headers)$(MAKE) -f Makefile.autoconf -s

include $(OUTPUT)Makefile.conf

export CFLAGS += -I$(OUTPUT)/include -Iinclude -Wall -g -O2 -Wextra \
	 -Wno-unused-parameter \
	 -Wno-missing-field-initializers -fno-strict-aliasing

include Targets

# Expand targets to output location and suffix but preserve special
# targets (e.g. .WAIT)
# $1 - targets
# $2 - suffix
expand-targets = $(foreach t,$(1),$(if $(filter .%,$(t)),$(t),$(OUTPUT)$(t)$(2)))

TARGETS := $(call expand-targets,$(progs-y),$(EXESUF))
TARGETS += $(call expand-targets,$(tests-y),$(EXESUF))
TARGETS += $(call expand-targets,$(libs-y),$(SOSUF))
all: $(TARGETS)

# this workaround is for FreeBSD
bin/stat:
ifeq ($(LKL_HOST_CONFIG_BSD),y)
	$(Q)ln -sf `which gnustat` bin/stat
	$(Q)ln -sf `which gsed` bin/sed
else
	$(Q)touch bin/stat
endif

ASM_UAPI_GENERATED:=$(OUTPUT)../../arch/lkl/include/generated/uapi/asm
ASM_CONFIG:=$(ASM_UAPI_GENERATED)/config.h
DOT_CONFIG:=$(OUTPUT)../../.config

$(DOT_CONFIG): $(OUTPUT)/kernel.config
	$(Q)$(MAKE) -C ../.. ARCH=lkl $(KOPT) $(KCONFIG)
	$(call QUIET_INSTALL, kernel.config)cat $(OUTPUT)/kernel.config >> $(DOT_CONFIG)
	$(Q)$(MAKE) -C ../.. ARCH=lkl $(KOPT) olddefconfig
	$(Q)$(MAKE) -C ../.. ARCH=lkl $(KOPT) syncconfig


$(ASM_CONFIG): $(OUTPUT)include/kernel_config.h
	$(Q)mkdir -p $$(dirname $@)
	$(call QUIET_INSTALL, kernel_config.h)cp $<  $@

# rule to build lkl.o
$(OUTPUT)lib/lkl.o: bin/stat $(ASM_CONFIG) $(DOT_CONFIG)
# this workaround is for arm32 linker (ld.gold)
	$(Q)export PATH="$(srctree)/tools/lkl/bin/:${PATH}" ;\
	$(MAKE) -C ../.. ARCH=lkl $(KOPT)
	$(MAKE) -C ../.. ARCH=lkl $(KOPT) install INSTALL_PATH=$(OUTPUT)

# rules to link libs
$(OUTPUT)%$(SOSUF): LDFLAGS += -shared
$(OUTPUT)%$(SOSUF): $(OUTPUT)%-in.o $(OUTPUT)liblkl.a
	$(QUIET_LINK)$(CC) $(LDFLAGS) $(LDFLAGS_$*-y) -o $@ $^ $(LDLIBS) $(LDLIBS_$*-y)

# liblkl is special
$(OUTPUT)liblkl$(SOSUF): $(OUTPUT)%-in.o $(OUTPUT)lib/lkl.o
$(OUTPUT)liblkl.a: $(OUTPUT)lib/liblkl-in.o $(OUTPUT)lib/lkl.o
	$(QUIET_AR)$(AR) -rc $@ $^

# rule to link programs
$(OUTPUT)%$(EXESUF): $(OUTPUT)%-in.o $(OUTPUT)liblkl.a
	$(QUIET_LINK)$(CC) $(LDFLAGS) $(LDFLAGS_$*-y) -o $@ $^ $(LDLIBS) $(LDLIBS_$*-y)

# rule to build objects
$(OUTPUT)%-in.o: $(OUTPUT)lib/lkl.o FORCE
	$(Q)$(MAKE) -f $(srctree)/tools/build/Makefile.build dir=$(patsubst %/,%,$(dir $*)) obj=$(notdir $*)


$(OUTPUT)cpfromfs$(EXESUF): cptofs$(EXESUF)
	$(Q)if ! [ -e $@ ]; then ln -s $< $@; fi

# START: fuzzing-related build-rules
FUZZ_TARGETS := $(fuzzers-y:%=$(OUTPUT)%$(EXESUF))
fuzzers: $(FUZZ_TARGETS)

# Enable libFuzzer fuzzing instrumentation for the LKL fuzzers
$(OUTPUT)fuzzers/%$(EXESUF): LDFLAGS += -fsanitize=fuzzer --coverage

# Binder fuzzer is special: built with C++ toolchain and linked with libprotobuf-mutator
$(OUTPUT)fuzzers/binder%$(EXESUF): $(OUTPUT)fuzzers/binder%-in.o $(OUTPUT)liblkl.a
	$(QUIET_LINK)$(CXX) $(LDFLAGS) $(LDFLAGS_$*-y) -o $@ $^ $(LDLIBS) $(LDLIBS_$*-y)

# Track all the protoc generated files for 'clean' target
PROTOC_GENERATED_FILES :=

read_proto_deps = \
  $(strip $(shell grep PROTOBUF_MUTATOR_PROTO $(1) | sed 's/^.*= //g'))

define gen_protobuf_deps =
  $(eval FUZZER_SRC_DIR := $(dir $(srctree)/tools/lkl/$(1)))
  $(eval FUZZER_PROTO_FILES := $(call read_proto_deps,$(FUZZER_SRC_DIR)Build))
  $(eval FUZZER_PROTO_FILES := $(FUZZER_PROTO_FILES:.proto=.pb.cpp) \
    $(FUZZER_PROTO_FILES:.proto=.pb.h))
  $(eval FUZZER_PROTO_FILES := \
    $(addprefix $(FUZZER_SRC_DIR),$(FUZZER_PROTO_FILES)))
  PROTOC_GENERATED_FILES += $(FUZZER_PROTO_FILES)
  $(OUTPUT)$(1)-in.o: $(FUZZER_PROTO_FILES)
endef

# Add generated .pb.cc and pb.h files for libprotobuf-mutator as prerequisites
# for the corresponding fuzz targets to have them made using the rule below.
$(foreach fuzzer,$(fuzzers-y),$(eval $(call gen_protobuf_deps,$(fuzzer))))

# TODO: Cannot depend on the corresponding $(srctree)/tools/lkl/%.proto file
# due to the pattern rule
# '$(OUTPUT)%$(EXESUF): $(OUTPUT)%-in.o $(OUTPUT)liblkl.a' above. For POSIX
# target $(EXESUF) is an empty string, thus, target $(OUTPUT)%$(EXESUF) would
# match any source file and will try to make it.
# Update once https://github.com/lkl/linux/issues/573 is fixed.
$(srctree)/tools/lkl/%.pb.cpp $(srctree)/tools/lkl/%.pb.h&: FORCE
	$(PROTOC_PATH) --cpp_out=$(@D) --proto_path=$(@D) $(@F:pb.cpp=proto)
	mv $(@:.cpp=.cc) $@

# END: fuzzing-related build-rules

clean:
	$(call QUIET_CLEAN, vmlinux)$(MAKE) -C ../.. ARCH=lkl $(KOPT) clean
	$(call QUIET_CLEAN, objects)find $(OUTPUT) -name '*.o' -delete -o -name '\.*.cmd'\
	 -delete -o -name '\.*.d' -delete
	$(call QUIET_CLEAN, headers)$(RM) -r $(OUTPUT)/include/lkl/
	$(call QUIET_CLEAN, liblkl.a)$(RM) $(OUTPUT)/liblkl.a
	$(call QUIET_CLEAN, targets)$(RM) $(TARGETS) $(FUZZ_TARGETS) bin/stat
	$(call QUIET_CLEAN, gen_protos)$(RM) $(PROTOC_GENERATED_FILES)

mrproper: clean
	$(call QUIET_CLEAN, vmlinux)$(MAKE) -C ../.. ARCH=lkl $(KOPT) mrproper

clean-conf: mrproper
	$(call QUIET_CLEAN, Makefile.conf)$(RM) $(OUTPUT)/Makefile.conf
	$(call QUIET_CLEAN, kernel_config.h)$(RM) $(OUTPUT)/include/kernel_config.h
	$(call QUIET_CLEAN, kernel.config)$(RM) $(OUTPUT)/kernel.config

headers_install: $(TARGETS)
	$(call QUIET_INSTALL, headers) \
	    install -d $(DESTDIR)$(INCDIR) ; \
	    install -m 644 include/lkl.h include/lkl_host.h $(OUTPUT)include/lkl_autoconf.h \
	      include/lkl_config.h $(DESTDIR)$(INCDIR) ; \
	    cp -r $(OUTPUT)include/lkl $(DESTDIR)$(INCDIR)

libraries_install: $(libs-y:%=$(OUTPUT)%$(SOSUF)) $(OUTPUT)liblkl.a
	$(call QUIET_INSTALL, libraries) \
	    install -d $(DESTDIR)$(LIBDIR) ; \
	    install -m 644 $^ $(DESTDIR)$(LIBDIR)

programs_install: $(call expand-targets,$(progs-y),$(EXESUF))
	$(call QUIET_INSTALL, programs) \
	    install -d $(DESTDIR)$(BINDIR) ; \
	    install -m 755 $^ $(DESTDIR)$(BINDIR)

install: headers_install libraries_install programs_install


run-tests:
	./tests/run.py $(tests)

FORCE: ;
.PHONY: all clean clean-conf mrproper FORCE run-tests
.PHONY: headers_install libraries_install programs_install install
.NOTPARALLEL : lib/lkl.o
.SECONDARY:
.WAIT:
