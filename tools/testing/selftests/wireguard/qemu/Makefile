# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.

PWD := $(shell pwd)

CHOST := $(shell gcc -dumpmachine)
HOST_ARCH := $(firstword $(subst -, ,$(CHOST)))
ifneq (,$(ARCH))
CBUILD := $(subst -gcc,,$(lastword $(subst /, ,$(firstword $(wildcard $(foreach bindir,$(subst :, ,$(PATH)),$(bindir)/$(ARCH)-*-gcc))))))
ifeq (,$(CBUILD))
$(error The toolchain for $(ARCH) is not installed)
endif
else
CBUILD := $(CHOST)
ARCH := $(firstword $(subst -, ,$(CBUILD)))
endif

# Set these from the environment to override
KERNEL_PATH ?= $(PWD)/../../../../..
BUILD_PATH ?= $(PWD)/build/$(ARCH)
DISTFILES_PATH ?= $(PWD)/distfiles
NR_CPUS ?= 4

MIRROR := https://download.wireguard.com/qemu-test/distfiles/

default: qemu

# variable name, tarball project name, version, tarball extension, default URI base
define tar_download =
$(1)_VERSION := $(3)
$(1)_NAME := $(2)-$$($(1)_VERSION)
$(1)_TAR := $(DISTFILES_PATH)/$$($(1)_NAME)$(4)
$(1)_PATH := $(BUILD_PATH)/$$($(1)_NAME)
$(call file_download,$$($(1)_NAME)$(4),$(5),$(6))
endef

define file_download =
$(DISTFILES_PATH)/$(1):
	mkdir -p $(DISTFILES_PATH)
	flock -x $$@.lock -c '[ -f $$@ ] && exit 0; wget -O $$@.tmp $(MIRROR)$(1) || wget -O $$@.tmp $(2)$(1) || rm -f $$@.tmp; [ -f $$@.tmp ] || exit 1; if echo "$(3)  $$@.tmp" | sha256sum -c -; then mv $$@.tmp $$@; else rm -f $$@.tmp; exit 71; fi'
endef

