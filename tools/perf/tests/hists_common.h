#ifndef __PERF_TESTS__HISTS_COMMON_H__
#define __PERF_TESTS__HISTS_COMMON_H__

struct machine;
struct machines;

/*
 * The setup_fake_machine() provides a test environment which consists
 * of 3 processes that have 3 mappings and in turn, have 3 symbols
 * respectively.  See below table:
 *
 * Command:  Pid  Shared Object               Symbol
 * .............  .............  ...................
 *    perf:  100           perf  main
 *    perf:  100           perf  run_command
 *    perf:  100           perf  comd_record
 *    perf:  100           libc  malloc
 *    perf:  100           libc  free
 *    perf:  100           libc  realloc
 *    perf:  100       [kernel]  schedule
 *    perf:  100       [kernel]  page_fault
 *    perf:  100       [kernel]  sys_perf_event_open
 *    perf:  200           perf  main
 *    perf:  200           perf  run_command
 *    perf:  200           perf  comd_record
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
