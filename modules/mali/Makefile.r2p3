PWD=$(shell pwd)

MALI_DRV_ROOT=DX910-SW-99002-r2p3-01rel0/src/devicedrv/mali
MALI_UMP_ROOT=DX910-SW-99002-r2p3-01rel0/src/devicedrv/ump

install: build
	cp $(MALI_DRV_ROOT)/mali.ko $(LICHEE_MOD_DIR)/
	cp $(MALI_UMP_ROOT)/ump.ko $(LICHEE_MOD_DIR)/
	

build:
	@echo $(LICHEE_KDIR)
	$(MAKE) -C $(MALI_UMP_ROOT) CONFIG=ca8-virtex820-m400-1 KDIR=${LICHEE_KDIR}
	$(MAKE) -C $(MALI_DRV_ROOT) USING_MMU=1 USING_UMP=1 USING_PMM=1 BUILD=release \
		CONFIG=ca8-virtex820-m400-1 KDIR=${LICHEE_KDIR}



clean:
	$(MAKE) -C $(MALI_UMP_ROOT) CONFIG=ca8-virtex820-m400-1 KDIR=${LICHEE_KDIR} clean
	$(MAKE) -C $(MALI_DRV_ROOT) USING_MMU=1 USING_UMP=1 USING_PMM=1 BUILD=release \
		CONFIG=ca8-virtex820-m400-1 KDIR=${LICHEE_KDIR} clean

