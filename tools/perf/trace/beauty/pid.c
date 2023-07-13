// SPDX-License-Identifier: LGPL-2.1

size_t syscall_arg__scnprintf_pid(char *bf, size_t size, struct syscall_arg *arg)
{
	int pid = arg->val;
	struct trace *trace = arg->trace;
	size_t printed = scnprintf(bf, size, "%d", pid);
	struct thread *thread = machine__findnew_thread(trace->host, pid, pid);

	if (thread != NULL) {
		if (!thread__comm_set(thread))
			thread__set_comm_from_proc(thread);

		if (thread__comm_set(thread))
			printed += scnprintf(bf + printed, size - printed,
					     " (%s)", thread__comm_str(thread));
		thread__put(thread);
	}

	return printed;
}
