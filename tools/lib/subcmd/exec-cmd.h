#ifndef __SUBCMD_EXEC_CMD_H
#define __SUBCMD_EXEC_CMD_H

extern void exec_cmd_init(const char *exec_name, const char *prefix,
			  const char *exec_path, const char *exec_path_env);

extern void set_argv_exec_path(const char *exec_path);
extern const char *extract_argv0_path(const char *path);
extern void setup_path(void);
extern int execv_cmd(const char **argv); /* NULL terminated */
extern int execl_cmd(const char *cmd, ...);
/* get_argv_exec_path and system_path return malloc'd string, caller must free it */
extern char *get_argv_exec_path(void);
extern char *system_path(const char *path);

#endif /* __SUBCMD_EXEC_CMD_H */
