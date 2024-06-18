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
	@echo '  bpf                    - misc BPF tools'
	@echo '  counter                - counter tools'
	@echo '  cpupower               - a tool for all things x86 CPU power'
	@echo '  debugging              - tools for debugging'
	@echo '  firewire               - the userspace part of nosy, an IEEE-1394 traffic sniffer'
	@echo '  firmware               - Firmware tools'
	@echo '  freefall               - laptop accelerometer program for disk protection'
	@echo '  gpio                   - GPIO tools'
	@echo '  hv                     - tools used when in Hyper-V clients'
	@echo '  iio                    - IIO tools'
	@echo '  intel-speed-select     - Intel Speed Select tool'
	@echo '  kvm_stat               - top-like utility for displaying kvm statistics'
	@echo '  leds                   - LEDs  tools'
	@echo '  nolibc                 - nolibc headers testing and installation'
	@echo '  objtool                - an ELF object analysis tool'
	@echo '  pci                    - PCI tools'
	@echo '  perf                   - Linux performance measurement and analysis tool'
	@echo '  selftests              - various kernel selftests'
	@echo '  bootconfig             - boot config tool'
	@echo '  spi                    - spi tools'
	@echo '  tmon                   - thermal monitoring and tuning tool'
	@echo '  thermometer            - temperature capture tool'
	@echo '  thermal-engine         - thermal monitoring tool'
	@echo '  thermal                - thermal library'
	@echo '  tracing                - misc tracing tools'
	@echo '  turbostat              - Intel CPU idle stats and freq reporting tool'
	@echo '  usb                    - USB testing tools'
	@echo '  virtio                 - vhost test module'
	@echo '  mm                     - misc mm tools'
	@echo '  wmi			- WMI interface examples'
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

counter firewire hv guest bootconfig spi usb virtio mm bpf iio gpio objtool leds wmi pci firmware debugging tracing: FORCE
	$(call descend,$@)

bpf/%: FORCE
	$(call descend,$@)

libapi: FORCE
	$(call descend,lib/api)

nolibc: FORCE
	$(call descend,include/nolibc)

nolibc_%: FORCE
	$(call descend,include/nolibc,$(patsubst nolibc_%,%,$@))

# The perf build does not follow the descend function setup,
# invoking it via it's own make rule.
PERF_O   = $(if $(O),$(O)/tools/perf,)

perf: FORCE
	$(Q)mkdir -p $(PERF_O) .
	$(Q)$(MAKE) --no-print-directory -C perf O=$(PERF_O) subdir=

selftests: FORCE
	$(call descend,testing/$@)

thermal: FORCE
	$(call descend,lib/$@)

turbostat x86_energy_perf_policy intel-speed-select: FORCE
	$(call descend,power/x86/$@)

tmon: FORCE
	$(call descend,thermal/$@)

thermometer: FORCE
	$(call descend,thermal/$@)

thermal-engine: FORCE thermal
	$(call descend,thermal/$@)

freefall: FORCE
	$(call descend,laptop/$@)

kvm_stat: FORCE
	$(call descend,kvm/$@)

all: acpi counter cpupower gpio hv firewire \
		perf selftests bootconfig spi turbostat usb \
		virtio mm bpf x86_energy_perf_policy \
		tmon freefall iio objtool kvm_stat wmi \
		pci debugging tracing thermal thermometer thermal-engine

acpi_install:
	$(call descend,power/$(@:_install=),install)

cpupower_install:
	$(call descend,power/$(@:_install=),install)

counter_install firewire_install gpio_install hv_install iio_install perf_install bootconfig_install spi_install usb_install virtio_install mm_install bpf_install objtool_install wmi_install pci_install debugging_install tracing_install:
	$(call descend,$(@:_install=),install)

selftests_install:
	$(call descend,testing/$(@:_install=),install)

thermal_install:
	$(call descend,lib/$(@:_install=),install)

turbostat_install x86_energy_perf_policy_install intel-speed-select_install:
	$(call descend,power/x86/$(@:_install=),install)

tmon_install:
	$(call descend,thermal/$(@:_install=),install)

thermometer_install:
	$(call descend,thermal/$(@:_install=),install)

thermal-engine_install:
	$(call descend,thermal/$(@:_install=),install)

freefall_install:
	$(call descend,laptop/$(@:_install=),install)

kvm_stat_install:
	$(call descend,kvm/$(@:_install=),install)

install: acpi_install counter_install cpupower_install gpio_install \
		hv_install firewire_install iio_install \
		perf_install selftests_install turbostat_install usb_install \
		virtio_install mm_install bpf_install x86_energy_perf_policy_install \
		tmon_install freefall_install objtool_install kvm_stat_install \
		wmi_install pci_install debugging_install intel-speed-select_install \
		tracing_install thermometer_install thermal-engine_install

acpi_clean:
	$(call descend,power/acpi,clean)

cpupower_clean:
	$(call descend,power/cpupower,clean)

counter_clean hv_clean firewire_clean bootconfig_clean spi_clean usb_clean virtio_clean mm_clean wmi_clean bpf_clean iio_clean gpio_clean objtool_clean leds_clean pci_clean firmware_clean debugging_clean tracing_clean:
	$(call descend,$(@:_clean=),clean)

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

thermal_clean:
	$(call descend,lib/thermal,clean)

turbostat_clean x86_energy_perf_policy_clean intel-speed-select_clean:
	$(call descend,power/x86/$(@:_clean=),clean)

thermometer_clean:
	$(call descend,thermal/thermometer,clean)

thermal-engine_clean:
	$(call descend,thermal/thermal-engine,clean)

tmon_clean:
	$(call descend,thermal/tmon,clean)

freefall_clean:
	$(call descend,laptop/freefall,clean)

build_clean:
	$(call descend,build,clean)

clean: acpi_clean counter_clean cpupower_clean hv_clean firewire_clean \
		perf_clean selftests_clean turbostat_clean bootconfig_clean spi_clean usb_clean virtio_clean \
		mm_clean bpf_clean iio_clean x86_energy_perf_policy_clean tmon_clean \
		freefall_clean build_clean libbpf_clean libsubcmd_clean \
		gpio_clean objtool_clean leds_clean wmi_clean pci_clean firmware_clean debugging_clean \
		intel-speed-select_clean tracing_clean thermal_clean thermometer_clean thermal-engine_clean

.PHONY: FORCE
