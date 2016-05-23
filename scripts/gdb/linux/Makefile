always := gdb-scripts

SRCTREE := $(shell cd $(srctree) && /bin/pwd)

$(obj)/gdb-scripts:
ifneq ($(KBUILD_SRC),)
	$(Q)ln -fsn $(SRCTREE)/$(obj)/*.py $(objtree)/$(obj)
endif
	@:

quiet_cmd_gen_constants_py = GEN     $@
      cmd_gen_constants_py = \
	$(CPP) -E -x c -P $(c_flags) $< > $@ ;\
	sed -i '1,/<!-- end-c-headers -->/d;' $@

$(obj)/constants.py: $(SRCTREE)/$(obj)/constants.py.in
	$(call if_changed,gen_constants_py)

build_constants_py: $(obj)/constants.py

clean-files := *.pyc *.pyo $(if $(KBUILD_SRC),*.py) $(obj)/constants.py
