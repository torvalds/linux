// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016 Facebook
 * Copyright (c) 2018 Covalent IO, Inc. http://covalent.io
 */
#include <uapi/linux/btf.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>

bool bpf_verifier_log_attr_valid(const struct bpf_verifier_log *log)
{
	return log->len_total >= 128 && log->len_total <= UINT_MAX >> 2 &&
	       log->level && log->ubuf && !(log->level & ~BPF_LOG_MASK);
}

void bpf_verifier_vlog(struct bpf_verifier_log *log, const char *fmt,
		       va_list args)
{
	unsigned int n;

	n = vscnprintf(log->kbuf, BPF_VERIFIER_TMP_LOG_SIZE, fmt, args);

	if (log->level == BPF_LOG_KERNEL) {
		bool newline = n > 0 && log->kbuf[n - 1] == '\n';

		pr_err("BPF: %s%s", log->kbuf, newline ? "" : "\n");
		return;
	}

	n = min(log->len_total - log->len_used - 1, n);
	log->kbuf[n] = '\0';
	if (!copy_to_user(log->ubuf + log->len_used, log->kbuf, n + 1))
		log->len_used += n;
	else
		log->ubuf = NULL;
}

void bpf_vlog_reset(struct bpf_verifier_log *log, u32 new_pos)
{
	char zero = 0;

	if (!bpf_verifier_log_needed(log))
		return;

	log->len_used = new_pos;
	if (put_user(zero, log->ubuf + new_pos))
		log->ubuf = NULL;
}

/* log_level controls verbosity level of eBPF verifier.
 * bpf_verifier_log_write() is used to dump the verification trace to the log,
 * so the user can figure out what's wrong with the program
 */
__printf(2, 3) void bpf_verifier_log_write(struct bpf_verifier_env *env,
					   const char *fmt, ...)
{
	va_list args;

	if (!bpf_verifier_log_needed(&env->log))
		return;

	va_start(args, fmt);
	bpf_verifier_vlog(&env->log, fmt, args);
	va_end(args);
}
EXPORT_SYMBOL_GPL(bpf_verifier_log_write);

__printf(2, 3) void bpf_log(struct bpf_verifier_log *log,
			    const char *fmt, ...)
{
	va_list args;

	if (!bpf_verifier_log_needed(log))
		return;

	va_start(args, fmt);
	bpf_verifier_vlog(log, fmt, args);
	va_end(args);
}
EXPORT_SYMBOL_GPL(bpf_log);
