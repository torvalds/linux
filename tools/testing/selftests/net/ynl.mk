# SPDX-License-Identifier: GPL-2.0

# YNL selftest build snippet

# Inputs:
#
# YNL_GENS:      families we need in the selftests
# YNL_GEN_PROGS: TEST_GEN_PROGS which need YNL
# YNL_GEN_FILES: TEST_GEN_FILES which need YNL

YNL_OUTPUTS :=	$(patsubst %,$(OUTPUT)/%,$(YNL_GEN_FILES)) \
		$(patsubst %,$(OUTPUT)/%,$(YNL_GEN_PROGS))
YNL_SPECS := \
	$(patsubst %,$(top_srcdir)/Documentation/netlink/specs/%.yaml,$(YNL_GENS))

$(YNL_OUTPUTS): $(OUTPUT)/libynl.a
$(YNL_OUTPUTS): CFLAGS += \
	-I$(top_srcdir)/usr/include/ $(KHDR_INCLUDES) \
	-I$(top_srcdir)/tools/net/ynl/lib/ \
	-I$(top_srcdir)/tools/net/ynl/generated/

# Make sure we rebuild libynl if user added a new family. We can't easily
# depend on the contents of a variable so create a fake file with a hash.
YNL_GENS_HASH := $(shell echo $(YNL_GENS) | sha1sum | cut -c1-8)
$(OUTPUT)/.libynl-$(YNL_GENS_HASH).sig:
	$(Q)rm -f $(OUTPUT)/.libynl-*.sig
	$(Q)touch $(OUTPUT)/.libynl-$(YNL_GENS_HASH).sig

$(OUTPUT)/libynl.a: $(YNL_SPECS) $(OUTPUT)/.libynl-$(YNL_GENS_HASH).sig
	$(Q)rm -f $(top_srcdir)/tools/net/ynl/libynl.a
	$(Q)$(MAKE) -C $(top_srcdir)/tools/net/ynl \
		GENS="$(YNL_GENS)" RSTS="" libynl.a
	$(Q)cp $(top_srcdir)/tools/net/ynl/libynl.a $(OUTPUT)/libynl.a

EXTRA_CLEAN += \
	$(top_srcdir)/tools/net/ynl/pyynl/__pycache__ \
	$(top_srcdir)/tools/net/ynl/pyynl/lib/__pycache__ \
	$(top_srcdir)/tools/net/ynl/lib/*.[ado] \
	$(OUTPUT)/.libynl-*.sig \
	$(OUTPUT)/libynl.a
