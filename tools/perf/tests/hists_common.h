/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_TESTS__HISTS_COMMON_H__
#define __PERF_TESTS__HISTS_COMMON_H__

struct machine;
struct machines;

#define FAKE_PID_PERF1  100
#define FAKE_PID_PERF2  200
#define FAKE_PID_BASH   300

#define FAKE_MAP_PERF    0x400000
#define FAKE_MAP_BASH    0x400000
#define FAKE_MAP_LIBC    0x500000
#define FAKE_MAP_KERNEL  0xf00000
#define FAKE_MAP_LENGTH  0x100000

#define FAKE_SYM_OFFSET1  700
#define FAKE_SYM_OFFSET2  800
#define FAKE_SYM_OFFSET3  900
#define FAKE_SYM_LENGTH   100

#define FAKE_IP_PERF_MAIN  FAKE_MAP_PERF + FAKE_SYM_OFFSET1
#define FAKE_IP_PERF_RUN_COMMAND  FAKE_MAP_PERF + FAKE_SYM_OFFSET2
#define FAKE_IP_PERF_CMD_RECORD  FAKE_MAP_PERF + FAKE_SYM_OFFSET3
#define FAKE_IP_BASH_MAIN  FAKE_MAP_BASH + FAKE_SYM_OFFSET1
#define FAKE_IP_BASH_XMALLOC  FAKE_MAP_BASH + FAKE_SYM_OFFSET2
#define FAKE_IP_BASH_XFREE  FAKE_MAP_BASH + FAKE_SYM_OFFSET3
#define FAKE_IP_LIBC_MALLOC  FAKE_MAP_LIBC + FAKE_SYM_OFFSET1
#define FAKE_IP_LIBC_FREE  FAKE_MAP_LIBC + FAKE_SYM_OFFSET2
#define FAKE_IP_LIBC_REALLOC  FAKE_MAP_LIBC + FAKE_SYM_OFFSET3
#define FAKE_IP_KERNEL_SCHEDULE  FAKE_MAP_KERNEL + FAKE_SYM_OFFSET1
#define FAKE_IP_KERNEL_PAGE_FAULT  FAKE_MAP_KERNEL + FAKE_SYM_OFFSET2
#define FAKE_IP_KERNEL_SYS_PERF_EVENT_OPEN  FAKE_MAP_KERNEL + FAKE_SYM_OFFSET3

/*
 * The setup_fake_machine() provides a test environment which consists
 * of 3 processes that have 3 mappings and in turn, have 3 symbols
 * respectively.  See below table:
 *
 * Command:  Pid  Shared Object               Symbol
 * .............  .............  ...................
 *    perf:  100           perf  main
 *    perf:  100           perf  run_command
 *    perf:  100           perf  cmd_record
 *    perf:  100           libc  malloc
 *    perf:  100           libc  free
 *    perf:  100           libc  realloc
 *    perf:  100       [kernel]  schedule
 *    perf:  100       [kernel]  page_fault
 *    perf:  100       [kernel]  sys_perf_event_open
 *    perf:  200           perf  main
 *    perf:  200           perf  run_command
 *    perf:  200           perf  cmd_record
 *    perf:  200           libc  malloc
 *    perf:  200           libc  free
 *    perf:  200           libc  realloc
 *    perf:  200       [kernel]  schedule
 *    perf:  200       [kernel]  page_fault
 *    perf:  200       [kernel]  sys_perf_event_open
 *    bash:  300           bash  main
 *    bash:  300           bash  xmalloc
 *    bash:  300           bash  xfree
 *    bash:  300           libc  malloc
 *    bash:  300           libc  free
 *    bash:  300           libc  realloc
 *    bash:  300       [kernel]  schedule
 *    bash:  300       [kernel]  page_fault
 *    bash:  300       [kernel]  sys_perf_event_open
 */
struct machine *setup_fake_machine(struct machines *machines);

void print_hists_in(struct hists *hists);
void print_hists_out(struct hists *hists);

#endif /* __PERF_TESTS__HISTS_COMMON_H__ */
