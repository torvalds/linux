QEMU_DISPLAY ?= none
ARCH ?= x86
ifeq ($(ARCH),x86)
b = b
endif
ZIMAGE=$(KDIR)/arch/$(ARCH)/boot/$(b)zImage
KCONFIG=$(KDIR)/.config

YOCTO_URL=http://downloads.yoctoproject.org/releases/yocto/yocto-2.3/machines/qemu/qemu$(ARCH)/
YOCTO_IMAGE=core-image-minimal-qemu$(ARCH).ext4
#YOCTO_IMAGE=core-image-minimal-dev-qemu$(ARCH).ext4
#YOCTO_IMAGE=core-image-sato-dev-qemu$(ARCH).ext4
#YOCTO_IMAGE=core-image-sato-qemu$(ARCH).ext4
#YOCTO_IMAGE=core-image-sato-sdk-qemu$(ARCH).ext4

QEMU_OPTS = -kernel $(ZIMAGE) \
	-device virtio-serial \
	-chardev pty,id=virtiocon0 -device virtconsole,chardev=virtiocon0 \
	-serial pipe:pipe1 -serial pipe:pipe2 \
	-net nic,model=virtio,vlan=0 -net tap,ifname=tap0,vlan=0,script=no,downscript=no \
	-drive file=$(YOCTO_IMAGE),if=virtio,format=raw \
	--append "root=/dev/vda console=hvc0" \
	--display $(QEMU_DISPLAY) -s

boot: .modinst tap0 pipe1.in pipe1.out pipe2.in pipe2.out
	ARCH=$(ARCH) qemu/qemu.sh $(QEMU_OPTS)

TEMPDIR := $(shell mktemp -u)

$(KCONFIG): qemu/kernel_config.x86
	cp $^ $@
	$(MAKE) -C $(KDIR) oldnoconfig

zImage: $(ZIMAGE)

$(ZIMAGE): $(KCONFIG)
	$(MAKE) -C $(KDIR)
	$(MAKE) -C $(KDIR) modules

.modinst: $(ZIMAGE) $(YOCTO_IMAGE)
	mkdir $(TEMPDIR)
	sudo mount -t ext4 -o loop $(YOCTO_IMAGE) $(TEMPDIR)
	sudo $(MAKE) -C $(KDIR) modules_install INSTALL_MOD_PATH=$(TEMPDIR)
	sudo umount $(TEMPDIR)
	rmdir $(TEMPDIR)
	sleep 1 && touch .modinst

gdb: $(ZIMAGE)
	gdb -ex "target remote localhost:1234" $(KDIR)/vmlinux

$(YOCTO_IMAGE):
	wget $(YOCTO_URL)/$(YOCTO_IMAGE)
	sudo qemu/prepare-image.sh $(YOCTO_IMAGE)

tap0:
	qemu/create_net.sh $@

pipe1.in:
	mkfifo $@

pipe1.out:
	mkfifo $@

pipe2.in: pipe1.out
	ln $< $@

pipe2.out: pipe1.in
	ln $< $@


clean::
	rm -f .modinst

.PHONY: clean tap0

