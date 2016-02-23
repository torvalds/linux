#
# Makefile for building the Extended Verification Module(EVM)
#
obj-$(CONFIG_EVM) += evm.o

evm-y := evm_main.o evm_crypto.o evm_secfs.o
evm-$(CONFIG_FS_POSIX_ACL) += evm_posix_acl.o
