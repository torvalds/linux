#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define MAX_BUF_SIZE 256

SYSCALL_DEFINE2(s2_encrypt, char __user *, str, int, key){
	//printk(KERN_INFO "Entry s2_encrypt\n");
	char * buf = kmalloc(MAX_BUF_SIZE, GFP_KERNEL);
	buf[MAX_BUF_SIZE-1] = '\0';
	if(!buf){
		printk(KERN_INFO "Memory Allocation Failed\n");
		return -ENOMEM;
	}
	long copied = strncpy_from_user(buf, str, MAX_BUF_SIZE);
	//printk(KERN_INFO "Copied %ld bytes, buf is now %s\n", copied, buf);
	if(copied<0||copied==MAX_BUF_SIZE){
		printk(KERN_INFO "String copy failed\n");
		return -EFAULT;
	}

	if(key<1||key>5){
		printk(KERN_INFO "Key value out of bounds\n");
		return -EINVAL;
	}
	char* index = buf;
	for(int i=0;i<copied;i++){
		index[i] = index[i]+key;
		//printk(KERN_INFO "index[%d] is now %c\n", i, index[i]);
	}
	printk(KERN_INFO "Encrypted Msg: \"%s\"\n", buf);
	return 0L;	

}
