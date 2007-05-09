#ifndef _LINUX_SCATTERLIST_H
#define _LINUX_SCATTERLIST_H

#include <asm/scatterlist.h>
#include <linux/mm.h>
#include <linux/string.h>

static inline void sg_set_buf(struct scatterlist *sg, const void *buf,
			      unsigned int buflen)
{
	sg->page = virt_to_page(buf);
	sg->offset = offset_in_page(buf);
	sg->length = buflen;
}

static inline void sg_init_one(struct scatterlist *sg, const void *buf,
			       unsigned int buflen)
{
	memset(sg, 0, sizeof(*sg));
	sg_set_buf(sg, buf, buflen);
}

#define sg_next(sg)		((sg) + 1)
#define sg_last(sg, nents)	(&(sg[(nents) - 1]))

/*
 * Loop over each sg element, following the pointer to a new list if necessary
 */
#define for_each_sg(sglist, sg, nr, __i)	\
	for (__i = 0, sg = (sglist); __i < (nr); __i++, sg = sg_next(sg))

#endif /* _LINUX_SCATTERLIST_H */
