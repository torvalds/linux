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

/*
 * We overload the LSB of the page pointer to indicate whether it's
 * a valid sg entry, or whether it points to the start of a new scatterlist.
 * Those low bits are there for everyone! (thanks mason :-)
 */
#define sg_is_chain(sg)		((unsigned long) (sg)->page & 0x01)
#define sg_chain_ptr(sg)	\
	((struct scatterlist *) ((unsigned long) (sg)->page & ~0x01))

/**
 * sg_next - return the next scatterlist entry in a list
 * @sg:		The current sg entry
 *
 * Usually the next entry will be @sg@ + 1, but if this sg element is part
 * of a chained scatterlist, it could jump to the start of a new
 * scatterlist array.
 *
 * Note that the caller must ensure that there are further entries after
 * the current entry, this function will NOT return NULL for an end-of-list.
 *
 */
static inline struct scatterlist *sg_next(struct scatterlist *sg)
{
	sg++;

	if (unlikely(sg_is_chain(sg)))
		sg = sg_chain_ptr(sg);

	return sg;
}

/*
 * Loop over each sg element, following the pointer to a new list if necessary
 */
#define for_each_sg(sglist, sg, nr, __i)	\
	for (__i = 0, sg = (sglist); __i < (nr); __i++, sg = sg_next(sg))

/**
 * sg_last - return the last scatterlist entry in a list
 * @sgl:	First entry in the scatterlist
 * @nents:	Number of entries in the scatterlist
 *
 * Should only be used casually, it (currently) scan the entire list
 * to get the last entry.
 *
 * Note that the @sgl@ pointer passed in need not be the first one,
 * the important bit is that @nents@ denotes the number of entries that
 * exist from @sgl@.
 *
 */
static inline struct scatterlist *sg_last(struct scatterlist *sgl,
					  unsigned int nents)
{
#ifndef ARCH_HAS_SG_CHAIN
	struct scatterlist *ret = &sgl[nents - 1];
#else
	struct scatterlist *sg, *ret = NULL;
	int i;

	for_each_sg(sgl, sg, nents, i)
		ret = sg;

#endif
	return ret;
}

/**
 * sg_chain - Chain two sglists together
 * @prv:	First scatterlist
 * @prv_nents:	Number of entries in prv
 * @sgl:	Second scatterlist
 *
 * Links @prv@ and @sgl@ together, to form a longer scatterlist.
 *
 */
static inline void sg_chain(struct scatterlist *prv, unsigned int prv_nents,
			    struct scatterlist *sgl)
{
#ifndef ARCH_HAS_SG_CHAIN
	BUG();
#endif
	prv[prv_nents - 1].page = (struct page *) ((unsigned long) sgl | 0x01);
}

#endif /* _LINUX_SCATTERLIST_H */
