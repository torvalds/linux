# SPDX-License-Identifier: GPL-2.0

ifdef building_out_of_srctree

symlinks := $(patsubst $(src)/%,%,$(wildcard $(src)/*.py))

quiet_cmd_symlink = SYMLINK $@
      cmd_symlink = ln -fsn $(patsubst $(obj)/%,$(abspath $(src))/%,$@) $@

always-y += $(symlinks)
$(addprefix $(obj)/, $(symlinks)): FORCE
	$(call if_changed,symlink)

endif

quiet_cmd_gen_constants_py = GEN     $@
      cmd_gen_constants_py = \
	$(CPP) -E -x c -P $(c_flags) $< > $@ ;\
	sed -i '1,/<!-- end-c-headers -->/d;' $@

always-y += constants.py
$(obj)/constants.py: $(src)/constants.py.in FORCE
	$(call if_changed_dep,gen_constants_py)

clean-files := *.pyc *.pyo
