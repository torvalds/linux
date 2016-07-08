
static size_t syscall_arg__scnprintf_flock(char *bf, size_t size,
					   struct syscall_arg *arg)
{
	int printed = 0, op = arg->val;

	if (op == 0)
		return scnprintf(bf, size, "NONE");
#define	P_CMD(cmd) \
	if ((op & LOCK_##cmd) == LOCK_##cmd) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #cmd); \
		op &= ~LOCK_##cmd; \
	}

	P_CMD(SH);
	P_CMD(EX);
	P_CMD(NB);
	P_CMD(UN);
	P_CMD(MAND);
	P_CMD(RW);
	P_CMD(READ);
	P_CMD(WRITE);
#undef P_OP

	if (op)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", op);

	return printed;
}

#define SCA_FLOCK syscall_arg__scnprintf_flock
