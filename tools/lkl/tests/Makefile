define valgrind_test
	VALGRIND_CMD="valgrind --suppressions=valgrind.supp --leak-check=full --show-leak-kinds=all --xml=yes --xml-file=valgrind-$1.xml" \
	./boot.sh -t $1 ;					     \
	./valgrind2xunit.py valgrind-$1.xml
endef

define valgrind_test_text
	VALGRIND_CMD="valgrind --suppressions=valgrind.supp --leak-check=full --show-leak-kinds=all" \
	./boot.sh -t $1
endef

all:
	./boot.sh -t ext4
	./boot.sh -t btrfs
	./boot.sh -t vfat
	./boot.sh -t xfs

valgrind:
	$(call valgrind_test,ext4)
	$(call valgrind_test,btrfs)
	$(call valgrind_test,vfat)
	$(call valgrind_test,xfs)
