include $(top_srcdir)/scripts/subarch.include
ARCH ?= $(SUBARCH)

VFIO_DIR := $(selfdir)/vfio

LIBVFIO_C := lib/vfio_pci_device.c
LIBVFIO_C += lib/vfio_pci_driver.c

ifeq ($(ARCH:x86_64=x86),x86)
LIBVFIO_C += lib/drivers/ioat/ioat.c
LIBVFIO_C += lib/drivers/dsa/dsa.c
endif

LIBVFIO_O := $(patsubst %.c, $(OUTPUT)/%.o, $(LIBVFIO_C))

LIBVFIO_O_DIRS := $(shell dirname $(LIBVFIO_O) | uniq)
$(shell mkdir -p $(LIBVFIO_O_DIRS))

CFLAGS += -I$(VFIO_DIR)/lib/include

$(LIBVFIO_O): $(OUTPUT)/%.o : $(VFIO_DIR)/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c $< -o $@

EXTRA_CLEAN += $(LIBVFIO_O)
