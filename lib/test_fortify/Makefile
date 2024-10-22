# SPDX-License-Identifier: GPL-2.0

ccflags-y := $(call cc-disable-warning,fortify-source)

quiet_cmd_test_fortify = TEST    $@
      cmd_test_fortify = $(CONFIG_SHELL) $(src)/test_fortify.sh \
			$< $@ "$(NM)" $(CC) $(c_flags) -DKBUILD_EXTRA_WARN1

$(obj)/%.log: $(src)/%.c $(src)/test_fortify.sh FORCE
	$(call if_changed_dep,test_fortify)

logs = $(patsubst $(src)/%.c, %.log, $(wildcard $(src)/*-*.c))
targets += $(logs)

quiet_cmd_gen_fortify_log = CAT     $@
      cmd_gen_fortify_log = cat $(or $(real-prereqs),/dev/null) > $@

$(obj)/test_fortify.log: $(addprefix $(obj)/, $(logs)) FORCE
	$(call if_changed,gen_fortify_log)

# GCC<=7 does not always produce *.d files.
# Run the tests only for GCC>=8 or Clang.
always-$(call gcc-min-version, 80000) += test_fortify.log
always-$(CONFIG_CC_IS_CLANG)          += test_fortify.log

# Some architectures define __NO_FORTIFY if __SANITIZE_ADDRESS__ is undefined.
# Pass CFLAGS_KASAN to avoid warnings.
KASAN_SANITIZE := y
