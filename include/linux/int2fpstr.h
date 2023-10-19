#ifndef _LINUX_INT2FPSTR_H 
#define _LINUX_INT2FPSTR_H

#include <linux/kernel.h>
#include <linux/slab.h>

inline char *int2fpstr(int number, int decimal_places) 
{
	if(!number)	return "0";

	char *buffer = kmalloc(32, GFP_KERNEL);
	if (!buffer) {
		pr_err("Error: Memory allocation failed.\n");
		return NULL;
	}

	int buf_index = 30;
	int count_decimal_place = 0;
	int point_include = decimal_places < 1;

	int neg = number < 0;
	if(neg)
		number *= -1;

	for (; number && buf_index; 
		--buf_index, number /= 10) 
	{
		count_decimal_place++;
		if (!point_include 
			&& count_decimal_place > decimal_places) 
		{
			buffer[buf_index--] = '.';
			point_include = 1;
		}

		buffer[buf_index] = "0123456789"[number % 10];
	}

	if(neg)
		*(buffer + buf_index--) = '-';

	return &buffer[buf_index + 1];
}

#endif
