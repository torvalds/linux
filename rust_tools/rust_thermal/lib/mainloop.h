/* SPDX-License-Identifier: LGPL-2.1+ */
/* Copyright (C) 2022, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org> */
#ifndef __THERMAL_TOOLS_MAINLOOP_H
#define __THERMAL_TOOLS_MAINLOOP_H

typedef int (*mainloop_callback_t)(int fd, void *data);

extern int mainloop(unsigned int timeout);
extern int mainloop_add(int fd, mainloop_callback_t cb, void *data);
extern int mainloop_del(int fd);
extern void mainloop_exit(void);
extern int mainloop_init(void);
extern void mainloop_fini(void);

#endif
