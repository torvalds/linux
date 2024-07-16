#ifndef COMMON_H_
#define COMMON_H_

extern int hugetlbfs_test;

unsigned long default_huge_page_size(void);
int sys_memfd_create(const char *name, unsigned int flags);

#endif
