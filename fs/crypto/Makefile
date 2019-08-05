obj-$(CONFIG_FS_ENCRYPTION)	+= fscrypto.o

fscrypto-y := crypto.o \
	      fname.o \
	      hooks.o \
	      keysetup.o \
	      keysetup_v1.o \
	      policy.o

fscrypto-$(CONFIG_BLOCK) += bio.o
