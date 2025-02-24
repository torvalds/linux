#ifndef _ENCRYPT_H
#define _ENCRYPT_H

#include <linux/unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <unistd.h>


#define SYS_s2_encrypt 548

static inline long s2_encrypt(const char *str, int key){
	return syscall(SYS_s2_encrypt, str, key);
}

#endif
