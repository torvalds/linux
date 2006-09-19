unifdef-y += a.out.h auxvec.h byteorder.h errno.h fcntl.h ioctl.h	\
	ioctls.h ipcbuf.h mman.h msgbuf.h param.h poll.h		\
	posix_types.h ptrace.h resource.h sembuf.h shmbuf.h shmparam.h	\
	sigcontext.h siginfo.h signal.h socket.h sockios.h stat.h	\
	statfs.h termbits.h termios.h types.h unistd.h user.h

# These probably shouldn't be exported
unifdef-y += elf.h page.h
