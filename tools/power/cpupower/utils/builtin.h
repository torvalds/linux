#ifndef BUILTIN_H
#define BUILTIN_H

extern int cmd_set(int argc, const char **argv);
extern int cmd_info(int argc, const char **argv);
extern int cmd_freq_set(int argc, const char **argv);
extern int cmd_freq_info(int argc, const char **argv);
extern int cmd_idle_info(int argc, const char **argv);
extern int cmd_monitor(int argc, const char **argv);

extern void set_help(void);
extern void info_help(void);
extern void freq_set_help(void);
extern void freq_info_help(void);
extern void idle_info_help(void);
extern void monitor_help(void);

#endif
