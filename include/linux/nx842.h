#ifndef __NX842_H__
#define __NX842_H__

#define __NX842_PSERIES_MEM_COMPRESS	(10240)
#define __NX842_POWERNV_MEM_COMPRESS	(1024)

#define NX842_MEM_COMPRESS	(max_t(unsigned int,			\
	__NX842_PSERIES_MEM_COMPRESS, __NX842_POWERNV_MEM_COMPRESS))

struct nx842_constraints {
	int alignment;
	int multiple;
	int minimum;
	int maximum;
};

int nx842_constraints(struct nx842_constraints *constraints);

int nx842_compress(const unsigned char *in, unsigned int in_len,
		   unsigned char *out, unsigned int *out_len, void *wrkmem);
int nx842_decompress(const unsigned char *in, unsigned int in_len,
		     unsigned char *out, unsigned int *out_len, void *wrkmem);

#endif
