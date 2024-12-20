// SPDX-License-Identifier: GPL-2.0
#include <trace-seq.h>

int main(void)
{
	int rv = 0;
	struct trace_seq s;
	trace_seq_init(&s);
	rv += !(s.state == TRACE_SEQ__GOOD);
	trace_seq_destroy(&s);
	return rv;
}
