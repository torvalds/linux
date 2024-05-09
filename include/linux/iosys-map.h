/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Pointer abstraction for IO/system memory
 */

#ifndef __IOSYS_MAP_H__
#define __IOSYS_MAP_H__

#include <linux/compiler_types.h>
#include <linux/io.h>
#include <linux/string.h>

/**
 * DOC: overview
 *
 * When accessing a memory region, depending on its location, users may have to
 * access it with I/O operations or memory load/store operations. For example,
 * copying to system memory could be done with memcpy(), copying to I/O memory
 * would be done with memcpy_toio().
 *
 * .. code-block:: c
 *
 *	void *vaddr = ...; // pointer to system memory
 *	memcpy(vaddr, src, len);
 *
 *	void *vaddr_iomem = ...; // pointer to I/O memory
 *	memcpy_toio(vaddr_iomem, src, len);
 *
 * The user of such pointer may not have information about the mapping of that
 * region or may want to have a single code path to handle operations on that
 * buffer, regardless if it's located in system or IO memory. The type
 * :c:type:`struct iosys_map <iosys_map>` and its helpers abstract that so the
 * buffer can be passed around to other drivers or have separate duties inside
 * the same driver for allocation, read and write operations.
 *
 * Open-coding access to :c:type:`struct iosys_map <iosys_map>` is considered
 * bad style. Rather then accessing its fields directly, use one of the provided
 * helper functions, or implement your own. For example, instances of
 * :c:type:`struct iosys_map <iosys_map>` can be initialized statically with
 * IOSYS_MAP_INIT_VADDR(), or at runtime with iosys_map_set_vaddr(). These
 * helpers will set an address in system memory.
 *
 * .. code-block:: c
 *
 *	struct iosys_map map = IOSYS_MAP_INIT_VADDR(0xdeadbeaf);
 *
 *	iosys_map_set_vaddr(&map, 0xdeadbeaf);
 *
 * To set an address in I/O memory, use IOSYS_MAP_INIT_VADDR_IOMEM() or
 * iosys_map_set_vaddr_iomem().
 *
 * .. code-block:: c
 *
 *	struct iosys_map map = IOSYS_MAP_INIT_VADDR_IOMEM(0xdeadbeaf);
 *
 *	iosys_map_set_vaddr_iomem(&map, 0xdeadbeaf);
 *
 * Instances of struct iosys_map do not have to be cleaned up, but
 * can be cleared to NULL with iosys_map_clear(). Cleared mappings
 * always refer to system memory.
 *
 * .. code-block:: c
 *
 *	iosys_map_clear(&map);
 *
 * Test if a mapping is valid with either iosys_map_is_set() or
 * iosys_map_is_null().
 *
 * .. code-block:: c
 *
 *	if (iosys_map_is_set(&map) != iosys_map_is_null(&map))
 *		// always true
 *
 * Instances of :c:type:`struct iosys_map <iosys_map>` can be compared for
 * equality with iosys_map_is_equal(). Mappings that point to different memory
 * spaces, system or I/O, are never equal. That's even true if both spaces are
 * located in the same address space, both mappings contain the same address
 * value, or both mappings refer to NULL.
 *
 * .. code-block:: c
 *
 *	struct iosys_map sys_map; // refers to system memory
 *	struct iosys_map io_map; // refers to I/O memory
 *
 *	if (iosys_map_is_equal(&sys_map, &io_map))
 *		// always false
 *
 * A set up instance of struct iosys_map can be used to access or manipulate the
 * buffer memory. Depending on the location of the memory, the provided helpers
 * will pick the correct operations. Data can be copied into the memory with
 * iosys_map_memcpy_to(). The address can be manipulated with iosys_map_incr().
 *
 * .. code-block:: c
 *
 *	const void *src = ...; // source buffer
 *	size_t len = ...; // length of src
 *
 *	iosys_map_memcpy_to(&map, src, len);
 *	iosys_map_incr(&map, len); // go to first byte after the memcpy
 */

/**
 * struct iosys_map - Pointer to IO/system memory
 * @vaddr_iomem:	The buffer's address if in I/O memory
 * @vaddr:		The buffer's address if in system memory
 * @is_iomem:		True if the buffer is located in I/O memory, or false
 *			otherwise.
 */
struct iosys_map {
	union {
		void __iomem *vaddr_iomem;
		void *vaddr;
	};
	bool is_iomem;
};

/**
 * IOSYS_MAP_INIT_VADDR - Initializes struct iosys_map to an address in system memory
 * @vaddr_:	A system-memory address
 */
#define IOSYS_MAP_INIT_VADDR(vaddr_)	\
	{				\
		.vaddr = (vaddr_),	\
		.is_iomem = false,	\
	}

