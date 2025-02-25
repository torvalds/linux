# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.

PWD := $(shell pwd)

# Set these from the environment to override
KERNEL_PATH ?= $(PWD)/../../../../..
BUILD_PATH ?= $(PWD)/build/$(ARCH)
DISTFILES_PATH ?= $(PWD)/distfiles
NR_CPUS ?= 4
ARCH ?=
CBUILD := $(shell gcc -dumpmachine)
HOST_ARCH := $(firstword $(subst -, ,$(CBUILD)))
ifeq ($(ARCH),)
ARCH := $(HOST_ARCH)
endif

MIRROR := https://download.wireguard.com/qemu-test/distfiles/

KERNEL_BUILD_PATH := $(BUILD_PATH)/kernel$(if $(findstring yes,$(DEBUG_KERNEL)),-debug)

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
$(DISTFILES_PATH)/$(1): | $(4)
	mkdir -p $(DISTFILES_PATH)
	flock -x $$@.lock -c '[ -f $$@ ] && exit 0; wget -O $$@.tmp $(MIRROR)$(1) || wget -O $$@.tmp $(2)$(1) || rm -f $$@.tmp; [ -f $$@.tmp ] || exit 1; if ([ -n "$(4)" ] && sed -n "s#^\([a-f0-9]\{64\}\)  \($(1)\)\$$$$#\1  $(DISTFILES_PATH)/\2.tmp#p" "$(4)" || echo "$(3)  $$@.tmp") | sha256sum -c -; then mv $$@.tmp $$@; else rm -f $$@.tmp; exit 71; fi'
endef

