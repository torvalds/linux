# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.

PWD := $(shell pwd)

CHOST := $(shell gcc -dumpmachine)
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
	flock -x $$@.lock -c '[ -f $$@ ] && exit 0; wget -O $$@.tmp $(MIRROR)$(1) || wget -t inf --retry-on-http-error=404 -O $$@.tmp $(2)$(1) || rm -f $$@.tmp'
	if echo "$(3)  $$@.tmp" | sha256sum -c -; then mv $$@.tmp $$@; else rm -f $$@.tmp; exit 71; fi
endef

$(eval $(call tar_download,MUSL,musl,1.1.20,.tar.gz,https://www.musl-libc.org/releases/,44be8771d0e6c6b5f82dd15662eb2957c9a3173a19a8b49966ac0542bbd40d61))
$(eval $(call tar_download,LIBMNL,libmnl,1.0.4,.tar.bz2,https://www.netfilter.org/projects/libmnl/files/,171f89699f286a5854b72b91d06e8f8e3683064c5901fb09d954a9ab6f551f81))
$(eval $(call tar_download,IPERF,iperf,3.1.7,.tar.gz,http://downloads.es.net/pub/iperf/,a4ef73406fe92250602b8da2ae89ec53211f805df97a1d1d629db5a14043734f))
$(eval $(call tar_download,BASH,bash,5.0,.tar.gz,https://ftp.gnu.org/gnu/bash/,b4a80f2ac66170b2913efbfb9f2594f1f76c7b1afd11f799e22035d63077fb4d))
$(eval $(call tar_download,IPROUTE2,iproute2,5.1.0,.tar.gz,https://www.kernel.org/pub/linux/utils/net/iproute2/,9b43707d6075ecdca14803ca8ce0c8553848c49fa1586d12fd508d66577243f2))
$(eval $(call tar_download,IPTABLES,iptables,1.6.1,.tar.bz2,https://www.netfilter.org/projects/iptables/files/,0fc2d7bd5d7be11311726466789d4c65fb4c8e096c9182b56ce97440864f0cf5))
$(eval $(call tar_download,NMAP,nmap,7.60,.tar.bz2,https://nmap.org/dist/,a8796ecc4fa6c38aad6139d9515dc8113023a82e9d787e5a5fb5fa1b05516f21))
$(eval $(call tar_download,IPUTILS,iputils,s20161105,.tar.gz,https://github.com/iputils/iputils/archive/s20161105.tar.gz/#,f813092f03d17294fd23544b129b95cdb87fe19f7970a51908a6b88509acad8a))
$(eval $(call tar_download,WIREGUARD_TOOLS,WireGuard,0.0.20191212,.tar.xz,https://git.zx2c4.com/WireGuard/snapshot/,b0d718380f7a8822b2f12d75e462fa4eafa3a77871002981f367cd4fe2a1b071))

KERNEL_BUILD_PATH := $(BUILD_PATH)/kernel$(if $(findstring yes,$(DEBUG_KERNEL)),-debug)
rwildcard=$(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2) $(filter $(subst *,%,$2),$d))
WIREGUARD_SOURCES := $(call rwildcard,$(KERNEL_PATH)/drivers/net/wireguard/,*)

export CFLAGS ?= -O3 -pipe
export LDFLAGS ?=
export CPPFLAGS := -I$(BUILD_PATH)/include

ifeq ($(CHOST),$(CBUILD))
CROSS_COMPILE_FLAG := --host=$(CHOST)
NOPIE_GCC := gcc -fno-PIE
CFLAGS += -march=native
STRIP := strip
else
$(info Cross compilation: building for $(CBUILD) using $(CHOST))
CROSS_COMPILE_FLAG := --build=$(CBUILD) --host=$(CHOST)
export CROSS_COMPILE=$(CBUILD)-
NOPIE_GCC := $(CBUILD)-gcc -fno-PIE
STRIP := $(CBUILD)-strip
endif
ifeq ($(ARCH),aarch64)
QEMU_ARCH := aarch64
KERNEL_ARCH := arm64
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/arm64/boot/Image
ifeq ($(CHOST),$(CBUILD))
QEMU_MACHINE := -cpu host -machine virt,gic_version=host,accel=kvm
else
QEMU_MACHINE := -cpu cortex-a53 -machine virt
CFLAGS += -march=armv8-a -mtune=cortex-a53
endif
else ifeq ($(ARCH),aarch64_be)
QEMU_ARCH := aarch64
KERNEL_ARCH := arm64
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/arm64/boot/Image
ifeq ($(CHOST),$(CBUILD))
QEMU_MACHINE := -cpu host -machine virt,gic_version=host,accel=kvm
else
QEMU_MACHINE := -cpu cortex-a53 -machine virt
CFLAGS += -march=armv8-a -mtune=cortex-a53
endif
else ifeq ($(ARCH),arm)
QEMU_ARCH := arm
KERNEL_ARCH := arm
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/arm/boot/zImage
ifeq ($(CHOST),$(CBUILD))
QEMU_MACHINE := -cpu host -machine virt,gic_version=host,accel=kvm
else
QEMU_MACHINE := -cpu cortex-a15 -machine virt
CFLAGS += -march=armv7-a -mtune=cortex-a15 -mabi=aapcs-linux
endif
else ifeq ($(ARCH),armeb)
QEMU_ARCH := arm
KERNEL_ARCH := arm
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/arm/boot/zImage
ifeq ($(CHOST),$(CBUILD))
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
ifeq ($(CHOST),$(CBUILD))
QEMU_MACHINE := -cpu host -machine q35,accel=kvm
else
QEMU_MACHINE := -cpu Skylake-Server -machine q35
CFLAGS += -march=skylake-avx512
endif
else ifeq ($(ARCH),i686)
QEMU_ARCH := i386
KERNEL_ARCH := x86
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/x86/boot/bzImage
ifeq ($(subst i686,x86_64,$(CBUILD)),$(CHOST))
QEMU_MACHINE := -cpu host -machine q35,accel=kvm
else
QEMU_MACHINE := -cpu coreduo -machine q35
CFLAGS += -march=prescott
endif
else ifeq ($(ARCH),mips64)
QEMU_ARCH := mips64
KERNEL_ARCH := mips
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/vmlinux
ifeq ($(CHOST),$(CBUILD))
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
ifeq ($(CHOST),$(CBUILD))
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
ifeq ($(CHOST),$(CBUILD))
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
ifeq ($(CHOST),$(CBUILD))
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
ifeq ($(CHOST),$(CBUILD))
QEMU_MACHINE := -cpu host,accel=kvm -machine pseries
else
QEMU_MACHINE := -machine pseries
endif
CFLAGS += -mcpu=powerpc64le -mlong-double-64
else ifeq ($(ARCH),powerpc)
QEMU_ARCH := ppc
KERNEL_ARCH := powerpc
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/arch/powerpc/boot/uImage
ifeq ($(CHOST),$(CBUILD))
QEMU_MACHINE := -cpu host,accel=kvm -machine ppce500
else
QEMU_MACHINE := -machine ppce500
endif
CFLAGS += -mcpu=powerpc -mlong-double-64 -msecure-plt
else ifeq ($(ARCH),m68k)
QEMU_ARCH := m68k
KERNEL_ARCH := m68k
KERNEL_BZIMAGE := $(KERNEL_BUILD_PATH)/vmlinux
ifeq ($(CHOST),$(CBUILD))
QEMU_MACHINE := -cpu host,accel=kvm -machine q800
else
QEMU_MACHINE := -machine q800
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
	echo "file /bin/wg $(WIREGUARD_TOOLS_PATH)/src/tools/wg 755 0 0" >> $@
	echo "file /bin/bash $(BASH_PATH)/bash 755 0 0" >> $@
	echo "file /bin/ip $(IPROUTE2_PATH)/ip/ip 755 0 0" >> $@
	echo "file /bin/ss $(IPROUTE2_PATH)/misc/ss 755 0 0" >> $@
	echo "file /bin/ping $(IPUTILS_PATH)/ping 755 0 0" >> $@
	echo "file /bin/ncat $(NMAP_PATH)/ncat/ncat 755 0 0" >> $@
	echo "file /bin/xtables-multi $(IPTABLES_PATH)/iptables/xtables-multi 755 0 0" >> $@
	echo "slink /bin/iptables xtables-multi 777 0 0" >> $@
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

$(KERNEL_BZIMAGE): $(KERNEL_BUILD_PATH)/.config $(BUILD_PATH)/init-cpio-spec.txt $(MUSL_PATH)/lib/libc.so $(IPERF_PATH)/src/iperf3 $(IPUTILS_PATH)/ping $(BASH_PATH)/bash $(IPROUTE2_PATH)/misc/ss $(IPROUTE2_PATH)/ip/ip $(IPTABLES_PATH)/iptables/xtables-multi $(NMAP_PATH)/ncat/ncat $(WIREGUARD_TOOLS_PATH)/src/tools/wg $(BUILD_PATH)/init ../netns.sh $(WIREGUARD_SOURCES)
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_BUILD_PATH) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(CROSS_COMPILE) CC="$(NOPIE_GCC)"

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
	printf '#!/bin/sh\nexec "$(REAL_CC)" --specs="$(BUILD_PATH)/musl-gcc.specs" -fno-stack-protector -no-pie "$$@"\n' > $(BUILD_PATH)/musl-gcc
	chmod +x $(BUILD_PATH)/musl-gcc

$(IPERF_PATH)/.installed: $(IPERF_TAR)
	mkdir -p $(BUILD_PATH)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	sed -i '1s/^/#include <stdint.h>/' $(IPERF_PATH)/src/cjson.h $(IPERF_PATH)/src/timer.h
	sed -i -r 's/-p?g//g' $(IPERF_PATH)/src/Makefile*
	touch $@

$(IPERF_PATH)/src/iperf3: | $(IPERF_PATH)/.installed $(USERSPACE_DEPS)
	cd $(IPERF_PATH) && CFLAGS="$(CFLAGS) -D_GNU_SOURCE" ./configure --prefix=/ $(CROSS_COMPILE_FLAG) --enable-static --disable-shared
	$(MAKE) -C $(IPERF_PATH)
	$(STRIP) -s $@

$(LIBMNL_PATH)/.installed: $(LIBMNL_TAR)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	touch $@

$(LIBMNL_PATH)/src/.libs/libmnl.a: | $(LIBMNL_PATH)/.installed $(USERSPACE_DEPS)
	cd $(LIBMNL_PATH) && ./configure --prefix=/ $(CROSS_COMPILE_FLAG) --enable-static --disable-shared
	$(MAKE) -C $(LIBMNL_PATH)
	sed -i 's:prefix=.*:prefix=$(LIBMNL_PATH):' $(LIBMNL_PATH)/libmnl.pc

$(WIREGUARD_TOOLS_PATH)/.installed: $(WIREGUARD_TOOLS_TAR)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	touch $@

$(WIREGUARD_TOOLS_PATH)/src/tools/wg: | $(WIREGUARD_TOOLS_PATH)/.installed $(LIBMNL_PATH)/src/.libs/libmnl.a $(USERSPACE_DEPS)
	LDFLAGS="$(LDFLAGS) -L$(LIBMNL_PATH)/src/.libs" $(MAKE) -C $(WIREGUARD_TOOLS_PATH)/src/tools LIBMNL_CFLAGS="-I$(LIBMNL_PATH)/include" LIBMNL_LDLIBS="-lmnl" wg
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
	$(MAKE) -C $(IPUTILS_PATH) USE_CAP=no USE_IDN=no USE_NETTLE=no USE_CRYPTO=no ping
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
	printf 'CC:=$(CC)\nPKG_CONFIG:=pkg-config\nTC_CONFIG_XT:=n\nTC_CONFIG_ATM:=n\nTC_CONFIG_IPSET:=n\nIP_CONFIG_SETNS:=y\nHAVE_ELF:=n\nHAVE_MNL:=y\nHAVE_BERKELEY_DB:=n\nHAVE_LATEX:=n\nHAVE_PDFLATEX:=n\nCFLAGS+=-DHAVE_SETNS -DHAVE_LIBMNL -I$(LIBMNL_PATH)/include\nLDLIBS+=-lmnl' > $(IPROUTE2_PATH)/config.mk
	printf 'lib: snapshot\n\t$$(MAKE) -C lib\nip/ip: lib\n\t$$(MAKE) -C ip ip\nmisc/ss: lib\n\t$$(MAKE) -C misc ss\n' >> $(IPROUTE2_PATH)/Makefile
	touch $@

$(IPROUTE2_PATH)/ip/ip: | $(IPROUTE2_PATH)/.installed $(LIBMNL_PATH)/src/.libs/libmnl.a $(USERSPACE_DEPS)
	LDFLAGS="$(LDFLAGS) -L$(LIBMNL_PATH)/src/.libs" PKG_CONFIG_LIBDIR="$(LIBMNL_PATH)" $(MAKE) -C $(IPROUTE2_PATH) PREFIX=/ ip/ip
	$(STRIP) -s $(IPROUTE2_PATH)/ip/ip

$(IPROUTE2_PATH)/misc/ss: | $(IPROUTE2_PATH)/.installed $(LIBMNL_PATH)/src/.libs/libmnl.a $(USERSPACE_DEPS)
	LDFLAGS="$(LDFLAGS) -L$(LIBMNL_PATH)/src/.libs" PKG_CONFIG_LIBDIR="$(LIBMNL_PATH)" $(MAKE) -C $(IPROUTE2_PATH) PREFIX=/ misc/ss
	$(STRIP) -s $(IPROUTE2_PATH)/misc/ss

$(IPTABLES_PATH)/.installed: $(IPTABLES_TAR)
	mkdir -p $(BUILD_PATH)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	sed -i -e "/nfnetlink=[01]/s:=[01]:=0:" -e "/nfconntrack=[01]/s:=[01]:=0:" $(IPTABLES_PATH)/configure
	touch $@

$(IPTABLES_PATH)/iptables/xtables-multi: | $(IPTABLES_PATH)/.installed $(LIBMNL_PATH)/src/.libs/libmnl.a $(USERSPACE_DEPS)
	cd $(IPTABLES_PATH) && PKG_CONFIG_LIBDIR="$(LIBMNL_PATH)" ./configure --prefix=/ $(CROSS_COMPILE_FLAG) --enable-static --disable-shared --disable-nftables --disable-bpf-compiler --disable-nfsynproxy --disable-libipq --with-kernel=$(BUILD_PATH)/include
	$(MAKE) -C $(IPTABLES_PATH)
	$(STRIP) -s $@

$(NMAP_PATH)/.installed: $(NMAP_TAR)
	mkdir -p $(BUILD_PATH)
	flock -s $<.lock tar -C $(BUILD_PATH) -xf $<
	touch $@

$(NMAP_PATH)/ncat/ncat: | $(NMAP_PATH)/.installed $(USERSPACE_DEPS)
	cd $(NMAP_PATH) && ./configure --prefix=/ $(CROSS_COMPILE_FLAG) --enable-static --disable-shared --without-ndiff --without-zenmap --without-nping --with-libpcap=included --with-libpcre=included --with-libdnet=included --without-liblua --with-liblinear=included --without-nmap-update --without-openssl --with-pcap=linux
	$(MAKE) -C $(NMAP_PATH) build-ncat
	$(STRIP) -s $@

clean:
	rm -rf $(BUILD_PATH)

distclean: clean
	rm -rf $(DISTFILES_PATH)

menuconfig: $(KERNEL_BUILD_PATH)/.config
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_BUILD_PATH) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(CROSS_COMPILE) CC="$(NOPIE_GCC)" menuconfig

.PHONY: qemu build clean distclean menuconfig
.DELETE_ON_ERROR:
