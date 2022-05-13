#ifndef _VM_H_
#define _VM_H_


typedef struct vm {
	int sys_fd;
	int fd;
	char *mem;
} virtual_machine;