/* Public domain. */

#ifndef _LINUX_IOSYS_MAP_H
#define _LINUX_IOSYS_MAP_H

#include <linux/io.h>
#include <linux/string.h>

struct iosys_map {
	union {
		void *vaddr_iomem;
		void *vaddr;
	};
	bool is_iomem;
	bus_space_handle_t bsh;
	bus_size_t size;
};

static inline void
iosys_map_incr(struct iosys_map *ism, size_t n)
{
	if (ism->is_iomem)
		ism->vaddr_iomem += n;
	else
		ism->vaddr += n;
}

static inline void
iosys_map_memcpy_to(struct iosys_map *ism, size_t off, const void *src,
    size_t len)
{
	if (ism->is_iomem)
		memcpy_toio(ism->vaddr_iomem + off, src, len);
	else
		memcpy(ism->vaddr + off, src, len);
}

static inline void
iosys_map_memset(struct iosys_map *ism, size_t off, int c, size_t len)
{
	if (ism->is_iomem)
		memset_io(ism->vaddr_iomem + off, c, len);
	else
		memset(ism->vaddr + off, c, len);
}

static inline bool
iosys_map_is_null(const struct iosys_map *ism)
{
	if (ism->is_iomem)
		return (ism->vaddr_iomem == NULL);
	else
		return (ism->vaddr == NULL);
}

static inline bool
iosys_map_is_set(const struct iosys_map *ism)
{
	if (ism->is_iomem)
		return (ism->vaddr_iomem != NULL);
	else
		return (ism->vaddr != NULL);
}

static inline void
iosys_map_clear(struct iosys_map *ism)
{
	if (ism->is_iomem) {
		ism->vaddr_iomem = NULL;
		ism->is_iomem = false;
	} else {
		ism->vaddr = NULL;
	}
}

static inline void
iosys_map_set_vaddr_iomem(struct iosys_map *ism, void *addr)
{
	ism->vaddr_iomem = addr;
	ism->is_iomem = true;
}

static inline void
iosys_map_set_vaddr(struct iosys_map *ism, void *addr)
{
	ism->vaddr = addr;
	ism->is_iomem = false;
}

static inline struct iosys_map
IOSYS_MAP_INIT_OFFSET(struct iosys_map *ism, size_t off)
{
	struct iosys_map nism = *ism;
	iosys_map_incr(&nism, off);
	return nism;
}

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112

#define iosys_map_rd(_ism, _o, _t) ({					\
	_t v;								\
	if ((_ism)->is_iomem) {						\
		void *addr = (_ism)->vaddr_iomem + (_o);		\
		v = _Generic(v,						\
		    uint8_t : ioread8(addr),				\
		    uint16_t: ioread16(addr),				\
		    uint32_t: ioread32(addr),				\
		    uint64_t: ioread64(addr));				\
	} else								\
		v = READ_ONCE(*(_t *)((_ism)->vaddr + (_o)));		\
	v;								\
})

#define iosys_map_wr(_ism, _o, _t, _v) ({				\
	_t v = (_v);							\
	if ((_ism)->is_iomem) {						\
		void *addr = (_ism)->vaddr_iomem + (_o);		\
		_Generic(v,						\
		    uint8_t : iowrite8(v, addr),			\
		    uint16_t: iowrite16(v, addr),			\
		    uint32_t: iowrite32(v, addr),			\
		    uint64_t: iowrite64(v, addr));			\
	} else								\
		WRITE_ONCE(*(_t *)((_ism)->vaddr + (_o)), v);		\
})

#define iosys_map_rd_field(_ism, _o, _t, _f) ({				\
	_t *t;								\
	iosys_map_rd(_ism, _o + offsetof(_t, _f), __typeof(t->_f));	\
})

#define iosys_map_wr_field(_ism, _o, _t, _f, _v) ({			\
        _t *t;								\
        iosys_map_wr(_ism, _o + offsetof(_t, _f), __typeof(t->_f), _v); \
})

#endif /* C11 */

#endif
