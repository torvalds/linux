HOSTDRIVER_DIR ?= $(shell pwd)
OBJ_DIR=$(HOSTDRIVER_DIR)/obj
-include $(HOSTDRIVER_DIR)/platform/defs.inc
include $(HOSTDRIVER_DIR)/platform/$(PLATFORM)/platform.inc
include common.inc


V?=0
ifeq ($V,0)
	Q=@
endif	

ifeq ($(KERNEL_MAJOR_VERSION),2.4)
	MAKEFILE=Makefile-2.4
else
	MAKEFILE=Makefile
endif

INSTALL_DIR ?= /lib/modules/$(shell uname -r)/kernel/drivers/net/wireless

all:: ic symver transport

obj:
	mkdir -p $(OBJ_DIR)

symver: ic
	$(Q)cp kernel/ic/Module.symvers kernel/$(TRANSPORT)

ic: obj
#	sh $(HOSTDRIVER_DIR)/update-release-tag.sh $(HOSTDRIVER_DIR)
	make -C kernel/ic -f $(MAKEFILE) PLATFORM=$(PLATFORM) V=$(V) all strip
	$(Q)for f in kernel/ic/nano_if.mod kernel/ic/nano_if.ko; do \
		if test -f $$f; then \
			cp $$f $(OBJ_DIR); \
		fi;\
	done

bt: obj
	make -C kernel/bt -f $(MAKEFILE) PLATFORM=$(PLATFORM) V=$(V) all strip
	$(Q)for f in kernel/bt/nano_bt.mod kernel/bt/nano_bt.ko; do \
		if test -f $$f; then \
			cp $$f $(OBJ_DIR); \
		fi;\
	done

transport: obj symver
	make -C kernel/$(TRANSPORT) -f $(MAKEFILE) PLATFORM=$(PLATFORM) V=$(V) all strip
	$(Q)for f in kernel/$(TRANSPORT)/nano_$(TRANSPORT).mod kernel/$(TRANSPORT)/nano_$(TRANSPORT).ko; do \
		if test -f $$f; then \
			cp $$f $(OBJ_DIR); \
		fi;\
	done

# loopback target is essentially identical to transport.  However, loopback does not depend on symver,
# because the module generated for loop back test  does not depend on nano_if.ko
loopback: obj
	make -C kernel/$(TRANSPORT) -f $(MAKEFILE) PLATFORM=$(PLATFORM) V=$(V) LOOPBACK_TEST=yes all strip
	$(Q)for f in kernel/$(TRANSPORT)/nano_$(TRANSPORT).mod kernel/$(TRANSPORT)/nano_$(TRANSPORT).ko; do \
		if test -f $$f; then \
			cp $$f $(OBJ_DIR); \
		fi;\
	done

install: ic transport 
	$(Q)echo "Installing in $(INSTALL_DIR)"
	cp obj/nano_if.ko obj/nano_$(TRANSPORT).ko $(INSTALL_DIR)
	cp obj/x_mac.axf $(INSTALL_DIR)
#	$(Q)sudo cp obj/nano_if.ko obj/nano_$(TRANSPORT).ko $(INSTALL_DIR)
#	$(Q)echo "Running depmod..."
#	$(Q)sudo depmod -a

clean:
	make -C kernel/ic -f $(MAKEFILE) PLATFORM=$(PLATFORM) clean
	make -C kernel/$(TRANSPORT) -f $(MAKEFILE) PLATFORM=$(PLATFORM) clean
	$(Q)for d in `ls userspace`; do \
		if test -f userspace/$$d/Makefile; then \
			(cd userspace/$$d && make clean) ;\
		fi;\
	done

SEEK_AND_DESTROY=*.o .*.cmd *.d *.symvers *.ko *.mod.c *.tmp_versions
veryclean: 
	@echo "Deleting files/dirs:" $(SEEK_AND_DESTROY)
	$(Q)for f in $(SEEK_AND_DESTROY); do \
		find $(HOSTDRIVER_DIR) -name $$f | xargs rm -rf ;\
	done
	$(Q)rm -rf $(OBJ_DIR) 

strip:
include targets.inc
-include include $(HOSTDRIVER_DIR)/platform/$(PLATFORM)/targets.inc
