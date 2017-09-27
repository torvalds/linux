#ifndef __SUBCMD_PAGER_H
#define __SUBCMD_PAGER_H

extern void pager_init(const char *pager_env);

extern void setup_pager(void);
extern int pager_in_use(void);
extern int pager_get_columns(void);

#endif /* __SUBCMD_PAGER_H */
