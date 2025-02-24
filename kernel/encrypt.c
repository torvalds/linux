#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define MAX_BUF_SIZE 512

SYSCALL_DEFINE2(s2_encrypt, char __user *, str, int, key){
	printk(KERN_INFO "Entry s2_encrypt\n");
	char * buf = kmalloc(MAX_BUF_SIZE, GFP_KERNEL);
	if(!buf)
		return -ENOMEM;

	long copied = strncpy_from_user(buf, str, sizeof(buf));
	if(copied<0||copied == sizeof(buf))
		return -EFAULT;

	if(key<1||key>5)
		return -EINVAL;
	char* index = buf;
	while(index){
		*index = (*index) + key;
		index++;
	}
	printk(KERN_INFO "Encrypted Msg: \"%s\"\n", buf);
	return 0L;	

}
