/*  Sysfs helper functions
 *
 *  Author: Andrei F. <luxneb@gmail.com>
 *  Derived from function implementation from Gokhan Moral
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 */

static inline int read_into(int *container, int size, const char *buf, size_t count)
{
	int i, j, t;
	i=0; j=0; t=0;

	for(j = 0; j < size; j++)
		*(container + j) = 0;

	for(j = 0; i < count; i++) {
		char c = buf[i];
		if(c >= '0' && c <= '9') {
			if(t < (j + 1)) 
				t = j + 1;
			if(t > size)
				return -EINVAL;
			*(container + j) *= 10;
			*(container + j) += (c - '0');
		} else if(c == ' ' || c == '\t' || c == '\n' ) {
			if(*(container + j) != 0) j++;
		} else
			break;
	}

	return t;
}