/**
 * IOSYS_MAP_INIT_VADDR_IOMEM - Initializes struct iosys_map to an address in I/O memory
 * @vaddr_iomem_:	An I/O-memory address
 */
#define IOSYS_MAP_INIT_VADDR_IOMEM(vaddr_iomem_)	\
	{						\
		.vaddr_iomem = (vaddr_iomem_),		\
		.is_iomem = true,			\
	}

/**
 * IOSYS_MAP_INIT_OFFSET - Initializes struct iosys_map from another iosys_map
 * @map_:	The dma-buf mapping structure to copy from
 * @offset_:	Offset to add to the other mapping
 *
 * Initializes a new iosys_map struct based on another passed as argument. It
 * does a shallow copy of the struct so it's possible to update the back storage
 * without changing where the original map points to. It is the equivalent of
 * doing:
 *
 * .. code-block:: c
 *
 *	iosys_map map = other_map;
 *	iosys_map_incr(&map, &offset);
 *
 * Example usage:
 *
 * .. code-block:: c
 *
 *	void foo(struct device *dev, struct iosys_map *base_map)
 *	{
 *		...
 *		struct iosys_map map = IOSYS_MAP_INIT_OFFSET(base_map, FIELD_OFFSET);
 *		...
 *	}
 *
 * The advantage of using the initializer over just increasing the offset with
 * iosys_map_incr() like above is that the new map will always point to the
 * right place of the buffer during its scope. It reduces the risk of updating
 * the wrong part of the buffer and having no compiler warning about that. If
 * the assignment to IOSYS_MAP_INIT_OFFSET() is forgotten, the compiler can warn
 * about the use of uninitialized variable.
 */
#define IOSYS_MAP_INIT_OFFSET(map_, offset_) ({				\
	struct iosys_map copy = *map_;					\
	iosys_map_incr(&copy, offset_);					\
	copy;								\
})

/**
 * iosys_map_set_vaddr - Sets a iosys mapping structure to an address in system memory
 * @map:	The iosys_map structure
 * @vaddr:	A system-memory address
 *
 * Sets the address and clears the I/O-memory flag.
 */
static inline void iosys_map_set_vaddr(struct iosys_map *map, void *vaddr)
{
	map->vaddr = vaddr;
	map->is_iomem = false;
}

/**
 * iosys_map_set_vaddr_iomem - Sets a iosys mapping structure to an address in I/O memory
 * @map:		The iosys_map structure
 * @vaddr_iomem:	An I/O-memory address
 *
 * Sets the address and the I/O-memory flag.
 */
static inline void iosys_map_set_vaddr_iomem(struct iosys_map *map,
					     void __iomem *vaddr_iomem)
{
	map->vaddr_iomem = vaddr_iomem;
	map->is_iomem = true;
}

/**
 * iosys_map_is_equal - Compares two iosys mapping structures for equality
 * @lhs:	The iosys_map structure
 * @rhs:	A iosys_map structure to compare with
 *
 * Two iosys mapping structures are equal if they both refer to the same type of memory
 * and to the same address within that memory.
 *
 * Returns:
 * True is both structures are equal, or false otherwise.
 */
static inline bool iosys_map_is_equal(const struct iosys_map *lhs,
				      const struct iosys_map *rhs)
{
	if (lhs->is_iomem != rhs->is_iomem)
		return false;
	else if (lhs->is_iomem)
		return lhs->vaddr_iomem == rhs->vaddr_iomem;
	else
		return lhs->vaddr == rhs->vaddr;
}

/**
 * iosys_map_is_null - Tests for a iosys mapping to be NULL
 * @map:	The iosys_map structure
 *
 * Depending on the state of struct iosys_map.is_iomem, tests if the
 * mapping is NULL.
 *
 * Returns:
 * True if the mapping is NULL, or false otherwise.
 */
static inline bool iosys_map_is_null(const struct iosys_map *map)
{
	if (map->is_iomem)
		return !map->vaddr_iomem;
	return !map->vaddr;
}

/**
 * iosys_map_is_set - Tests if the iosys mapping has been set
 * @map:	The iosys_map structure
 *
 * Depending on the state of struct iosys_map.is_iomem, tests if the
 * mapping has been set.
 *
 * Returns:
 * True if the mapping is been set, or false otherwise.
 */
static inline bool iosys_map_is_set(const struct iosys_map *map)
{
	return !iosys_map_is_null(map);
}

/**
 * iosys_map_clear - Clears a iosys mapping structure
 * @map:	The iosys_map structure
 *
 * Clears all fields to zero, including struct iosys_map.is_iomem, so
 * mapping structures that were set to point to I/O memory are reset for
 * system memory. Pointers are cleared to NULL. This is the default.
 */
static inline void iosys_map_clear(struct iosys_map *map)
{
	if (map->is_iomem) {
		map->vaddr_iomem = NULL;
		map->is_iomem = false;
	} else {
		map->vaddr = NULL;
	}
}

