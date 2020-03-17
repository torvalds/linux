#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/io.h>
#include <linux/uaccess.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>

MODULE_DESCRIPTION("KBD");
MODULE_AUTHOR("Kernel Hacker");
MODULE_LICENSE("GPL");

#define MODULE_NAME		"kbd"

#define KBD_MAJOR		42
#define KBD_MINOR		0
#define KBD_NR_MINORS	1

#define I8042_KBD_IRQ		1
#define I8042_STATUS_REG	0x64
#define I8042_DATA_REG		0x60

#define BUFFER_SIZE		1024
#define SCANCODE_RELEASED_MASK	0x80

struct kbd {
	struct cdev cdev;
	/* TODO 3: add spinlock */
	spinlock_t lock;
	char buf[BUFFER_SIZE];
	size_t put_idx, get_idx, count;
} devs[1];

/*
 * Checks if scancode corresponds to key press or release.
 */
static int is_key_press(unsigned int scancode)
{
	return !(scancode & SCANCODE_RELEASED_MASK);
}

/*
 * Return the character of the given scancode.
 * Only works for alphanumeric/space/enter; returns '?' for other
 * characters.
 */
static int get_ascii(unsigned int scancode)
{
	static char *row1 = "1234567890";
	static char *row2 = "qwertyuiop";
	static char *row3 = "asdfghjkl";
	static char *row4 = "zxcvbnm";

	scancode &= ~SCANCODE_RELEASED_MASK;
	if (scancode >= 0x02 && scancode <= 0x0b)
		return *(row1 + scancode - 0x02);
	if (scancode >= 0x10 && scancode <= 0x19)
		return *(row2 + scancode - 0x10);
	if (scancode >= 0x1e && scancode <= 0x26)
		return *(row3 + scancode - 0x1e);
	if (scancode >= 0x2c && scancode <= 0x32)
		return *(row4 + scancode - 0x2c);
	if (scancode == 0x39)
		return ' ';
	if (scancode == 0x1c)
		return '\n';
	return '?';
}

static void put_char(struct kbd *data, char c)
{
	if (data->count >= BUFFER_SIZE)
		return;

	data->buf[data->put_idx] = c;
	data->put_idx = (data->put_idx + 1) % BUFFER_SIZE;
	data->count++;
}

static bool get_char(char *c, struct kbd *data)
{
	/* TODO 4/6: get char from buffer; update count and get_idx */
	if (data->count > 0) {
		*c = data->buf[data->get_idx];
		data->get_idx = (data->get_idx + 1) % BUFFER_SIZE;
		data->count--;
		return true;
	}
	return false;
}

static void reset_buffer(struct kbd *data)
{
	/* TODO 5/3: reset count, put_idx, get_idx */
	data->count = 0;
	data->put_idx = 0;
	data->get_idx = 0;
}

/*
 * Return the value of the DATA register.
 */
static inline u8 i8042_read_data(void)
{
	u8 val;
	/* TODO 3: Read DATA register (8 bits). */
	val = inb(I8042_DATA_REG);
	return val;
}

/* TODO 2/27: implement interrupt handler */
irqreturn_t kbd_interrupt_handle(int irq_no, void *dev_id)
{

	unsigned int scancode = 0;
	int pressed, ch;

	/* TODO 3: read the scancode */
	scancode = i8042_read_data();
	/* TODO 3/2: interpret the scancode */
	pressed = is_key_press(scancode);
	ch = get_ascii(scancode);

	/* TODO 3/2: display information about the keystrokes */
	pr_info("IRQ %d: scancode=0x%x (%u) pressed=%d ch=%c\n",
		irq_no, scancode, scancode, pressed, ch);

	/* TODO 3/7: store ASCII key to buffer */
	if (pressed) {
		struct kbd *data = (struct kbd *)dev_id;

		spin_lock(&data->lock);
		put_char(data, ch);
		spin_unlock(&data->lock);
	}

	return IRQ_NONE;
}

