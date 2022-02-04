/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Pointer abstraction for IO/system memory
 */

#ifndef __IOSYS_MAP_H__
#define __IOSYS_MAP_H__

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
 *	memcpy_toio(vaddr, _iomem, src, len);
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
 * To set an address in I/O memory, use iosys_map_set_vaddr_iomem().
 *
 * .. code-block:: c
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
 * iosys_map_memcpy_to - Memcpy into iosys mapping
 * @dst:	The iosys_map structure
 * @src:	The source buffer
 * @len:	The number of byte in src
 *
 * Copies data into a iosys mapping. The source buffer is in system
 * memory. Depending on the buffer's location, the helper picks the correct
 * method of accessing the memory.
 */
static inline void iosys_map_memcpy_to(struct iosys_map *dst, const void *src,
				       size_t len)
{
	if (dst->is_iomem)
		memcpy_toio(dst->vaddr_iomem, src, len);
	else
		memcpy(dst->vaddr, src, len);
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

#endif /* __IOSYS_MAP_H__ */