/**
 * iosys_map_memcpy_to - Memcpy into offset of iosys_map
 * @dst:	The iosys_map structure
 * @dst_offset:	The offset from which to copy
 * @src:	The source buffer
 * @len:	The number of byte in src
 *
 * Copies data into a iosys_map with an offset. The source buffer is in
 * system memory. Depending on the buffer's location, the helper picks the
 * correct method of accessing the memory.
 */
static inline void iosys_map_memcpy_to(struct iosys_map *dst, size_t dst_offset,
				       const void *src, size_t len)
{
	if (dst->is_iomem)
		memcpy_toio(dst->vaddr_iomem + dst_offset, src, len);
	else
		memcpy(dst->vaddr + dst_offset, src, len);
}

/**
 * iosys_map_memcpy_from - Memcpy from iosys_map into system memory
 * @dst:	Destination in system memory
 * @src:	The iosys_map structure
 * @src_offset:	The offset from which to copy
 * @len:	The number of byte in src
 *
 * Copies data from a iosys_map with an offset. The dest buffer is in
 * system memory. Depending on the mapping location, the helper picks the
 * correct method of accessing the memory.
 */
static inline void iosys_map_memcpy_from(void *dst, const struct iosys_map *src,
					 size_t src_offset, size_t len)
{
	if (src->is_iomem)
		memcpy_fromio(dst, src->vaddr_iomem + src_offset, len);
	else
		memcpy(dst, src->vaddr + src_offset, len);
}

/**
 * iosys_map_incr - Increments the address stored in a iosys mapping
 * @map:	The iosys_map structure
 * @incr:	The number of bytes to increment
 *
 * Increments the address stored in a iosys mapping. Depending on the
 * buffer's location, the correct value will be updated.
 */
static inline void iosys_map_incr(struct iosys_map *map, size_t incr)
{
	if (map->is_iomem)
		map->vaddr_iomem += incr;
	else
		map->vaddr += incr;
}

/**
 * iosys_map_memset - Memset iosys_map
 * @dst:	The iosys_map structure
 * @offset:	Offset from dst where to start setting value
 * @value:	The value to set
 * @len:	The number of bytes to set in dst
 *
 * Set value in iosys_map. Depending on the buffer's location, the helper
 * picks the correct method of accessing the memory.
 */
static inline void iosys_map_memset(struct iosys_map *dst, size_t offset,
				    int value, size_t len)
{
	if (dst->is_iomem)
		memset_io(dst->vaddr_iomem + offset, value, len);
	else
		memset(dst->vaddr + offset, value, len);
}

#ifdef CONFIG_64BIT
#define __iosys_map_rd_io_u64_case(val_, vaddr_iomem_)				\
	u64: val_ = readq(vaddr_iomem_)
#define __iosys_map_wr_io_u64_case(val_, vaddr_iomem_)				\
	u64: writeq(val_, vaddr_iomem_)
#else
#define __iosys_map_rd_io_u64_case(val_, vaddr_iomem_)				\
	u64: memcpy_fromio(&(val_), vaddr_iomem_, sizeof(u64))
#define __iosys_map_wr_io_u64_case(val_, vaddr_iomem_)				\
	u64: memcpy_toio(vaddr_iomem_, &(val_), sizeof(u64))
#endif

#define __iosys_map_rd_io(val__, vaddr_iomem__, type__) _Generic(val__,		\
	u8: val__ = readb(vaddr_iomem__),					\
	u16: val__ = readw(vaddr_iomem__),					\
	u32: val__ = readl(vaddr_iomem__),					\
	__iosys_map_rd_io_u64_case(val__, vaddr_iomem__))

#define __iosys_map_rd_sys(val__, vaddr__, type__)				\
	val__ = READ_ONCE(*(type__ *)(vaddr__))

#define __iosys_map_wr_io(val__, vaddr_iomem__, type__) _Generic(val__,		\
	u8: writeb(val__, vaddr_iomem__),					\
	u16: writew(val__, vaddr_iomem__),					\
	u32: writel(val__, vaddr_iomem__),					\
	__iosys_map_wr_io_u64_case(val__, vaddr_iomem__))

#define __iosys_map_wr_sys(val__, vaddr__, type__)				\
	WRITE_ONCE(*(type__ *)(vaddr__), val__)

/**
 * iosys_map_rd - Read a C-type value from the iosys_map
 *
 * @map__:	The iosys_map structure
 * @offset__:	The offset from which to read
 * @type__:	Type of the value being read
 *
 * Read a C type value (u8, u16, u32 and u64) from iosys_map. For other types or
 * if pointer may be unaligned (and problematic for the architecture supported),
 * use iosys_map_memcpy_from().
 *
 * Returns:
 * The value read from the mapping.
 */
