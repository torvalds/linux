ifneq ($(wildcard $(srctree)/arch/$(SRCARCH)/include/asm/kvm.h \
		  $(srctree)/include/asm-$(SRCARCH)/kvm.h),)
header-y  += kvm.h
endif

ifneq ($(wildcard $(srctree)/arch/$(SRCARCH)/include/asm/kvm_para.h \
		  $(srctree)/include/asm-$(SRCARCH)/kvm_para.h),)
header-y  += kvm_para.h
endif

ifneq ($(wildcard $(srctree)/arch/$(SRCARCH)/include/asm/a.out.h \
		  $(srctree)/include/asm-$(SRCARCH)/a.out.h),)
header-y += a.out.h
endif

header-y += auxvec.h
header-y += bitsperlong.h
header-y += byteorder.h
header-y += errno.h
header-y += fcntl.h
header-y += ioctl.h
header-y += ioctls.h
header-y += ipcbuf.h
header-y += mman.h
header-y += msgbuf.h
header-y += param.h
header-y += poll.h
header-y += posix_types.h
header-y += ptrace.h
header-y += resource.h
header-y += sembuf.h
header-y += setup.h
header-y += shmbuf.h
header-y += sigcontext.h
header-y += siginfo.h
header-y += signal.h
header-y += socket.h
header-y += sockios.h
header-y += stat.h
header-y += statfs.h
header-y += swab.h
header-y += termbits.h
header-y += termios.h
header-y += types.h
header-y += unistd.h
