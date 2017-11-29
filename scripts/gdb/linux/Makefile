# SPDX-License-Identifier: GPL-2.0
always := gdb-scripts

SRCTREE := $(abspath $(srctree))

$(obj)/gdb-scripts:
ifneq ($(KBUILD_SRC),)
	$(Q)ln -fsn $(SRCTREE)/$(obj)/*.py $(objtree)/$(obj)
endif
	@:

quiet_cmd_gen_constants_py = GEN     $@
      cmd_gen_constants_py = \
	$(CPP) -E -x c -P $(c_flags) $< > $@ ;\
	sed -i '1,/<!-- end-c-headers -->/d;' $@

targets += constants.py
$(obj)/constants.py: $(SRCTREE)/$(obj)/constants.py.in FORCE
	$(call if_changed_dep,gen_constants_py)

build_constants_py: $(obj)/constants.py
	@:

clean-files := *.pyc *.pyo $(if $(KBUILD_SRC),*.py) $(obj)/constants.py
