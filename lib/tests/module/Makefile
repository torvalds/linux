obj-$(CONFIG_TEST_KALLSYMS_A) += test_kallsyms_a.o
obj-$(CONFIG_TEST_KALLSYMS_B) += test_kallsyms_b.o
obj-$(CONFIG_TEST_KALLSYMS_C) += test_kallsyms_c.o
obj-$(CONFIG_TEST_KALLSYMS_D) += test_kallsyms_d.o

$(obj)/%.c: FORCE
	@$(kecho) "  GEN     $@"
	$(Q)$(srctree)/lib/tests/module/gen_test_kallsyms.sh $@\
		$(CONFIG_TEST_KALLSYMS_NUMSYMS) \
		$(CONFIG_TEST_KALLSYMS_SCALE_FACTOR)

clean-files += test_kallsyms_a.c
clean-files += test_kallsyms_b.c
clean-files += test_kallsyms_c.c
clean-files += test_kallsyms_d.c
