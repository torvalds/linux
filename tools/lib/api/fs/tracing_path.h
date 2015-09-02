#ifndef __API_FS_TRACING_PATH_H
#define __API_FS_TRACING_PATH_H

extern char tracing_path[];
extern char tracing_events_path[];

void tracing_path_set(const char *mountpoint);
const char *tracing_path_mount(void);

char *get_tracing_file(const char *name);
void put_tracing_file(char *file);

#endif /* __API_FS_TRACING_PATH_H */
