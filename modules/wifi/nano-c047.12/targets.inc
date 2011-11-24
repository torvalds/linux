
-include tools/tools.inc

nrxapi: obj
	(cd userspace/libnrx/autotest; \
	gcc header2test.c -o header2test;)
	(cd userspace/libnrx; \
	touch *.c ;\
	autoreconf -fi; \
	./configure --host=$(HOST) \
	CFLAGS="-I$(KERNEL_DIR)/include \
	$(addprefix -I$(HOSTDRIVER_DIR)/, $(NANO_IF_INC) \
	 $(WIFI_ENGINE_INC))"; \
	make;)
	cp userspace/libnrx/libnrx.a $(OBJ_DIR)
	cp userspace/libnrx/autotest/nrxpriv $(OBJ_DIR)

hic-proxy: obj
	make -C userspace/hic-proxy \
	CFLAGS=-I$(addprefix $(HOSTDRIVER_DIR)/, $(NANO_IF_INC)) \
	OBJ_DIR=$(OBJ_DIR)

