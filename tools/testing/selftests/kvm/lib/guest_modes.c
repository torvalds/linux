// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020, Red Hat, Inc.
 */
#include "guest_modes.h"

struct guest_mode guest_modes[NUM_VM_MODES];

void guest_modes_append_default(void)
{
	guest_mode_append(VM_MODE_DEFAULT, true, true);

#ifdef __aarch64__
	guest_mode_append(VM_MODE_P40V48_64K, true, true);
	{
		unsigned int limit = kvm_check_cap(KVM_CAP_ARM_VM_IPA_SIZE);
		if (limit >= 52)
			guest_mode_append(VM_MODE_P52V48_64K, true, true);
		if (limit >= 48) {
			guest_mode_append(VM_MODE_P48V48_4K, true, true);
			guest_mode_append(VM_MODE_P48V48_64K, true, true);
		}
	}
#endif
#ifdef __s390x__
	{
		int kvm_fd, vm_fd;
		struct kvm_s390_vm_cpu_processor info;

		kvm_fd = open_kvm_dev_path_or_exit();
		vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);
		kvm_device_access(vm_fd, KVM_S390_VM_CPU_MODEL,
				  KVM_S390_VM_CPU_PROCESSOR, &info, false);
		close(vm_fd);
		close(kvm_fd);
		/* Starting with z13 we have 47bits of physical address */
		if (info.ibc >= 0x30)
			guest_mode_append(VM_MODE_P47V64_4K, true, true);
	}
#endif
}

void for_each_guest_mode(void (*func)(enum vm_guest_mode, void *), void *arg)
{
	int i;

	for (i = 0; i < NUM_VM_MODES; ++i) {
		if (!guest_modes[i].enabled)
			continue;
		TEST_ASSERT(guest_modes[i].supported,
			    "Guest mode ID %d (%s) not supported.",
			    i, vm_guest_mode_string(i));
		func(i, arg);
	}
}

void guest_modes_help(void)
{
	int i;

	printf(" -m: specify the guest mode ID to test\n"
	       "     (default: test all supported modes)\n"
	       "     This option may be used multiple times.\n"
	       "     Guest mode IDs:\n");
	for (i = 0; i < NUM_VM_MODES; ++i) {
		printf("         %d:    %s%s\n", i, vm_guest_mode_string(i),
		       guest_modes[i].supported ? " (supported)" : "");
	}
}

void guest_modes_cmdline(const char *arg)
{
	static bool mode_selected;
	unsigned int mode;
	int i;

	if (!mode_selected) {
		for (i = 0; i < NUM_VM_MODES; ++i)
			guest_modes[i].enabled = false;
		mode_selected = true;
	}

	mode = strtoul(optarg, NULL, 10);
	TEST_ASSERT(mode < NUM_VM_MODES, "Guest mode ID %d too big", mode);
	guest_modes[mode].enabled = true;
}
