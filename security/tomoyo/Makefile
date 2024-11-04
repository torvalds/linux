# SPDX-License-Identifier: GPL-2.0
obj-y = audit.o common.o condition.o domain.o environ.o file.o gc.o group.o load_policy.o memory.o mount.o network.o realpath.o securityfs_if.o tomoyo.o util.o

targets += builtin-policy.h

quiet_cmd_policy = POLICY  $@
      cmd_policy = { \
	$(foreach x, profile exception_policy domain_policy manager stat, \
	printf 'static char tomoyo_builtin_$x[] __initdata =\n'; \
	sed -e 's/\\/\\\\/g' -e 's/\"/\\"/g' -e 's/\(.*\)/\t"\1\\n"/' -- $(firstword $(filter %/$x.conf %/$x.conf.default, $^) /dev/null);  \
	printf '\t"";\n';) \
	} > $@

$(obj)/builtin-policy.h: $(wildcard $(obj)/policy/*.conf $(src)/policy/*.conf.default) FORCE
	$(call if_changed,policy)

ifndef CONFIG_SECURITY_TOMOYO_INSECURE_BUILTIN_SETTING
$(obj)/common.o: $(obj)/builtin-policy.h
endif
