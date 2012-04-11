include scripts/Makefile.include

cpupower: FORCE
	$(QUIET_SUBDIR0)power/$@/ $(QUIET_SUBDIR1)

firewire lguest perf usb virtio vm: FORCE
	$(QUIET_SUBDIR0)$@/ $(QUIET_SUBDIR1)

selftests: FORCE
	$(QUIET_SUBDIR0)testing/$@/ $(QUIET_SUBDIR1)

turbostat x86_energy_perf_policy: FORCE
	$(QUIET_SUBDIR0)power/x86/$@/ $(QUIET_SUBDIR1)

cpupower_install:
	$(QUIET_SUBDIR0)power/$(@:_install=)/ $(QUIET_SUBDIR1) install

firewire_install lguest_install perf_install usb_install virtio_install vm_install:
	$(QUIET_SUBDIR0)$(@:_install=)/ $(QUIET_SUBDIR1) install

selftests_install:
	$(QUIET_SUBDIR0)testing/$(@:_clean=)/ $(QUIET_SUBDIR1) install

turbostat_install x86_energy_perf_policy_install:
	$(QUIET_SUBDIR0)power/x86/$(@:_install=)/ $(QUIET_SUBDIR1) install

install: cpupower_install firewire_install lguest_install perf_install \
		selftests_install turbostat_install usb_install virtio_install \
		vm_install x86_energy_perf_policy_install

cpupower_clean:
	$(QUIET_SUBDIR0)power/cpupower/ $(QUIET_SUBDIR1) clean

firewire_clean lguest_clean perf_clean usb_clean virtio_clean vm_clean:
	$(QUIET_SUBDIR0)$(@:_clean=)/ $(QUIET_SUBDIR1) clean

selftests_clean:
	$(QUIET_SUBDIR0)testing/$(@:_clean=)/ $(QUIET_SUBDIR1) clean

turbostat_clean x86_energy_perf_policy_clean:
	$(QUIET_SUBDIR0)power/x86/$(@:_clean=)/ $(QUIET_SUBDIR1) clean

clean: cpupower_clean firewire_clean lguest_clean perf_clean selftests_clean \
		turbostat_clean usb_clean virtio_clean vm_clean \
		x86_energy_perf_policy_clean

.PHONY: FORCE
