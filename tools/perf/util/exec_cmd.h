#ifndef __PERF_EXEC_CMD_H
#define __PERF_EXEC_CMD_H

extern void exec_cmd_init(const char *exec_name, const char *prefix,
			  const char *exec_path, const char *exec_path_env);

extern void perf_set_argv_exec_path(const char *exec_path);
extern const char *perf_extract_argv0_path(const char *path);
extern void setup_path(void);
extern int execv_perf_cmd(const char **argv); /* NULL terminated */
extern int execl_perf_cmd(const char *cmd, ...);
/* perf_exec_path and system_path return malloc'd string, caller must free it */
extern char *perf_exec_path(void);
extern char *system_path(const char *path);

#endif /* __PERF_EXEC_CMD_H */
