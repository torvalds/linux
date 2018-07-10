#
# Makefile for Menu Program
#

CC_PTHREAD_FLAGS			 = -lpthread
CC_FLAGS                     = -c 
CC_OUTPUT_FLAGS				 = -o
CC                           = gcc
RM                           = rm
RM_FLAGS                     = -f

TARGET  =   test
OBJS    =   linktable.o  menu.o test.o

all:	$(OBJS)
	$(CC) $(CC_OUTPUT_FLAGS) $(TARGET) $(OBJS) 
rootfs:
	#gcc -o init linktable.c menu.c test.c -m32 -static -lpthread
	gcc -o init linktable.c menu.c test.c  -static -lpthread
	#gcc -o hello hello.c -m32 -static
	gcc -o hello hello.c  -static
	find init hello | cpio -o -Hnewc |gzip -9 > rootfs.img
	#qemu -kernel ../linux-3.18.6/arch/x86/boot/bzImage -initrd ../rootfs.img
.c.o:
	$(CC) $(CC_FLAGS) $<

clean:
	$(RM) $(RM_FLAGS)  $(OBJS) $(TARGET) *.bak