$(eval $(call tar_download,IPERF,iperf,3.11,.tar.gz,https://downloads.es.net/pub/iperf/,de8cb409fad61a0574f4cb07eb19ce1159707403ac2dc01b5d175e91240b7e5f))
$(eval $(call tar_download,BASH,bash,5.1.16,.tar.gz,https://ftp.gnu.org/gnu/bash/,5bac17218d3911834520dad13cd1f85ab944e1c09ae1aba55906be1f8192f558))
$(eval $(call tar_download,IPROUTE2,iproute2,5.17.0,.tar.gz,https://www.kernel.org/pub/linux/utils/net/iproute2/,bda331d5c4606138892f23a565d78fca18919b4d508a0b7ca8391c2da2db68b9))
$(eval $(call tar_download,IPTABLES,iptables,1.8.7,.tar.bz2,https://www.netfilter.org/projects/iptables/files/,c109c96bb04998cd44156622d36f8e04b140701ec60531a10668cfdff5e8d8f0))
$(eval $(call tar_download,NMAP,nmap,7.92,.tgz,https://nmap.org/dist/,064183ea642dc4c12b1ab3b5358ce1cef7d2e7e11ffa2849f16d339f5b717117))
$(eval $(call tar_download,IPUTILS,iputils,s20190709,.tar.gz,https://github.com/iputils/iputils/archive/s20190709.tar.gz/#,a15720dd741d7538dd2645f9f516d193636ae4300ff7dbc8bfca757bf166490a))
$(eval $(call tar_download,WIREGUARD_TOOLS,wireguard-tools,1.0.20210914,.tar.xz,https://git.zx2c4.com/wireguard-tools/snapshot/,97ff31489217bb265b7ae850d3d0f335ab07d2652ba1feec88b734bc96bd05ac))

export CFLAGS := -O3 -pipe
ifeq ($(HOST_ARCH),$(ARCH))
CFLAGS += -march=native
endif
export LDFLAGS :=
export CPPFLAGS :=

QEMU_VPORT_RESULT :=
ifeq ($(ARCH),aarch64)
CHOST := aarch64-linux-musl
QEMU_ARCH := aarch64
KERNEL_ARCH := arm64
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/arm64/boot/Image
QEMU_VPORT_RESULT := virtio-serial-device
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host -machine virt,gic_version=host,accel=kvm
else
QEMU_MACHINE := -cpu max -machine virt
CFLAGS += -march=armv8-a
endif
else ifeq ($(ARCH),aarch64_be)
CHOST := aarch64_be-linux-musl
QEMU_ARCH := aarch64
KERNEL_ARCH := arm64
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/arm64/boot/Image
QEMU_VPORT_RESULT := virtio-serial-device
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host -machine virt,gic_version=host,accel=kvm
else
QEMU_MACHINE := -cpu max -machine virt
CFLAGS += -march=armv8-a
endif
else ifeq ($(ARCH),arm)
CHOST := arm-linux-musleabi
QEMU_ARCH := arm
KERNEL_ARCH := arm
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/arm/boot/zImage
QEMU_VPORT_RESULT := virtio-serial-device
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host -machine virt,gic_version=host,accel=kvm
else
QEMU_MACHINE := -cpu max -machine virt
CFLAGS += -march=armv7-a -mabi=aapcs-linux
endif
else ifeq ($(ARCH),armeb)
CHOST := armeb-linux-musleabi
QEMU_ARCH := arm
KERNEL_ARCH := arm
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/arm/boot/zImage
QEMU_VPORT_RESULT := virtio-serial-device
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host -machine virt,gic_version=host,accel=kvm
else
QEMU_MACHINE := -cpu max -machine virt
CFLAGS += -march=armv7-a -mabi=aapcs-linux
LDFLAGS += -Wl,--be8
endif
else ifeq ($(ARCH),x86_64)
CHOST := x86_64-linux-musl
QEMU_ARCH := x86_64
KERNEL_ARCH := x86_64
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/x86/boot/bzImage
QEMU_VPORT_RESULT := virtio-serial-device
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host -machine microvm,accel=kvm,pit=off,pic=off,rtc=off,acpi=off
else
QEMU_MACHINE := -cpu max -machine microvm,acpi=off
endif
else ifeq ($(ARCH),i686)
CHOST := i686-linux-musl
QEMU_ARCH := i386
KERNEL_ARCH := x86
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/x86/boot/bzImage
QEMU_VPORT_RESULT := virtio-serial-device
ifeq ($(subst x86_64,i686,$(HOST_ARCH)),$(ARCH))
QEMU_MACHINE := -cpu host -machine microvm,accel=kvm,pit=off,pic=off,rtc=off,acpi=off
else
QEMU_MACHINE := -cpu coreduo -machine microvm,acpi=off
endif
else ifeq ($(ARCH),mips64)
CHOST := mips64-linux-musl
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
CHOST := mips64el-linux-musl
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
CHOST := mips-linux-musl
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
CHOST := mipsel-linux-musl
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
else ifeq ($(ARCH),powerpc64)
CHOST := powerpc64-linux-musl
QEMU_ARCH := ppc64
KERNEL_ARCH := powerpc
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/vmlinux
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host,accel=kvm -machine pseries
else
QEMU_MACHINE := -machine pseries -device spapr-rng,rng=rng -object rng-random,id=rng
endif
else ifeq ($(ARCH),powerpc64le)
CHOST := powerpc64le-linux-musl
QEMU_ARCH := ppc64
KERNEL_ARCH := powerpc
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/vmlinux
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host,accel=kvm -machine pseries
else
QEMU_MACHINE := -machine pseries -device spapr-rng,rng=rng -object rng-random,id=rng
endif
else ifeq ($(ARCH),powerpc)
CHOST := powerpc-linux-musl
QEMU_ARCH := ppc
KERNEL_ARCH := powerpc
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/powerpc/boot/uImage
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host,accel=kvm -machine ppce500
else
QEMU_MACHINE := -machine ppce500
endif
else ifeq ($(ARCH),m68k)
CHOST := m68k-linux-musl
QEMU_ARCH := m68k
KERNEL_ARCH := m68k
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/vmlinux
KERNEL_CMDLINE := $(shell sed -n 's/CONFIG_CMDLINE=\(.*\)/\1/p' arch/m68k.config)
QEMU_VPORT_RESULT := virtio-serial-device
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host,accel=kvm -machine virt -append $(KERNEL_CMDLINE)
else
QEMU_MACHINE := -machine virt -smp 1 -append $(KERNEL_CMDLINE)
endif
else ifeq ($(ARCH),riscv64)
CHOST := riscv64-linux-musl
QEMU_ARCH := riscv64
KERNEL_ARCH := riscv
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/riscv/boot/Image
QEMU_VPORT_RESULT := virtio-serial-device
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host,accel=kvm -machine virt
else
QEMU_MACHINE := -cpu rv64 -machine virt
endif
else ifeq ($(ARCH),riscv32)
CHOST := riscv32-linux-musl
QEMU_ARCH := riscv32
KERNEL_ARCH := riscv
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/riscv/boot/Image
QEMU_VPORT_RESULT := virtio-serial-device
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host,accel=kvm -machine virt
else
QEMU_MACHINE := -cpu rv32 -machine virt
endif
else ifeq ($(ARCH),s390x)
CHOST := s390x-linux-musl
QEMU_ARCH := s390x
KERNEL_ARCH := s390
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/s390/boot/bzImage
KERNEL_CMDLINE := $(shell sed -n 's/CONFIG_CMDLINE=\(.*\)/\1/p' arch/s390x.config)
QEMU_VPORT_RESULT := virtio-serial-ccw
ifeq ($(HOST_ARCH),$(ARCH))
QEMU_MACHINE := -cpu host,accel=kvm -machine s390-ccw-virtio -append $(KERNEL_CMDLINE)
else
QEMU_MACHINE := -cpu max -machine s390-ccw-virtio -append $(KERNEL_CMDLINE)
endif
else ifeq ($(ARCH),um)
CHOST := $(HOST_ARCH)-linux-musl
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/vmlinux
KERNEL_ARCH := um
KERNEL_CMDLINE := $(shell sed -n 's/CONFIG_CMDLINE=\(.*\)/\1/p' arch/um.config)
else
$(error I only build: x86_64, i686, arm, armeb, aarch64, aarch64_be, mips, mipsel, mips64, mips64el, powerpc64, powerpc64le, powerpc, m68k, riscv64, riscv32, s390x, um)
endif

TOOLCHAIN_FILENAME := $(CHOST)-cross.tgz
TOOLCHAIN_TAR := $(DISTFILES_PATH)/$(TOOLCHAIN_FILENAME)
TOOLCHAIN_PATH := $(BUILD_PATH)/$(CHOST)-cross
TOOLCHAIN_DIR := https://download.wireguard.com/qemu-test/toolchains/20211123/
$(eval $(call file_download,toolchain-sha256sums-20211123,$(TOOLCHAIN_DIR)SHA256SUMS#,83da033fd8c798df476c21d9612da2dfb896ec62fbed4ceec5eefc0e56b3f0c8))
$(eval $(call file_download,$(TOOLCHAIN_FILENAME),$(TOOLCHAIN_DIR),,$(DISTFILES_PATH)/toolchain-sha256sums-20211123))

STRIP := $(CHOST)-strip
CROSS_COMPILE_FLAG := --build=$(CBUILD) --host=$(CHOST)
$(info Building for $(CHOST) using $(CBUILD))
ifneq ($(ARCH),um)
export CROSS_COMPILE := $(CHOST)-
endif
export PATH := $(TOOLCHAIN_PATH)/bin:$(PATH)
export CC := $(CHOST)-gcc
CCACHE_PATH := $(shell which ccache 2>/dev/null)
ifneq ($(CCACHE_PATH),)
export KBUILD_BUILD_TIMESTAMP := Fri Jun  5 15:58:00 CEST 2015
export PATH := $(TOOLCHAIN_PATH)/bin/ccache:$(PATH)
export CCACHE_SLOPPINESS := file_macro,time_macros
export CCACHE_DIR ?= $(PWD)/ccache
endif

USERSPACE_DEPS := $(TOOLCHAIN_PATH)/.installed $(TOOLCHAIN_PATH)/$(CHOST)/include/linux/.installed

comma := ,
build: $(KERNEL_BZIMAGE)
qemu: $(KERNEL_BZIMAGE)
	rm -f $(BUILD_PATH)/result
ifneq ($(ARCH),um)
	timeout --foreground 20m qemu-system-$(QEMU_ARCH) \
		-nodefaults \
		-nographic \
		-smp $(NR_CPUS) \
		$(QEMU_MACHINE) \
		-m $$(grep -q CONFIG_DEBUG_KMEMLEAK=y $(KERNEL_BUILD_PATH)/.config && echo 1G || echo 256M) \
		-serial stdio \
		-chardev file,path=$(BUILD_PATH)/result,id=result \
		$(if $(QEMU_VPORT_RESULT),-device $(QEMU_VPORT_RESULT) -device virtserialport$(comma)chardev=result,-serial chardev:result) \
		-no-reboot \
		-monitor none \
		-kernel $<
else
	timeout --foreground 20m $< \
		$(KERNEL_CMDLINE) \
		mem=$$(grep -q CONFIG_DEBUG_KMEMLEAK=y $(KERNEL_BUILD_PATH)/.config && echo 1G || echo 256M) \
		noreboot \
		con1=fd:51 51>$(BUILD_PATH)/result </dev/null 2>&1 | cat
endif
	grep -Fq success $(BUILD_PATH)/result

$(BUILD_PATH)/init-cpio-spec.txt: $(TOOLCHAIN_PATH)/.installed $(BUILD_PATH)/init
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
	echo "file /lib/libc.so $(TOOLCHAIN_PATH)/$(CHOST)/lib/libc.so 755 0 0" >> $@
	echo "slink $$($(CHOST)-readelf -p .interp '$(BUILD_PATH)/init'| grep -o '/lib/.*') libc.so 777 0 0" >> $@

$(KERNEL_BUILD_PATH)/.config: $(TOOLCHAIN_PATH)/.installed kernel.config arch/$(ARCH).config
	mkdir -p $(KERNEL_BUILD_PATH)
	cp kernel.config $(KERNEL_BUILD_PATH)/minimal.config
	printf 'CONFIG_NR_CPUS=$(NR_CPUS)\nCONFIG_INITRAMFS_SOURCE="$(BUILD_PATH)/init-cpio-spec.txt"\n' >> $(KERNEL_BUILD_PATH)/minimal.config
	cat arch/$(ARCH).config >> $(KERNEL_BUILD_PATH)/minimal.config
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_BUILD_PATH) ARCH=$(KERNEL_ARCH) allnoconfig
	cd $(KERNEL_BUILD_PATH) && ARCH=$(KERNEL_ARCH) $(KERNEL_PATH)/scripts/kconfig/merge_config.sh -n $(KERNEL_BUILD_PATH)/.config $(KERNEL_BUILD_PATH)/minimal.config
	$(if $(findstring yes,$(DEBUG_KERNEL)),cp debug.config $(KERNEL_BUILD_PATH) && cd $(KERNEL_BUILD_PATH) && ARCH=$(KERNEL_ARCH) $(KERNEL_PATH)/scripts/kconfig/merge_config.sh -n $(KERNEL_BUILD_PATH)/.config debug.config,)

$(KERNEL_BZIMAGE): $(TOOLCHAIN_PATH)/.installed $(KERNEL_BUILD_PATH)/.config $(BUILD_PATH)/init-cpio-spec.txt $(IPERF_PATH)/src/iperf3 $(IPUTILS_PATH)/ping $(BASH_PATH)/bash $(IPROUTE2_PATH)/misc/ss $(IPROUTE2_PATH)/ip/ip $(IPTABLES_PATH)/iptables/xtables-legacy-multi $(NMAP_PATH)/ncat/ncat $(WIREGUARD_TOOLS_PATH)/src/wg $(BUILD_PATH)/init
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_BUILD_PATH) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(CROSS_COMPILE)
.PHONY: $(KERNEL_BZIMAGE)

$(TOOLCHAIN_PATH)/$(CHOST)/include/linux/.installed: | $(KERNEL_BUILD_PATH)/.config $(TOOLCHAIN_PATH)/.installed
ifneq ($(ARCH),um)
	rm -rf $(TOOLCHAIN_PATH)/$(CHOST)/include/linux
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_BUILD_PATH) INSTALL_HDR_PATH=$(TOOLCHAIN_PATH)/$(CHOST) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(CROSS_COMPILE) headers_install
endif
	touch $@

$(TOOLCHAIN_PATH)/.installed: $(TOOLCHAIN_TAR)
	mkdir -p $(BUILD_PATH)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	$(STRIP) -s $(TOOLCHAIN_PATH)/$(CHOST)/lib/libc.so
ifneq ($(CCACHE_PATH),)
	mkdir -p $(TOOLCHAIN_PATH)/bin/ccache
	ln -s $(CCACHE_PATH) $(TOOLCHAIN_PATH)/bin/ccache/$(CC)
endif
	touch $@

$(IPERF_PATH)/.installed: $(IPERF_TAR)
	mkdir -p $(BUILD_PATH)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	sed -i '1s/^/#include <stdint.h>/' $(IPERF_PATH)/src/cjson.h $(IPERF_PATH)/src/timer.h
	sed -i -r 's/-p?g//g' $(IPERF_PATH)/src/Makefile*
	touch $@

$(IPERF_PATH)/src/iperf3: | $(IPERF_PATH)/.installed $(USERSPACE_DEPS)
	cd $(IPERF_PATH) && autoreconf -fi
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
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) -std=gnu11 $<
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
	cd $(BASH_PATH) && ./configure --prefix=/ $(CROSS_COMPILE_FLAG) --without-bash-malloc --disable-debugger --disable-help-builtin --disable-history --disable-progcomp --disable-readline --disable-mem-scramble
	$(MAKE) -C $(BASH_PATH)
	$(STRIP) -s $@

$(IPROUTE2_PATH)/.installed: $(IPROUTE2_TAR)
	mkdir -p $(BUILD_PATH)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	printf 'CC:=$(CC)\nPKG_CONFIG:=pkg-config\nTC_CONFIG_XT:=n\nTC_CONFIG_ATM:=n\nTC_CONFIG_IPSET:=n\nIP_CONFIG_SETNS:=y\nHAVE_ELF:=n\nHAVE_MNL:=n\nHAVE_BERKELEY_DB:=n\nHAVE_LATEX:=n\nHAVE_PDFLATEX:=n\nCFLAGS+=-DHAVE_SETNS -DHAVE_HANDLE_AT\n' > $(IPROUTE2_PATH)/config.mk
	printf 'libutil.a.done:\n\tflock -x $$@.lock $$(MAKE) -C lib\n\ttouch $$@\nip/ip: libutil.a.done\n\t$$(MAKE) -C ip ip\nmisc/ss: libutil.a.done\n\t$$(MAKE) -C misc ss\n' >> $(IPROUTE2_PATH)/Makefile
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

cacheclean: clean
ifneq ($(CCACHE_DIR),)
	rm -rf $(CCACHE_DIR)
endif

menuconfig: $(KERNEL_BUILD_PATH)/.config
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_BUILD_PATH) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(CROSS_COMPILE) menuconfig

.PHONY: qemu build clean distclean cacheclean menuconfig
.DELETE_ON_ERROR:
