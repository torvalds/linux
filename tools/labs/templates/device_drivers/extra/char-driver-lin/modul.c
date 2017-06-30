#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/ioctl.h>

#define MY_MAJOR 42
#define MY_MAX_MINORS 2
/* #define IOCTL_IN _IOC(_IOC_WRITE, 'k', 1, sizeof(my_ioctl_data)) */
#define MY_IOCTL_IN _IOC(_IOC_WRITE, 'k', 1, 0)

struct my_device_data {
    struct cdev cdev;
    /* my data starts here */
}devs[MY_MAX_MINORS];

MODULE_DESCRIPTION("My kernel module");
MODULE_AUTHOR("Me");
MODULE_LICENSE("GPL");

static int my_open(struct inode *inode, struct file *file) {
    struct my_device_data *my_data =
            container_of(inode->i_cdev, struct my_device_data, cdev);

	printk( KERN_DEBUG "[my_open]\n" );		
    /* validate access to device */
    file->private_data = my_data;
    /* initialize device */

    return 0;
}

static int my_close(struct inode *inode, struct file *file) {
	printk( KERN_DEBUG "[my_close]\n" );
    /* deinitialize device */
    return 0;
}

static int my_read(struct file *file, char __user *user_buffer, size_t size, loff_t *offset) {
    struct my_device_data *my_data =
             (struct my_device_data*) file->private_data;
	int sizeRead = 0;		 

	printk( KERN_DEBUG "[my_read]\n" );
    /* read data from device in my_data->buffer */
	/* if(copy_to_user(user_buffer, my_data->buffer, my_data->size))
        return -EFAULT; */

    return sizeRead;
}

static int my_write(struct file *file, const char __user *user_buffer, size_t size, loff_t *offset) {
    struct my_device_data *my_data =
             (struct my_device_data*) file->private_data;
	int sizeWritten = 0;		 

	printk( KERN_DEBUG "[my_write]\n" );
    /* copy_from_user */
	/* write data to device from my_data->buffer */
	sizeWritten	= size;						//only if sizeWritten == size !
	
    return sizeWritten;
}

static long my_ioctl (struct file *file, unsigned int cmd, unsigned long arg) {
    struct my_device_data *my_data =
         (struct my_device_data*) file->private_data;
	/* my_ioctl_data mid; */
	
	printk( KERN_DEBUG "[my_ioctl]\n" );
    switch(cmd) {
    case MY_IOCTL_IN:
        /* if( copy_from_user(&mid, (my_ioctl_data *) arg, sizeof(my_ioctl_data)) )
            return -EFAULT;  */	

        /* process data and execute command */
        break;
    default:
        return -ENOTTY;
    }
    return 0;
}


struct file_operations my_fops = {
    .owner = THIS_MODULE,
    .open = my_open,
    .read = my_read,
    .write = my_write,
    .release = my_close,
    .unlocked_ioctl = my_ioctl
};

int init_module(void) {
    int i, err;

	printk( KERN_DEBUG "[init_module]\n" );
    err = register_chrdev_region(MKDEV(MY_MAJOR, 0), MY_MAX_MINORS,"my_device_driver");
    if (err != 0) {
        /* report error */
        return err;
    }

    for(i = 0; i < MY_MAX_MINORS; i++) {
        /* initialize devs[i] fields */
        cdev_init(&devs[i].cdev, &my_fops);
        cdev_add(&devs[i].cdev, MKDEV(MY_MAJOR, i), 1);
    }

    return 0;
}

void cleanup_module(void) {
    int i;

	printk( KERN_DEBUG "[cleanup_module]\n" );
    for(i = 0; i < MY_MAX_MINORS; i++) {
        /* release devs[i] fields */
        cdev_del(&devs[i].cdev);
    }
    unregister_chrdev_region(MKDEV(MY_MAJOR, 0), MY_MAX_MINORS);
}

