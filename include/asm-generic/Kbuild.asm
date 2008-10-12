ifneq ($(wildcard $(srctree)/arch/$(SRCARCH)/include/asm/kvm.h \
      		  $(srctree)/include/asm-$(SRCARCH)/kvm.h),)
header-y  += kvm.h
endif

ifneq ($(wildcard $(srctree)/arch/$(SRCARCH)/include/asm/a.out.h \
      		  $(srctree)/include/asm-$(SRCARCH)/a.out.h),)
unifdef-y += a.out.h
endif
unifdef-y += auxvec.h
unifdef-y += byteorder.h
unifdef-y += errno.h
unifdef-y += fcntl.h
unifdef-y += ioctl.h
unifdef-y += ioctls.h
unifdef-y += ipcbuf.h
unifdef-y += mman.h
unifdef-y += msgbuf.h
unifdef-y += param.h
unifdef-y += poll.h
unifdef-y += posix_types.h
unifdef-y += ptrace.h
unifdef-y += resource.h
unifdef-y += sembuf.h
unifdef-y += setup.h
unifdef-y += shmbuf.h
unifdef-y += sigcontext.h
unifdef-y += siginfo.h
unifdef-y += signal.h
unifdef-y += socket.h
unifdef-y += sockios.h
unifdef-y += stat.h
unifdef-y += statfs.h
unifdef-y += termbits.h
unifdef-y += termios.h
unifdef-y += types.h
unifdef-y += unistd.h
