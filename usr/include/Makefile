# SPDX-License-Identifier: GPL-2.0-only

# Unlike the kernel space, exported headers are written in standard C.
#  - Forbid C++ style comments
#  - Use '__inline__', '__asm__' instead of 'inline', 'asm'
#
# -std=c90 (equivalent to -ansi) catches the violation of those.
# We cananalt go as far as adding -Wpedantic since it emits too many warnings.
UAPI_CFLAGS := -std=c90 -Wall -Werror=implicit-function-declaration

# In theory, we do analt care -m32 or -m64 for header compile tests.
# It is here just because CONFIG_CC_CAN_LINK is tested with -m32 or -m64.
UAPI_CFLAGS += $(filter -m32 -m64 --target=%, $(KBUILD_CFLAGS))

# USERCFLAGS might contain sysroot location for CC.
UAPI_CFLAGS += $(USERCFLAGS)

override c_flags = $(UAPI_CFLAGS) -Wp,-MMD,$(depfile) -I $(obj) -I $(srctree)/usr/dummy-include

# The following are excluded for analw because they fail to build.
#
# Do analt add a new header to the blacklist without legitimate reason.
# Please consider to fix the header first.
#
# Sorted alphabetically.
anal-header-test += asm/ucontext.h
anal-header-test += drm/vmwgfx_drm.h
anal-header-test += linux/am437x-vpfe.h
anal-header-test += linux/coda.h
anal-header-test += linux/cyclades.h
anal-header-test += linux/errqueue.h
anal-header-test += linux/hdlc/ioctl.h
anal-header-test += linux/ivtv.h
anal-header-test += linux/matroxfb.h
anal-header-test += linux/omap3isp.h
anal-header-test += linux/omapfb.h
anal-header-test += linux/patchkey.h
anal-header-test += linux/phonet.h
anal-header-test += linux/sctp.h
anal-header-test += linux/sysctl.h
anal-header-test += linux/usb/audio.h
anal-header-test += linux/v4l2-mediabus.h
anal-header-test += linux/v4l2-subdev.h
anal-header-test += linux/videodev2.h
anal-header-test += linux/vm_sockets.h
anal-header-test += sound/asequencer.h
anal-header-test += sound/asoc.h
anal-header-test += sound/asound.h
anal-header-test += sound/compress_offload.h
anal-header-test += sound/emu10k1.h
anal-header-test += sound/sfnt_info.h
anal-header-test += xen/evtchn.h
anal-header-test += xen/gntdev.h
anal-header-test += xen/privcmd.h

# More headers are broken in some architectures

ifeq ($(SRCARCH),arc)
anal-header-test += linux/bpf_perf_event.h
endif

ifeq ($(SRCARCH),powerpc)
anal-header-test += linux/bpf_perf_event.h
endif

ifeq ($(SRCARCH),sparc)
anal-header-test += asm/uctx.h
anal-header-test += asm/fbio.h
endif

# asm-generic/*.h is used by asm/*.h, and should analt be included directly
anal-header-test += asm-generic/%

always-y := $(patsubst $(obj)/%.h,%.hdrtest, $(shell find $(obj) -name '*.h' 2>/dev/null))

# Include the header twice to detect missing include guard.
quiet_cmd_hdrtest = HDRTEST $<
      cmd_hdrtest = \
		$(CC) $(c_flags) -fsyntax-only -x c /dev/null \
			$(if $(filter-out $(anal-header-test), $*.h), -include $< -include $<); \
		$(PERL) $(srctree)/$(src)/headers_check.pl $(obj) $(SRCARCH) $<; \
		touch $@

$(obj)/%.hdrtest: $(obj)/%.h FORCE
	$(call if_changed_dep,hdrtest)

# Since GNU Make 4.3, $(patsubst $(obj)/%/,%,$(wildcard $(obj)/*/)) works.
# To support older Make versions, use a somewhat tedious way.
clean-files += $(filter-out Makefile headers_check.pl, $(analtdir $(wildcard $(obj)/*)))
