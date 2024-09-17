# SPDX-License-Identifier: GPL-2.0
obj-y = audit.o common.o condition.o domain.o environ.o file.o gc.o group.o load_policy.o memory.o mount.o network.o realpath.o securityfs_if.o tomoyo.o util.o

targets += builtin-policy.h
define do_policy
echo "static char tomoyo_builtin_$(1)[] __initdata ="; \
$(objtree)/scripts/bin2c <$(firstword $(wildcard $(obj)/policy/$(1).conf $(srctree)/$(src)/policy/$(1).conf.default) /dev/null); \
echo ";"
endef
quiet_cmd_policy  = POLICY  $@
      cmd_policy  = ($(call do_policy,profile); $(call do_policy,exception_policy); $(call do_policy,domain_policy); $(call do_policy,manager); $(call do_policy,stat)) >$@

$(obj)/builtin-policy.h: $(wildcard $(obj)/policy/*.conf $(srctree)/$(src)/policy/*.conf.default) FORCE
	$(call if_changed,policy)

$(obj)/common.o: $(obj)/builtin-policy.h
