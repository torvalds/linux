#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/slab.h>

#include "fp_printk.h"

#define OURMODNAME "fp_printk"

MODULE_AUTHOR("Guilherme Giacomo Simoes <trintaeoitogc@gmail.com>");
MODULE_DESCRIPTION("This lib will convert int to string float");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.1.1-BETA");

char *fp_printk(int number, int decimal_places)
{
	char *buffer = kmalloc(32, GFP_KERNEL);
    if (!buffer) {
        pr_err("Error: Memory allocation failed.\n");
        return NULL;
    }

	int buf_index = 30;
	int count_decimal_place = 0;
	int point_include = 0;

	for (; number && buf_index; --buf_index, number /= 10) {
		count_decimal_place++;
		if (!point_include && count_decimal_place > decimal_places) {
			buffer[buf_index] = '.';
			buf_index--;
			point_include = 1;
		}

		buffer[buf_index] = "0123456789"[number % 10];
	}

	return &buffer[buf_index + 1];
}

EXPORT_SYMBOL(fp_printk);

static int __init fp_printk_init(void)
{
	pr_info("%s: initial execute module", OURMODNAME);
	int n = 123334;
	char *s = fp_printk(n, 4);
	if(s){
		pr_info("%s: test for fp_printk: %s", OURMODNAME, s);
		kfree(s);
	}

	return 0;
}

static void __exit fp_printk_exit(void)
{
	pr_info("%s: module end", OURMODNAME);
}

module_init(fp_printk_init);
module_exit(fp_printk_exit);
