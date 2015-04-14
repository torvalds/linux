always := gdb-scripts

SRCTREE := $(shell cd $(srctree) && /bin/pwd)

$(obj)/gdb-scripts:
ifneq ($(KBUILD_SRC),)
	$(Q)ln -fsn $(SRCTREE)/$(obj)/*.py $(objtree)/$(obj)
endif
	@:

clean-files := *.pyc *.pyo $(if $(KBUILD_SRC),*.py)
