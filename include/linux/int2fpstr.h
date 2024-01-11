#ifndef _LINUX_INT2FPSTR_H 
#define _LINUX_INT2FPSTR_H

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>

inline __kernel_size_t size_alloc(int number) 
{
	const int neg =  number < 0;
	if(neg) number *= -1;

	int ret = neg ? 2 : 1;
	for(; 
		number;
		number /= 10, ret++
	); 

	return ret;
}

inline void int2fpstr(int number, 
		const int decimal_places, char* dest) 
{
	if(!number)	{
		strncpy(dest, "0", 1);
		return;
	}

	const __kernel_size_t size_alloc_n = size_alloc(number);
	char* buffer = kmalloc(size_alloc_n, GFP_KERNEL);

	int buf_index = size_alloc_n;
	*(buffer + buf_index) = '\0';

	int c_dec_places = 0;
	int point_include = decimal_places < 1;

	int neg = number < 0;
	if(neg)
		number *= -1;

	for (; number && buf_index; 
		--buf_index, number /= 10) 
	{
		c_dec_places++;
		if (!point_include 
			&& c_dec_places > decimal_places) 
		{
			*(buffer + buf_index--) = '.';
			point_include = 1;
		}

		*(buffer + buf_index) = "0123456789"[number % 10];
	}

	if(neg)
		*(buffer + buf_index--) = '-';

	strncpy(dest, &buffer[buf_index+1], size_alloc_n);
}

#endif
