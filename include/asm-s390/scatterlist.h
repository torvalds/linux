#ifndef _ASMS390_SCATTERLIST_H
#define _ASMS390_SCATTERLIST_H

struct scatterlist {
    struct page *page;
    unsigned int offset;
    unsigned int length;
};

#ifdef __s390x__
#define ISA_DMA_THRESHOLD (0xffffffffffffffffUL)
#else
#define ISA_DMA_THRESHOLD (0xffffffffUL)
#endif

#endif /* _ASMS390X_SCATTERLIST_H */
