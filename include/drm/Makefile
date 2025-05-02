# SPDX-License-Identifier: GPL-2.0

# Ensure drm headers are self-contained and pass kernel-doc
hdrtest-files := \
	$(shell cd $(src) && find * -name '*.h' 2>/dev/null)

always-$(CONFIG_DRM_HEADER_TEST) += \
	$(patsubst %.h,%.hdrtest, $(hdrtest-files))

# Include the header twice to detect missing include guard.
quiet_cmd_hdrtest = HDRTEST $(patsubst %.hdrtest,%.h,$@)
      cmd_hdrtest = \
		$(CC) $(c_flags) -fsyntax-only -x c /dev/null -include $< -include $<; \
		$(srctree)/scripts/kernel-doc -none $(if $(CONFIG_WERROR)$(CONFIG_DRM_WERROR),-Werror) $<; \
		touch $@

$(obj)/%.hdrtest: $(src)/%.h FORCE
	$(call if_changed_dep,hdrtest)
