include scripts/Makefile.include

help:
	@echo 'Possible targets:'
	@echo ''
	@echo '  cpupower   - a tool for all things x86 CPU power'
	@echo '  firewire   - the userspace part of nosy, an IEEE-1394 traffic sniffer'
	@echo '  lguest     - a minimal 32-bit x86 hypervisor'
	@echo '  perf       - Linux performance measurement and analysis tool'
	@echo '  selftests  - various kernel selftests'
	@echo '  turbostat  - Intel CPU idle stats and freq reporting tool'
	@echo '  usb        - USB testing tools'
	@echo '  virtio     - vhost test module'
	@echo '  vm         - misc vm tools'
	@echo '  x86_energy_perf_policy - Intel energy policy tool'
	@echo ''
	@echo 'You can do:'
	@echo ' $$ make -C tools/<tool>_install'
	@echo ''
	@echo '  from the kernel command line to build and install one of'
	@echo '  the tools above'
	@echo ''
	@echo '  $$ make tools/install'
	@echo ''
	@echo '  installs all tools.'
	@echo ''
	@echo 'Cleaning targets:'
	@echo ''
	@echo '  all of the above with the "_clean" string appended cleans'
	@echo '    the respective build directory.'
	@echo '  clean: a summary clean target to clean _all_ folders'

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
