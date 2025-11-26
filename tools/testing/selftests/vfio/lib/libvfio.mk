include $(top_srcdir)/scripts/subarch.include
ARCH ?= $(SUBARCH)

LIBVFIO_SRCDIR := $(selfdir)/vfio/lib

LIBVFIO_C := iommu.c
LIBVFIO_C += iova_allocator.c
LIBVFIO_C += libvfio.c
LIBVFIO_C += vfio_pci_device.c
LIBVFIO_C += vfio_pci_driver.c

ifeq ($(ARCH:x86_64=x86),x86)
LIBVFIO_C += drivers/ioat/ioat.c
LIBVFIO_C += drivers/dsa/dsa.c
endif

LIBVFIO_OUTPUT := $(OUTPUT)/libvfio

LIBVFIO_O := $(patsubst %.c, $(LIBVFIO_OUTPUT)/%.o, $(LIBVFIO_C))

LIBVFIO_O_DIRS := $(shell dirname $(LIBVFIO_O) | uniq)
$(shell mkdir -p $(LIBVFIO_O_DIRS))

CFLAGS += -I$(LIBVFIO_SRCDIR)/include

$(LIBVFIO_O): $(LIBVFIO_OUTPUT)/%.o : $(LIBVFIO_SRCDIR)/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c $< -o $@

EXTRA_CLEAN += $(LIBVFIO_OUTPUT)
