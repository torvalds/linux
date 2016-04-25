#ifndef __PERF_SVGHELPER_H
#define __PERF_SVGHELPER_H

#include <linux/types.h>

void open_svg(const char *filename, int cpus, int rows, u64 start, u64 end);
void svg_ubox(int Yslot, u64 start, u64 end, double height, const char *type, int fd, int err, int merges);
void svg_lbox(int Yslot, u64 start, u64 end, double height, const char *type, int fd, int err, int merges);
void svg_fbox(int Yslot, u64 start, u64 end, double height, const char *type, int fd, int err, int merges);
void svg_box(int Yslot, u64 start, u64 end, const char *type);
void svg_blocked(int Yslot, int cpu, u64 start, u64 end, const char *backtrace);
void svg_running(int Yslot, int cpu, u64 start, u64 end, const char *backtrace);
void svg_waiting(int Yslot, int cpu, u64 start, u64 end, const char *backtrace);
void svg_cpu_box(int cpu, u64 max_frequency, u64 turbo_frequency);


void svg_process(int cpu, u64 start, u64 end, int pid, const char *name, const char *backtrace);
void svg_cstate(int cpu, u64 start, u64 end, int type);
void svg_pstate(int cpu, u64 start, u64 end, u64 freq);


void svg_time_grid(double min_thickness);
void svg_io_legenda(void);
void svg_legenda(void);
void svg_wakeline(u64 start, int row1, int row2, const char *backtrace);
void svg_partial_wakeline(u64 start, int row1, char *desc1, int row2, char *desc2, const char *backtrace);
void svg_interrupt(u64 start, int row, const char *backtrace);
void svg_text(int Yslot, u64 start, const char *text);
void svg_close(void);
int svg_build_topology_map(char *sib_core, int sib_core_nr, char *sib_thr, int sib_thr_nr);

extern int svg_page_width;
extern u64 svg_highlight;
extern const char *svg_highlight_name;

#endif /* __PERF_SVGHELPER_H */
