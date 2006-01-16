# Makefile to build lxdialog package
#

check-lxdialog  := $(srctree)/$(src)/check-lxdialog.sh
HOST_EXTRACFLAGS:= $(shell $(CONFIG_SHELL) $(check-lxdialog) -ccflags)
HOST_LOADLIBES  := $(shell $(CONFIG_SHELL) $(check-lxdialog) -ldflags $(HOSTCC))
 
HOST_EXTRACFLAGS += -DLOCALE 

.PHONY: dochecklxdialog
$(obj)/dochecklxdialog:
	$(Q)$(CONFIG_SHELL) $(check-lxdialog) -check $(HOSTCC) $(HOST_LOADLIBES)

hostprogs-y	:= lxdialog
always		:= $(hostprogs-y) dochecklxdialog

lxdialog-objs := checklist.o menubox.o textbox.o yesno.o inputbox.o \
		 util.o lxdialog.o msgbox.o
