# SPDX-License-Identifier: GPL-2.0

# YNL selftest build snippet

# Inputs:
#
# YNL_GENS:      families we need in the selftests
# YNL_PROGS:     TEST_PROGS which need YNL (TODO, none exist, yet)
# YNL_GEN_FILES: TEST_GEN_FILES which need YNL

YNL_OUTPUTS := $(patsubst %,$(OUTPUT)/%,$(YNL_GEN_FILES))

$(YNL_OUTPUTS): $(OUTPUT)/libynl.a
$(YNL_OUTPUTS): CFLAGS += \
	-I$(top_srcdir)/usr/include/ $(KHDR_INCLUDES) \
	-I$(top_srcdir)/tools/net/ynl/lib/ \
	-I$(top_srcdir)/tools/net/ynl/generated/

$(OUTPUT)/libynl.a:
	$(Q)$(MAKE) -C $(top_srcdir)/tools/net/ynl GENS="$(YNL_GENS)" libynl.a
	$(Q)cp $(top_srcdir)/tools/net/ynl/libynl.a $(OUTPUT)/libynl.a
