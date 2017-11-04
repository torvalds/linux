# SPDX-License-Identifier: GPL-2.0
# Some of the tools (perf) use same make variables
# as in kernel build.
export srctree=
export objtree=

include scripts/Makefile.include

help:
	@echo 'Possible targets:'
	@echo ''
	@echo '  acpi                   - ACPI tools'
	@echo '  cgroup                 - cgroup tools'
	@echo '  cpupower               - a tool for all things x86 CPU power'
	@echo '  firewire               - the userspace part of nosy, an IEEE-1394 traffic sniffer'
	@echo '  freefall               - laptop accelerometer program for disk protection'
	@echo '  gpio                   - GPIO tools'
	@echo '  hv                     - tools used when in Hyper-V clients'
	@echo '  iio                    - IIO tools'
	@echo '  kvm_stat               - top-like utility for displaying kvm statistics'
	@echo '  leds                   - LEDs  tools'
	@echo '  liblockdep             - user-space wrapper for kernel locking-validator'
	@echo '  bpf                    - misc BPF tools'
	@echo '  perf                   - Linux performance measurement and analysis tool'
	@echo '  selftests              - various kernel selftests'
	@echo '  spi                    - spi tools'
	@echo '  objtool                - an ELF object analysis tool'
	@echo '  tmon                   - thermal monitoring and tuning tool'
	@echo '  turbostat              - Intel CPU idle stats and freq reporting tool'
	@echo '  usb                    - USB testing tools'
	@echo '  virtio                 - vhost test module'
	@echo '  vm                     - misc vm tools'
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

acpi: FORCE
	$(call descend,power/$@)

cpupower: FORCE
	$(call descend,power/$@)

cgroup firewire hv guest spi usb virtio vm bpf iio gpio objtool leds: FORCE
	$(call descend,$@)

liblockdep: FORCE
	$(call descend,lib/lockdep)

libapi: FORCE
	$(call descend,lib/api)

# The perf build does not follow the descend function setup,
# invoking it via it's own make rule.
PERF_O   = $(if $(O),$(O)/tools/perf,)

perf: FORCE
	$(Q)mkdir -p $(PERF_O) .
	$(Q)$(MAKE) --no-print-directory -C perf O=$(PERF_O) subdir=

selftests: FORCE
	$(call descend,testing/$@)

turbostat x86_energy_perf_policy: FORCE
	$(call descend,power/x86/$@)

tmon: FORCE
	$(call descend,thermal/$@)

freefall: FORCE
	$(call descend,laptop/$@)

kvm_stat: FORCE
	$(call descend,kvm/$@)

all: acpi cgroup cpupower gpio hv firewire liblockdep \
		perf selftests spi turbostat usb \
		virtio vm bpf x86_energy_perf_policy \
		tmon freefall iio objtool kvm_stat

acpi_install:
	$(call descend,power/$(@:_install=),install)

cpupower_install:
	$(call descend,power/$(@:_install=),install)

cgroup_install firewire_install gpio_install hv_install iio_install perf_install spi_install usb_install virtio_install vm_install bpf_install objtool_install:
	$(call descend,$(@:_install=),install)

liblockdep_install:
	$(call descend,lib/lockdep,install)

selftests_install:
	$(call descend,testing/$(@:_install=),install)

turbostat_install x86_energy_perf_policy_install:
	$(call descend,power/x86/$(@:_install=),install)

tmon_install:
	$(call descend,thermal/$(@:_install=),install)

freefall_install:
	$(call descend,laptop/$(@:_install=),install)

kvm_stat_install:
	$(call descend,kvm/$(@:_install=),install)

install: acpi_install cgroup_install cpupower_install gpio_install \
		hv_install firewire_install iio_install liblockdep_install \
		perf_install selftests_install turbostat_install usb_install \
		virtio_install vm_install bpf_install x86_energy_perf_policy_install \
		tmon_install freefall_install objtool_install kvm_stat_install

acpi_clean:
	$(call descend,power/acpi,clean)

cpupower_clean:
	$(call descend,power/cpupower,clean)

cgroup_clean hv_clean firewire_clean spi_clean usb_clean virtio_clean vm_clean bpf_clean iio_clean gpio_clean objtool_clean leds_clean:
	$(call descend,$(@:_clean=),clean)

liblockdep_clean:
	$(call descend,lib/lockdep,clean)

libapi_clean:
	$(call descend,lib/api,clean)

libbpf_clean:
	$(call descend,lib/bpf,clean)

libsubcmd_clean:
	$(call descend,lib/subcmd,clean)

perf_clean:
	$(Q)mkdir -p $(PERF_O) .
	$(Q)$(MAKE) --no-print-directory -C perf O=$(PERF_O) subdir= clean

selftests_clean:
	$(call descend,testing/$(@:_clean=),clean)

turbostat_clean x86_energy_perf_policy_clean:
	$(call descend,power/x86/$(@:_clean=),clean)

tmon_clean:
	$(call descend,thermal/tmon,clean)

freefall_clean:
	$(call descend,laptop/freefall,clean)

build_clean:
	$(call descend,build,clean)

clean: acpi_clean cgroup_clean cpupower_clean hv_clean firewire_clean \
		perf_clean selftests_clean turbostat_clean spi_clean usb_clean virtio_clean \
		vm_clean bpf_clean iio_clean x86_energy_perf_policy_clean tmon_clean \
		freefall_clean build_clean libbpf_clean libsubcmd_clean liblockdep_clean \
		gpio_clean objtool_clean leds_clean

.PHONY: FORCE
