CFLAGS = $(KHDR_INCLUDES)
TEST_GEN_PROGS += vfio_dma_mapping_test
TEST_GEN_PROGS += vfio_iommufd_setup_test
TEST_GEN_PROGS += vfio_pci_device_test
TEST_GEN_PROGS += vfio_pci_driver_test
TEST_PROGS_EXTENDED := run.sh
include ../lib.mk
include lib/libvfio.mk

CFLAGS += -I$(top_srcdir)/tools/include
CFLAGS += -MD
CFLAGS += $(EXTRA_CFLAGS)

$(TEST_GEN_PROGS): %: %.o $(LIBVFIO_O)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $< $(LIBVFIO_O) $(LDLIBS) -o $@

TEST_GEN_PROGS_O = $(patsubst %, %.o, $(TEST_GEN_PROGS))
TEST_DEP_FILES = $(patsubst %.o, %.d, $(TEST_GEN_PROGS_O) $(LIBVFIO_O))
-include $(TEST_DEP_FILES)

EXTRA_CLEAN += $(TEST_GEN_PROGS_O) $(TEST_DEP_FILES)