static int kbd_open(struct inode *inode, struct file *file)
{
	struct kbd *data = container_of(inode->i_cdev, struct kbd, cdev);

	file->private_data = data;
	pr_info("%s opened\n", MODULE_NAME);
	return 0;
}

static int kbd_release(struct inode *inode, struct file *file)
{
	pr_info("%s closed\n", MODULE_NAME);
	return 0;
}

/* TODO 5/12: add write operation and reset the buffer */
static ssize_t kbd_write(struct file *file, const char __user *user_buffer,
			 size_t size, loff_t *offset)
{
	struct kbd *data = (struct kbd *) file->private_data;
	unsigned long flags;

	spin_lock_irqsave(&data->lock, flags);
	reset_buffer(data);
	spin_unlock_irqrestore(&data->lock, flags);

	return size;
}

static ssize_t kbd_read(struct file *file,  char __user *user_buffer,
			size_t size, loff_t *offset)
{
	struct kbd *data = (struct kbd *) file->private_data;
	size_t read = 0;
	/* TODO 4/18: read data from buffer */
	unsigned long flags;
	char ch;
	bool more = true;

	while (size--) {
		spin_lock_irqsave(&data->lock, flags);
		more = get_char(&ch, data);
		spin_unlock_irqrestore(&data->lock, flags);

		if (!more)
			break;

		if (put_user(ch, user_buffer++))
			return -EFAULT;

		read++;
	}

	return read;
}

static const struct file_operations kbd_fops = {
	.owner = THIS_MODULE,
	.open = kbd_open,
	.release = kbd_release,
	.read = kbd_read,
	/* TODO 5: add write operation */
	.write = kbd_write,
};

static int kbd_init(void)
{
	int err;

	err = register_chrdev_region(MKDEV(KBD_MAJOR, KBD_MINOR),
				     KBD_NR_MINORS, MODULE_NAME);
	if (err != 0) {
		pr_err("register_region failed: %d\n", err);
		goto out;
	}

	/* TODO 1/8: request the keyboard I/O ports */
	if (request_region(I8042_DATA_REG+1, 1, MODULE_NAME) == NULL) {
		err = -EBUSY;
		goto out_unregister;
	}
	if (request_region(I8042_STATUS_REG+1, 1, MODULE_NAME) == NULL) {
		err = -EBUSY;
		goto out_unregister;
	}

	/* TODO 3: initialize spinlock */
	spin_lock_init(&devs[0].lock);

	/* TODO 2/7: Register IRQ handler for keyboard IRQ (IRQ 1). */
	err = request_irq(I8042_KBD_IRQ,
			kbd_interrupt_handle,
			IRQF_SHARED, MODULE_NAME, &devs[0]);
	if (err != 0) {
		pr_err("request_irq failed: %d\n", err);
		goto out_release_regions;
	}

	cdev_init(&devs[0].cdev, &kbd_fops);
	cdev_add(&devs[0].cdev, MKDEV(KBD_MAJOR, KBD_MINOR), 1);

	pr_notice("Driver %s loaded\n", MODULE_NAME);
	return 0;

	/*TODO 2/3: release regions in case of error */
out_release_regions:
	release_region(I8042_STATUS_REG+1, 1);
	release_region(I8042_DATA_REG+1, 1);

out_unregister:
	unregister_chrdev_region(MKDEV(KBD_MAJOR, KBD_MINOR),
				 KBD_NR_MINORS);
out:
	return err;
}

static void kbd_exit(void)
{
	cdev_del(&devs[0].cdev);

	/* TODO 2: Free IRQ. */
	free_irq(I8042_KBD_IRQ, &devs[0]);

	/* TODO 1/2: release keyboard I/O ports */
	release_region(I8042_STATUS_REG+1, 1);
	release_region(I8042_DATA_REG+1, 1);


	unregister_chrdev_region(MKDEV(KBD_MAJOR, KBD_MINOR),
				 KBD_NR_MINORS);
	pr_notice("Driver %s unloaded\n", MODULE_NAME);
}

module_init(kbd_init);
module_exit(kbd_exit);
