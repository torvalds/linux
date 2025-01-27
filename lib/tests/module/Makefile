obj-$(CONFIG_TEST_KALLSYMS_A) += test_kallsyms_a.o
obj-$(CONFIG_TEST_KALLSYMS_B) += test_kallsyms_b.o
obj-$(CONFIG_TEST_KALLSYMS_C) += test_kallsyms_c.o
obj-$(CONFIG_TEST_KALLSYMS_D) += test_kallsyms_d.o

quiet_cmd_gen_test_kallsyms = GEN     $@
	cmd_gen_test_kallsyms = $< $@ \
	$(CONFIG_TEST_KALLSYMS_NUMSYMS) \
	$(CONFIG_TEST_KALLSYMS_SCALE_FACTOR)

$(obj)/%.c: $(src)/gen_test_kallsyms.sh FORCE
	$(call if_changed,gen_test_kallsyms)

targets += $(foreach x, a b c d, test_kallsyms_$(x).c)
