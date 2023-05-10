CONFIG_MODULE_SIG=n

FNAME_c := fp_printk

PWD := $(shell pwd)
obj-m += $(FNAME_c).o
EXTRA_CFLAGS += -DDEBUG
KDIR := /lib/modules/$(shell uname -r)/build

all:
	@echo
	@echo '--- Building : KDIR=${KDIR} EXTRA_CFLAGS=${EXTRA_CFLAGS} ---'
	@echo
	make -C $(KDIR) M=$(PWD)
install:
	@echo
	@echo "--- installing ---"
	@echo " [First, invoke the 'make' ]"
	make
	@echo
	@echo " [Now for the 'sudo make install' ]"
	sudo make -C $(KDIR) M=$(PWD) modules_install
	sudo depmod
clean:
	@echo
	@echo "--- cleaning ---"
	@echo
	make -C $(KDIR) M=$(PWD) clean
	rm -f *~   # from 'indent'

INDENT := indent
indent:
	@echo
	@echo "--- applying kernel code style indentation with indent ---"
	@echo
	mkdir bkp 2> /dev/null; cp -f *.[chsS] bkp/
	${INDENT} -linux --line-length95 *.[chsS]
sa:
	make sa_sparse
	make sa_gcc
	make sa_flawfinder
	make sa_cppcheck
sa_sparse:
	make clean
	@echo
	@echo "--- static analysis with sparse ---"
	@echo
	make C=2 CHECK="/usr/bin/sparse" -C $(KDIR) M=$(PWD) modules
sa_gcc:
	make clean
	@echo
	@echo "--- static analysis with gcc ---"
	@echo
	make W=1 -C $(KDIR) M=$(PWD) modules
sa_flawfinder:
	make clean
	@echo
	@echo "--- static analysis with flawfinder ---"
	@echo
	flawfinder *.[ch]
sa_cppcheck:
	make clean
	@echo
	@echo "--- static analysis with cppcheck ---"
	@echo
	cppcheck -v --force --enable=all -i .tmp_versions/ -i *.mod.c -i bkp/ --suppress=missingIncludeSystem .
