# SPDX-License-Identifier: GPL-2.0-only

# Unlike the kernel space, exported headers are written in standard C.
#  - Forbid C++ style comments
#  - Use '__inline__', '__asm__' instead of 'inline', 'asm'
#
# -std=c90 (equivalent to -ansi) catches the violation of those.
# We cannot go as far as adding -Wpedantic since it emits too many warnings.
UAPI_CFLAGS := -std=c90 -Werror=implicit-function-declaration

override c_flags = $(KBUILD_USERCFLAGS) $(UAPI_CFLAGS) -Wp,-MMD,$(depfile) -I $(obj) -I $(srctree)/usr/dummy-include

# The following are excluded for now because they fail to build.
#
# Do not add a new header to the blacklist without legitimate reason.
# Please consider to fix the header first.
#
# Sorted alphabetically.
no-header-test += asm/ucontext.h
no-header-test += drm/vmwgfx_drm.h
no-header-test += linux/am437x-vpfe.h
no-header-test += linux/coda.h
no-header-test += linux/cyclades.h
no-header-test += linux/errqueue.h
no-header-test += linux/hdlc/ioctl.h
no-header-test += linux/ivtv.h
no-header-test += linux/matroxfb.h
no-header-test += linux/omap3isp.h
no-header-test += linux/omapfb.h
no-header-test += linux/patchkey.h
no-header-test += linux/phonet.h
no-header-test += linux/sctp.h
no-header-test += linux/sysctl.h
no-header-test += linux/usb/audio.h
no-header-test += linux/v4l2-mediabus.h
no-header-test += linux/v4l2-subdev.h
no-header-test += linux/videodev2.h
no-header-test += linux/vm_sockets.h
no-header-test += sound/asequencer.h
no-header-test += sound/asoc.h
no-header-test += sound/asound.h
no-header-test += sound/compress_offload.h
no-header-test += sound/emu10k1.h
no-header-test += sound/sfnt_info.h
no-header-test += xen/evtchn.h
no-header-test += xen/gntdev.h
no-header-test += xen/privcmd.h

# More headers are broken in some architectures

ifeq ($(SRCARCH),arc)
no-header-test += linux/bpf_perf_event.h
endif

ifeq ($(SRCARCH),openrisc)
no-header-test += linux/bpf_perf_event.h
endif

ifeq ($(SRCARCH),powerpc)
no-header-test += linux/bpf_perf_event.h
endif

ifeq ($(SRCARCH),sparc)
no-header-test += asm/uctx.h
no-header-test += asm/fbio.h
endif

ifeq ($(SRCARCH),xtensa)
no-header-test += linux/bpf_perf_event.h
endif

# asm-generic/*.h is used by asm/*.h, and should not be included directly
no-header-test += asm-generic/%

# The following are using libc header and types.
#
# Do not add a new header to the list without legitimate reason.
# Please consider to fix the header first.
#
# Sorted alphabetically.
uses-libc += linux/a.out.h
uses-libc += linux/atmbr2684.h
uses-libc += linux/auto_dev-ioctl.h
uses-libc += linux/auto_fs.h
uses-libc += linux/auto_fs4.h
uses-libc += linux/btrfs_tree.h
uses-libc += linux/cec-funcs.h
uses-libc += linux/cec.h
uses-libc += linux/dvb/dmx.h
uses-libc += linux/dvb/video.h
uses-libc += linux/ethtool.h
uses-libc += linux/ethtool_netlink.h
uses-libc += linux/fuse.h
uses-libc += linux/gsmmux.h
uses-libc += linux/icmp.h
uses-libc += linux/idxd.h
uses-libc += linux/if.h
uses-libc += linux/if_arp.h
uses-libc += linux/if_bonding.h
uses-libc += linux/if_pppox.h
uses-libc += linux/if_tunnel.h
uses-libc += linux/input.h
uses-libc += linux/ip6_tunnel.h
uses-libc += linux/joystick.h
uses-libc += linux/llc.h
uses-libc += linux/mctp.h
uses-libc += linux/mdio.h
uses-libc += linux/mii.h
uses-libc += linux/mptcp.h
uses-libc += linux/netdevice.h
uses-libc += linux/netfilter/xt_RATEEST.h
uses-libc += linux/netfilter/xt_hashlimit.h
uses-libc += linux/netfilter/xt_physdev.h
uses-libc += linux/netfilter/xt_rateest.h
uses-libc += linux/netfilter_arp/arp_tables.h
uses-libc += linux/netfilter_arp/arpt_mangle.h
uses-libc += linux/netfilter_bridge.h
uses-libc += linux/netfilter_bridge/ebtables.h
uses-libc += linux/netfilter_ipv4.h
uses-libc += linux/netfilter_ipv4/ip_tables.h
uses-libc += linux/netfilter_ipv6.h
uses-libc += linux/netfilter_ipv6/ip6_tables.h
uses-libc += linux/route.h
uses-libc += linux/shm.h
uses-libc += linux/soundcard.h
uses-libc += linux/string.h
uses-libc += linux/tipc_config.h
uses-libc += linux/uhid.h
uses-libc += linux/uinput.h
uses-libc += linux/vhost.h
uses-libc += linux/vhost_types.h
uses-libc += linux/virtio_ring.h
uses-libc += linux/wireless.h
uses-libc += regulator/regulator.h
uses-libc += scsi/fc/fc_els.h

ifeq ($(SRCARCH),hexagon)
uses-libc += asm/sigcontext.h
endif

ifeq ($(SRCARCH),nios2)
uses-libc += asm/ptrace.h
uses-libc += linux/bpf_perf_event.h
endif

ifeq ($(SRCARCH),s390)
uses-libc += asm/chpid.h
uses-libc += asm/chsc.h
endif

always-y := $(patsubst $(obj)/%.h,%.hdrtest, $(shell find $(obj) -name '*.h' 2>/dev/null))

target-no-libc = $(filter-out $(uses-libc), $*.h)
target-can-compile = $(and $(filter-out $(no-header-test), $*.h), \
                           $(or $(CONFIG_CC_CAN_LINK), $(target-no-libc)))

# Include the header twice to detect missing include guard.
quiet_cmd_hdrtest = HDRTEST $<
      cmd_hdrtest = \
		$(CC) $(c_flags) -fsyntax-only -Werror -x c /dev/null \
			$(if $(target-no-libc), -nostdinc) \
			$(if $(target-can-compile), -include $< -include $<); \
		$(PERL) $(src)/headers_check.pl $(obj) $<; \
		touch $@

$(obj)/%.hdrtest: $(obj)/%.h $(src)/headers_check.pl FORCE
	$(call if_changed_dep,hdrtest)

# Since GNU Make 4.3, $(patsubst $(obj)/%/,%,$(wildcard $(obj)/*/)) works.
# To support older Make versions, use a somewhat tedious way.
clean-files += $(filter-out Makefile headers_check.pl, $(notdir $(wildcard $(obj)/*)))
