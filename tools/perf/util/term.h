/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_TERM_H
#define __PERF_TERM_H

struct termios;
struct winsize;

void get_term_dimensions(struct winsize *ws);
void set_term_quiet_input(struct termios *old);

#endif /* __PERF_TERM_H */
