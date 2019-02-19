# SPDX-License-Identifier: GPL-2.0

ifneq ($(KBUILD_SRC),)

symlinks := $(patsubst $(srctree)/$(src)/%,%,$(wildcard $(srctree)/$(src)/*.py))

quiet_cmd_symlink = SYMLINK $@
      cmd_symlink = ln -fsn $(patsubst $(obj)/%,$(abspath $(srctree))/$(src)/%,$@) $@

extra-y += $(symlinks)
$(addprefix $(obj)/, $(symlinks)): FORCE
	$(call if_changed,symlink)

endif

quiet_cmd_gen_constants_py = GEN     $@
      cmd_gen_constants_py = \
	$(CPP) -E -x c -P $(c_flags) $< > $@ ;\
	sed -i '1,/<!-- end-c-headers -->/d;' $@

extra-y += constants.py
$(obj)/constants.py: $(src)/constants.py.in FORCE
	$(call if_changed_dep,gen_constants_py)

clean-files := *.pyc *.pyo