$(eval $(call tar_download,MUSL,musl,1.2.0,.tar.gz,https://musl.libc.org/releases/,c6de7b191139142d3f9a7b5b702c9cae1b5ee6e7f57e582da9328629408fd4e8))
$(eval $(call tar_download,IPERF,iperf,3.7,.tar.gz,https://downloads.es.net/pub/iperf/,d846040224317caf2f75c843d309a950a7db23f9b44b94688ccbe557d6d1710c))
$(eval $(call tar_download,BASH,bash,5.0,.tar.gz,https://ftp.gnu.org/gnu/bash/,b4a80f2ac66170b2913efbfb9f2594f1f76c7b1afd11f799e22035d63077fb4d))
$(eval $(call tar_download,IPROUTE2,iproute2,5.6.0,.tar.xz,https://www.kernel.org/pub/linux/utils/net/iproute2/,1b5b0e25ce6e23da7526ea1da044e814ad85ba761b10dd29c2b027c056b04692))
$(eval $(call tar_download,IPTABLES,iptables,1.8.4,.tar.bz2,https://www.netfilter.org/projects/iptables/files/,993a3a5490a544c2cbf2ef15cf7e7ed21af1845baf228318d5c36ef8827e157c))
$(eval $(call tar_download,NMAP,nmap,7.80,.tar.bz2,https://nmap.org/dist/,fcfa5a0e42099e12e4bf7a68ebe6fde05553383a682e816a7ec9256ab4773faa))
$(eval $(call tar_download,IPUTILS,iputils,s20190709,.tar.gz,https://github.com/iputils/iputils/archive/s20190709.tar.gz/#,a15720dd741d7538dd2645f9f516d193636ae4300ff7dbc8bfca757bf166490a))
$(eval $(call tar_download,WIREGUARD_TOOLS,wireguard-tools,1.0.20200206,.tar.xz,https://git.zx2c4.com/wireguard-tools/snapshot/,f5207248c6a3c3e3bfc9ab30b91c1897b00802ed861e1f9faaed873366078c64))

KERNEL_BUILD_PATH := $(BUILD_PATH)/kernel$(if $(findstring yes,$(DEBUG_KERNEL)),-debug)
rwildcard=$(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2) $(filter $(subst *,%,$2),$d))
WIREGUARD_SOURCES := $(call rwildcard,$(KERNEL_PATH)/drivers/net/wireguard/,*)

export CFLAGS ?= -O3 -pipe
export LDFLAGS ?=
export CPPFLAGS := -I$(BUILD_PATH)/include

ifeq ($(HOST_ARCH),$(ARCH))
CROSS_COMPILE_FLAG := --host=$(CHOST)
CFLAGS += -march=native
STRIP := strip
else
$(info Cross compilation: building for $(CBUILD) using $(CHOST))
CROSS_COMPILE_FLAG := --build=$(CBUILD) --host=$(CHOST)
export CROSS_COMPILE=$(CBUILD)-
STRIP := $(CBUILD)-strip
endif
ifeq ($(ARCH),aarch64)
QEMU_ARCH := aarch64
KERNEL_ARCH := arm64
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/arm64/boot/Image
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host -machine virt,gic_version=host,accel=kvm
else
QEMU_MACHINE := -cpu cortex-a53 -machine virt
CFLAGS += -march=armv8-a -mtune=cortex-a53
endif
else ifeq ($(ARCH),aarch64_be)
QEMU_ARCH := aarch64
KERNEL_ARCH := arm64
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/arm64/boot/Image
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host -machine virt,gic_version=host,accel=kvm
else
QEMU_MACHINE := -cpu cortex-a53 -machine virt
CFLAGS += -march=armv8-a -mtune=cortex-a53
endif
else ifeq ($(ARCH),arm)
QEMU_ARCH := arm
KERNEL_ARCH := arm
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/arm/boot/zImage
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host -machine virt,gic_version=host,accel=kvm
else
QEMU_MACHINE := -cpu cortex-a15 -machine virt
CFLAGS += -march=armv7-a -mtune=cortex-a15 -mabi=aapcs-linux
endif
else ifeq ($(ARCH),armeb)
QEMU_ARCH := arm
KERNEL_ARCH := arm
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/arm/boot/zImage
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host -machine virt,gic_version=host,accel=kvm
else
QEMU_MACHINE := -cpu cortex-a15 -machine virt
CFLAGS += -march=armv7-a -mabi=aapcs-linux # We don't pass -mtune=cortex-a15 due to a compiler bug on big endian.
LDFLAGS += -Wl,--be8
endif
else ifeq ($(ARCH),x86_64)
QEMU_ARCH := x86_64
KERNEL_ARCH := x86_64
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/x86/boot/bzImage
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host -machine q35,accel=kvm
else
QEMU_MACHINE := -cpu Skylake-Server -machine q35
CFLAGS += -march=skylake-avx512
endif
else ifeq ($(ARCH),i686)
QEMU_ARCH := i386
KERNEL_ARCH := x86
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/x86/boot/bzImage
ifeq ($(subst x86_64,i686,$(HOST_ARCH)),$(ARCH))
QEMU_MACHINE := -cpu host -machine q35,accel=kvm
else
QEMU_MACHINE := -cpu coreduo -machine q35
CFLAGS += -march=prescott
endif
else ifeq ($(ARCH),mips64)
QEMU_ARCH := mips64
KERNEL_ARCH := mips
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/vmlinux
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host -machine malta,accel=kvm
CFLAGS += -EB
else
QEMU_MACHINE := -cpu MIPS64R2-generic -machine malta -smp 1
CFLAGS += -march=mips64r2 -EB
endif
else ifeq ($(ARCH),mips64el)
QEMU_ARCH := mips64el
KERNEL_ARCH := mips
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/vmlinux
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host -machine malta,accel=kvm
CFLAGS += -EL
else
QEMU_MACHINE := -cpu MIPS64R2-generic -machine malta -smp 1
CFLAGS += -march=mips64r2 -EL
endif
else ifeq ($(ARCH),mips)
QEMU_ARCH := mips
KERNEL_ARCH := mips
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/vmlinux
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host -machine malta,accel=kvm
CFLAGS += -EB
else
QEMU_MACHINE := -cpu 24Kf -machine malta -smp 1
CFLAGS += -march=mips32r2 -EB
endif
else ifeq ($(ARCH),mipsel)
QEMU_ARCH := mipsel
KERNEL_ARCH := mips
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/vmlinux
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host -machine malta,accel=kvm
CFLAGS += -EL
else
QEMU_MACHINE := -cpu 24Kf -machine malta -smp 1
CFLAGS += -march=mips32r2 -EL
endif
else ifeq ($(ARCH),powerpc64le)
QEMU_ARCH := ppc64
KERNEL_ARCH := powerpc
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/vmlinux
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host,accel=kvm -machine pseries
else
QEMU_MACHINE := -machine pseries
endif
CFLAGS += -mcpu=powerpc64le -mlong-double-64
else ifeq ($(ARCH),powerpc)
QEMU_ARCH := ppc
KERNEL_ARCH := powerpc
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/powerpc/boot/uImage
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host,accel=kvm -machine ppce500
else
QEMU_MACHINE := -machine ppce500
endif
CFLAGS += -mcpu=powerpc -mlong-double-64 -msecure-plt
else ifeq ($(ARCH),m68k)
QEMU_ARCH := m68k
KERNEL_ARCH := m68k
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/vmlinux
KERNEL_CMDLINE := $(shell sed -n 's/CONFIG_CMDLINE=\(.*\)/\1/p' arch/m68k.config)
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host,accel=kvm -machine q800 -smp 1 -append $(KERNEL_CMDLINE)
else
QEMU_MACHINE := -machine q800 -smp 1 -append $(KERNEL_CMDLINE)
endif
else
$(error I only build: x86_64, i686, arm, armeb, aarch64, aarch64_be, mips, mipsel, mips64, mips64el, powerpc64le, powerpc, m68k)
endif

REAL_CC := $(CBUILD)-gcc
MUSL_CC := $(BUILD_PATH)/musl-gcc
export CC := $(MUSL_CC)
USERSPACE_DEPS := $(MUSL_CC) $(BUILD_PATH)/include/.installed $(BUILD_PATH)/include/linux/.installed

build: $(KERNEL_BZIMAGE)
qemu: $(KERNEL_BZIMAGE)
	rm -f $(BUILD_PATH)/result
	timeout --foreground 20m qemu-system-$(QEMU_ARCH) \
		-nodefaults \
		-nographic \
		-smp $(NR_CPUS) \
		$(QEMU_MACHINE) \
		-m $$(grep -q CONFIG_DEBUG_KMEMLEAK=y $(KERNEL_BUILD_PATH)/.config && echo 1G || echo 256M) \
		-serial stdio \
		-serial file:$(BUILD_PATH)/result \
		-no-reboot \
		-monitor none \
		-kernel $<
	grep -Fq success $(BUILD_PATH)/result

$(BUILD_PATH)/init-cpio-spec.txt:
	mkdir -p $(BUILD_PATH)
	echo "file /init $(BUILD_PATH)/init 755 0 0" > $@
	echo "file /init.sh $(PWD)/../netns.sh 755 0 0" >> $@
	echo "dir /dev 755 0 0" >> $@
	echo "nod /dev/console 644 0 0 c 5 1" >> $@
	echo "dir /bin 755 0 0" >> $@
	echo "file /bin/iperf3 $(IPERF_PATH)/src/iperf3 755 0 0" >> $@
	echo "file /bin/wg $(WIREGUARD_TOOLS_PATH)/src/wg 755 0 0" >> $@
	echo "file /bin/bash $(BASH_PATH)/bash 755 0 0" >> $@
	echo "file /bin/ip $(IPROUTE2_PATH)/ip/ip 755 0 0" >> $@
	echo "file /bin/ss $(IPROUTE2_PATH)/misc/ss 755 0 0" >> $@
	echo "file /bin/ping $(IPUTILS_PATH)/ping 755 0 0" >> $@
	echo "file /bin/ncat $(NMAP_PATH)/ncat/ncat 755 0 0" >> $@
	echo "file /bin/xtables-legacy-multi $(IPTABLES_PATH)/iptables/xtables-legacy-multi 755 0 0" >> $@
	echo "slink /bin/iptables xtables-legacy-multi 777 0 0" >> $@
	echo "slink /bin/ping6 ping 777 0 0" >> $@
	echo "dir /lib 755 0 0" >> $@
	echo "file /lib/libc.so $(MUSL_PATH)/lib/libc.so 755 0 0" >> $@
	echo "slink /lib/ld-linux.so.1 libc.so 777 0 0" >> $@

$(KERNEL_BUILD_PATH)/.config: kernel.config arch/$(ARCH).config
	mkdir -p $(KERNEL_BUILD_PATH)
	cp kernel.config $(KERNEL_BUILD_PATH)/minimal.config
	printf 'CONFIG_NR_CPUS=$(NR_CPUS)\nCONFIG_INITRAMFS_SOURCE="$(BUILD_PATH)/init-cpio-spec.txt"\n' >> $(KERNEL_BUILD_PATH)/minimal.config
	cat arch/$(ARCH).config >> $(KERNEL_BUILD_PATH)/minimal.config
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_BUILD_PATH) ARCH=$(KERNEL_ARCH) allnoconfig
	cd $(KERNEL_BUILD_PATH) && ARCH=$(KERNEL_ARCH) $(KERNEL_PATH)/scripts/kconfig/merge_config.sh -n $(KERNEL_BUILD_PATH)/.config $(KERNEL_BUILD_PATH)/minimal.config
	$(if $(findstring yes,$(DEBUG_KERNEL)),cp debug.config $(KERNEL_BUILD_PATH) && cd $(KERNEL_BUILD_PATH) && ARCH=$(KERNEL_ARCH) $(KERNEL_PATH)/scripts/kconfig/merge_config.sh -n $(KERNEL_BUILD_PATH)/.config debug.config,)

$(KERNEL_BZIMAGE): $(KERNEL_BUILD_PATH)/.config $(BUILD_PATH)/init-cpio-spec.txt $(MUSL_PATH)/lib/libc.so $(IPERF_PATH)/src/iperf3 $(IPUTILS_PATH)/ping $(BASH_PATH)/bash $(IPROUTE2_PATH)/misc/ss $(IPROUTE2_PATH)/ip/ip $(IPTABLES_PATH)/iptables/xtables-legacy-multi $(NMAP_PATH)/ncat/ncat $(WIREGUARD_TOOLS_PATH)/src/wg $(BUILD_PATH)/init ../netns.sh $(WIREGUARD_SOURCES)
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_BUILD_PATH) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(CROSS_COMPILE)

$(BUILD_PATH)/include/linux/.installed: | $(KERNEL_BUILD_PATH)/.config
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_BUILD_PATH) INSTALL_HDR_PATH=$(BUILD_PATH) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(CROSS_COMPILE) headers_install
	touch $@

$(MUSL_PATH)/lib/libc.so: $(MUSL_TAR)
	mkdir -p $(BUILD_PATH)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	cd $(MUSL_PATH) && CC=$(REAL_CC) ./configure --prefix=/ --disable-static --build=$(CBUILD)
	$(MAKE) -C $(MUSL_PATH)
	$(STRIP) -s $@

$(BUILD_PATH)/include/.installed: $(MUSL_PATH)/lib/libc.so
	$(MAKE) -C $(MUSL_PATH) DESTDIR=$(BUILD_PATH) install-headers
	touch $@

$(MUSL_CC): $(MUSL_PATH)/lib/libc.so
	sh $(MUSL_PATH)/tools/musl-gcc.specs.sh $(BUILD_PATH)/include $(MUSL_PATH)/lib /lib/ld-linux.so.1 > $(BUILD_PATH)/musl-gcc.specs
	printf '#!/bin/sh\nexec "$(REAL_CC)" --specs="$(BUILD_PATH)/musl-gcc.specs" "$$@"\n' > $(BUILD_PATH)/musl-gcc
	chmod +x $(BUILD_PATH)/musl-gcc

$(IPERF_PATH)/.installed: $(IPERF_TAR)
	mkdir -p $(BUILD_PATH)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	sed -i '1s/^/#include <stdint.h>/' $(IPERF_PATH)/src/cjson.h $(IPERF_PATH)/src/timer.h
	sed -i -r 's/-p?g//g' $(IPERF_PATH)/src/Makefile*
	touch $@

$(IPERF_PATH)/src/iperf3: | $(IPERF_PATH)/.installed $(USERSPACE_DEPS)
	cd $(IPERF_PATH) && CFLAGS="$(CFLAGS) -D_GNU_SOURCE" ./configure --prefix=/ $(CROSS_COMPILE_FLAG) --enable-static --disable-shared --with-openssl=no
	$(MAKE) -C $(IPERF_PATH)
	$(STRIP) -s $@

$(WIREGUARD_TOOLS_PATH)/.installed: $(WIREGUARD_TOOLS_TAR)
	mkdir -p $(BUILD_PATH)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	touch $@

$(WIREGUARD_TOOLS_PATH)/src/wg: | $(WIREGUARD_TOOLS_PATH)/.installed $(USERSPACE_DEPS)
	$(MAKE) -C $(WIREGUARD_TOOLS_PATH)/src wg
	$(STRIP) -s $@

$(BUILD_PATH)/init: init.c | $(USERSPACE_DEPS)
	mkdir -p $(BUILD_PATH)
	$(MUSL_CC) -o $@ $(CFLAGS) $(LDFLAGS) -std=gnu11 $<
	$(STRIP) -s $@

$(IPUTILS_PATH)/.installed: $(IPUTILS_TAR)
	mkdir -p $(BUILD_PATH)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	touch $@

$(IPUTILS_PATH)/ping: | $(IPUTILS_PATH)/.installed $(USERSPACE_DEPS)
	sed -i /atexit/d $(IPUTILS_PATH)/ping.c
	cd $(IPUTILS_PATH) && $(CC) $(CFLAGS) -std=c99 -o $@ ping.c ping_common.c ping6_common.c iputils_common.c -D_GNU_SOURCE -D'IPUTILS_VERSION(f)=f' -lresolv $(LDFLAGS)
	$(STRIP) -s $@

$(BASH_PATH)/.installed: $(BASH_TAR)
	mkdir -p $(BUILD_PATH)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	touch $@

$(BASH_PATH)/bash: | $(BASH_PATH)/.installed $(USERSPACE_DEPS)
	cd $(BASH_PATH) && ./configure --prefix=/ $(CROSS_COMPILE_FLAG) --without-bash-malloc --disable-debugger --disable-help-builtin --disable-history --disable-multibyte --disable-progcomp --disable-readline --disable-mem-scramble
	$(MAKE) -C $(BASH_PATH)
	$(STRIP) -s $@

$(IPROUTE2_PATH)/.installed: $(IPROUTE2_TAR)
	mkdir -p $(BUILD_PATH)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	printf 'CC:=$(CC)\nPKG_CONFIG:=pkg-config\nTC_CONFIG_XT:=n\nTC_CONFIG_ATM:=n\nTC_CONFIG_IPSET:=n\nIP_CONFIG_SETNS:=y\nHAVE_ELF:=n\nHAVE_MNL:=n\nHAVE_BERKELEY_DB:=n\nHAVE_LATEX:=n\nHAVE_PDFLATEX:=n\nCFLAGS+=-DHAVE_SETNS\n' > $(IPROUTE2_PATH)/config.mk
	printf 'lib: snapshot\n\t$$(MAKE) -C lib\nip/ip: lib\n\t$$(MAKE) -C ip ip\nmisc/ss: lib\n\t$$(MAKE) -C misc ss\n' >> $(IPROUTE2_PATH)/Makefile
	touch $@

$(IPROUTE2_PATH)/ip/ip: | $(IPROUTE2_PATH)/.installed $(USERSPACE_DEPS)
	$(MAKE) -C $(IPROUTE2_PATH) PREFIX=/ ip/ip
	$(STRIP) -s $@

$(IPROUTE2_PATH)/misc/ss: | $(IPROUTE2_PATH)/.installed $(USERSPACE_DEPS)
	$(MAKE) -C $(IPROUTE2_PATH) PREFIX=/ misc/ss
	$(STRIP) -s $@

$(IPTABLES_PATH)/.installed: $(IPTABLES_TAR)
	mkdir -p $(BUILD_PATH)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	sed -i -e "/nfnetlink=[01]/s:=[01]:=0:" -e "/nfconntrack=[01]/s:=[01]:=0:" $(IPTABLES_PATH)/configure
	touch $@

$(IPTABLES_PATH)/iptables/xtables-legacy-multi: | $(IPTABLES_PATH)/.installed $(USERSPACE_DEPS)
	cd $(IPTABLES_PATH) && ./configure --prefix=/ $(CROSS_COMPILE_FLAG) --enable-static --disable-shared --disable-nftables --disable-bpf-compiler --disable-nfsynproxy --disable-libipq --disable-connlabel --with-kernel=$(BUILD_PATH)/include
	$(MAKE) -C $(IPTABLES_PATH)
	$(STRIP) -s $@

$(NMAP_PATH)/.installed: $(NMAP_TAR)
	mkdir -p $(BUILD_PATH)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	touch $@

$(NMAP_PATH)/ncat/ncat: | $(NMAP_PATH)/.installed $(USERSPACE_DEPS)
	cd $(NMAP_PATH) && ./configure --prefix=/ $(CROSS_COMPILE_FLAG) --enable-static --disable-shared --without-ndiff --without-zenmap --without-nping --with-libpcap=included --with-libpcre=included --with-libdnet=included --without-liblua --with-liblinear=included --without-nmap-update --without-openssl --with-pcap=linux --without-libssh
	$(MAKE) -C $(NMAP_PATH)/libpcap
	$(MAKE) -C $(NMAP_PATH)/ncat
	$(STRIP) -s $@

clean:
	rm -rf $(BUILD_PATH)

distclean: clean
	rm -rf $(DISTFILES_PATH)

menuconfig: $(KERNEL_BUILD_PATH)/.config
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_BUILD_PATH) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(CROSS_COMPILE) menuconfig

.PHONY: qemu build clean distclean menuconfig
.DELETE_ON_ERROR:
