obj-y = audit.o common.o condition.o domain.o environ.o file.o gc.o group.o load_policy.o memory.o mount.o network.o realpath.o securityfs_if.o tomoyo.o util.o

$(obj)/policy/profile.conf:
	@mkdir -p $(obj)/policy/
	@echo Creating an empty policy/profile.conf
	@touch $@

$(obj)/policy/exception_policy.conf:
	@mkdir -p $(obj)/policy/
	@echo Creating a default policy/exception_policy.conf
	@echo initialize_domain /sbin/modprobe from any >> $@
	@echo initialize_domain /sbin/hotplug from any >> $@

$(obj)/policy/domain_policy.conf:
	@mkdir -p $(obj)/policy/
	@echo Creating an empty policy/domain_policy.conf
	@touch $@

$(obj)/policy/manager.conf:
	@mkdir -p $(obj)/policy/
	@echo Creating an empty policy/manager.conf
	@touch $@

$(obj)/policy/stat.conf:
	@mkdir -p $(obj)/policy/
	@echo Creating an empty policy/stat.conf
	@touch $@

targets += builtin-policy.h
define do_policy
echo "static char tomoyo_builtin_$(1)[] __initdata ="; \
$(objtree)/scripts/basic/bin2c <$(obj)/policy/$(1).conf; \
echo ";"
endef
quiet_cmd_policy  = POLICY  $@
      cmd_policy  = ($(call do_policy,profile); $(call do_policy,exception_policy); $(call do_policy,domain_policy); $(call do_policy,manager); $(call do_policy,stat)) >$@

$(obj)/builtin-policy.h: $(obj)/policy/profile.conf $(obj)/policy/exception_policy.conf $(obj)/policy/domain_policy.conf $(obj)/policy/manager.conf $(obj)/policy/stat.conf FORCE
	$(call if_changed,policy)

$(obj)/common.o: $(obj)/builtin-policy.h
