#ifndef _UAPI_LINUX_MEMBARRIER_H
#define _UAPI_LINUX_MEMBARRIER_H

/*
 * linux/membarrier.h
 *
 * membarrier system call API
 *
 * Copyright (c) 2010, 2015 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * enum membarrier_cmd - membarrier system call command
 * @MEMBARRIER_CMD_QUERY:   Query the set of supported commands. It returns
 *                          a bitmask of valid commands.
 * @MEMBARRIER_CMD_SHARED:  Execute a memory barrier on all running threads.
 *                          Upon return from system call, the caller thread
 *                          is ensured that all running threads have passed
 *                          through a state where all memory accesses to
 *                          user-space addresses match program order between
 *                          entry to and return from the system call
 *                          (non-running threads are de facto in such a
 *                          state). This covers threads from all processes
 *                          running on the system. This command returns 0.
 *
 * Command to be passed to the membarrier system call. The commands need to
 * be a single bit each, except for MEMBARRIER_CMD_QUERY which is assigned to
 * the value 0.
 */
enum membarrier_cmd {
	MEMBARRIER_CMD_QUERY = 0,
	MEMBARRIER_CMD_SHARED = (1 << 0),
};

#endif /* _UAPI_LINUX_MEMBARRIER_H */
