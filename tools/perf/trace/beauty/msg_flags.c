#include <sys/types.h>
#include <sys/socket.h>

#ifndef MSG_PROBE
#define MSG_PROBE		     0x10
#endif
#ifndef MSG_WAITFORONE
#define MSG_WAITFORONE		   0x10000
#endif
#ifndef MSG_SENDPAGE_NOTLAST
#define MSG_SENDPAGE_NOTLAST	   0x20000
#endif
#ifndef MSG_FASTOPEN
#define MSG_FASTOPEN		0x20000000
#endif
#ifndef MSG_CMSG_CLOEXEC
# define MSG_CMSG_CLOEXEC	0x40000000
#endif

static size_t syscall_arg__scnprintf_msg_flags(char *bf, size_t size,
					       struct syscall_arg *arg)
{
	int printed = 0, flags = arg->val;

	if (flags == 0)
		return scnprintf(bf, size, "NONE");
#define	P_MSG_FLAG(n) \
	if (flags & MSG_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
		flags &= ~MSG_##n; \
	}

	P_MSG_FLAG(OOB);
	P_MSG_FLAG(PEEK);
	P_MSG_FLAG(DONTROUTE);
	P_MSG_FLAG(TRYHARD);
	P_MSG_FLAG(CTRUNC);
	P_MSG_FLAG(PROBE);
	P_MSG_FLAG(TRUNC);
	P_MSG_FLAG(DONTWAIT);
	P_MSG_FLAG(EOR);
	P_MSG_FLAG(WAITALL);
	P_MSG_FLAG(FIN);
	P_MSG_FLAG(SYN);
	P_MSG_FLAG(CONFIRM);
	P_MSG_FLAG(RST);
	P_MSG_FLAG(ERRQUEUE);
	P_MSG_FLAG(NOSIGNAL);
	P_MSG_FLAG(MORE);
	P_MSG_FLAG(WAITFORONE);
	P_MSG_FLAG(SENDPAGE_NOTLAST);
	P_MSG_FLAG(FASTOPEN);
	P_MSG_FLAG(CMSG_CLOEXEC);
#undef P_MSG_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}

#define SCA_MSG_FLAGS syscall_arg__scnprintf_msg_flags