#define iosys_map_rd(map__, offset__, type__) ({				\
	type__ val;								\
	if ((map__)->is_iomem) {						\
		__iosys_map_rd_io(val, (map__)->vaddr_iomem + (offset__), type__);\
	} else {								\
		__iosys_map_rd_sys(val, (map__)->vaddr + (offset__), type__);	\
	}									\
	val;									\
})

/**
 * iosys_map_wr - Write a C-type value to the iosys_map
 *
 * @map__:	The iosys_map structure
 * @offset__:	The offset from the mapping to write to
 * @type__:	Type of the value being written
 * @val__:	Value to write
 *
 * Write a C type value (u8, u16, u32 and u64) to the iosys_map. For other types
 * or if pointer may be unaligned (and problematic for the architecture
 * supported), use iosys_map_memcpy_to()
 */
#define iosys_map_wr(map__, offset__, type__, val__) ({				\
	type__ val = (val__);							\
	if ((map__)->is_iomem) {						\
		__iosys_map_wr_io(val, (map__)->vaddr_iomem + (offset__), type__);\
	} else {								\
		__iosys_map_wr_sys(val, (map__)->vaddr + (offset__), type__);	\
	}									\
})

/**
 * iosys_map_rd_field - Read a member from a struct in the iosys_map
 *
 * @map__:		The iosys_map structure
 * @struct_offset__:	Offset from the beginning of the map, where the struct
 *			is located
 * @struct_type__:	The struct describing the layout of the mapping
 * @field__:		Member of the struct to read
 *
 * Read a value from iosys_map considering its layout is described by a C struct
 * starting at @struct_offset__. The field offset and size is calculated and its
 * value read. If the field access would incur in un-aligned access, then either
 * iosys_map_memcpy_from() needs to be used or the architecture must support it.
 * For example: suppose there is a @struct foo defined as below and the value
 * ``foo.field2.inner2`` needs to be read from the iosys_map:
 *
 * .. code-block:: c
 *
 *	struct foo {
 *		int field1;
 *		struct {
 *			int inner1;
 *			int inner2;
 *		} field2;
 *		int field3;
 *	} __packed;
 *
 * This is the expected memory layout of a buffer using iosys_map_rd_field():
 *
 * +------------------------------+--------------------------+
 * | Address                      | Content                  |
 * +==============================+==========================+
 * | buffer + 0000                | start of mmapped buffer  |
 * |                              | pointed by iosys_map     |
 * +------------------------------+--------------------------+
 * | ...                          | ...                      |
 * +------------------------------+--------------------------+
 * | buffer + ``struct_offset__`` | start of ``struct foo``  |
 * +------------------------------+--------------------------+
 * | ...                          | ...                      |
 * +------------------------------+--------------------------+
 * | buffer + wwww                | ``foo.field2.inner2``    |
 * +------------------------------+--------------------------+
 * | ...                          | ...                      |
 * +------------------------------+--------------------------+
 * | buffer + yyyy                | end of ``struct foo``    |
 * +------------------------------+--------------------------+
 * | ...                          | ...                      |
 * +------------------------------+--------------------------+
 * | buffer + zzzz                | end of mmaped buffer     |
 * +------------------------------+--------------------------+
 *
 * Values automatically calculated by this macro or not needed are denoted by
 * wwww, yyyy and zzzz. This is the code to read that value:
 *
 * .. code-block:: c
 *
 *	x = iosys_map_rd_field(&map, offset, struct foo, field2.inner2);
 *
 * Returns:
 * The value read from the mapping.
 */
#define iosys_map_rd_field(map__, struct_offset__, struct_type__, field__) ({	\
	struct_type__ *s;							\
	iosys_map_rd(map__, struct_offset__ + offsetof(struct_type__, field__),	\
		     typeof(s->field__));					\
})

/**
 * iosys_map_wr_field - Write to a member of a struct in the iosys_map
 *
 * @map__:		The iosys_map structure
 * @struct_offset__:	Offset from the beginning of the map, where the struct
 *			is located
 * @struct_type__:	The struct describing the layout of the mapping
 * @field__:		Member of the struct to read
 * @val__:		Value to write
 *
 * Write a value to the iosys_map considering its layout is described by a C
 * struct starting at @struct_offset__. The field offset and size is calculated
 * and the @val__ is written. If the field access would incur in un-aligned
 * access, then either iosys_map_memcpy_to() needs to be used or the
 * architecture must support it. Refer to iosys_map_rd_field() for expected
 * usage and memory layout.
 */
#define iosys_map_wr_field(map__, struct_offset__, struct_type__, field__, val__) ({	\
	struct_type__ *s;								\
	iosys_map_wr(map__, struct_offset__ + offsetof(struct_type__, field__),		\
		     typeof(s->field__), val__);					\
})

#endif /* __IOSYS_MAP_H__ */
