include scripts/Makefile.include

help:
	@echo 'Possible targets:'
	@echo ''
	@echo '  cgroup     - cgroup tools'
	@echo '  cpupower   - a tool for all things x86 CPU power'
	@echo '  firewire   - the userspace part of nosy, an IEEE-1394 traffic sniffer'
	@echo '  lguest     - a minimal 32-bit x86 hypervisor'
	@echo '  perf       - Linux performance measurement and analysis tool'
	@echo '  selftests  - various kernel selftests'
	@echo '  turbostat  - Intel CPU idle stats and freq reporting tool'
	@echo '  usb        - USB testing tools'
	@echo '  virtio     - vhost test module'
	@echo '  net        - misc networking tools'
	@echo '  vm         - misc vm tools'
	@echo '  x86_energy_perf_policy - Intel energy policy tool'
	@echo ''
	@echo 'You can do:'
	@echo ' $$ make -C tools/ <tool>_install'
	@echo ''
	@echo '  from the kernel command line to build and install one of'
	@echo '  the tools above'
	@echo ''
	@echo '  $$ make tools/all'
	@echo ''
	@echo '  builds all tools.'
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
	$(call descend,power/$@)

cgroup firewire guest usb virtio vm net: FORCE
	$(call descend,$@)

liblk: FORCE
	$(call descend,lib/lk)

perf: liblk FORCE
	$(call descend,$@)

selftests: FORCE
	$(call descend,testing/$@)

turbostat x86_energy_perf_policy: FORCE
	$(call descend,power/x86/$@)

all: cgroup cpupower firewire lguest \
		perf selftests turbostat usb \
		virtio vm net x86_energy_perf_policy

cpupower_install:
	$(call descend,power/$(@:_install=),install)

cgroup_install firewire_install lguest_install perf_install usb_install virtio_install vm_install net_install:
	$(call descend,$(@:_install=),install)

selftests_install:
	$(call descend,testing/$(@:_clean=),install)

turbostat_install x86_energy_perf_policy_install:
	$(call descend,power/x86/$(@:_install=),install)

install: cgroup_install cpupower_install firewire_install lguest_install \
		perf_install selftests_install turbostat_install usb_install \
		virtio_install vm_install net_install x86_energy_perf_policy_install

cpupower_clean:
	$(call descend,power/cpupower,clean)

cgroup_clean firewire_clean lguest_clean usb_clean virtio_clean vm_clean net_clean:
	$(call descend,$(@:_clean=),clean)

liblk_clean:
	$(call descend,lib/lk,clean)

perf_clean: liblk_clean
	$(call descend,$(@:_clean=),clean)

selftests_clean:
	$(call descend,testing/$(@:_clean=),clean)

turbostat_clean x86_energy_perf_policy_clean:
	$(call descend,power/x86/$(@:_clean=),clean)

clean: cgroup_clean cpupower_clean firewire_clean lguest_clean perf_clean \
		selftests_clean turbostat_clean usb_clean virtio_clean \
		vm_clean net_clean x86_energy_perf_policy_clean

.PHONY: FORCE
