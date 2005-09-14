/****************************************************************************/

/*
 *	mcfcache.h -- ColdFire CPU cache support code
 *
 *	(C) Copyright 2004, Greg Ungerer <gerg@snapgear.com>
 */

/****************************************************************************/
#ifndef	__M68KNOMMU_MCFCACHE_H
#define	__M68KNOMMU_MCFCACHE_H
/****************************************************************************/

#include <linux/config.h>

/*
 *	The different ColdFire families have different cache arrangments.
 *	Everything from a small instruction only cache, to configurable
 *	data and/or instruction cache, to unified instruction/data, to 
 *	harvard style separate instruction and data caches.
 */

#if defined(CONFIG_M5206) || defined(CONFIG_M5206e) || defined(CONFIG_M5272)
/*
 *	Simple version 2 core cache. These have instruction cache only,
 *	we just need to invalidate it and enable it.
 */
.macro CACHE_ENABLE
	movel	#0x01000000,%d0		/* invalidate cache cmd */
	movec	%d0,%CACR		/* do invalidate cache */
	movel	#0x80000100,%d0		/* setup cache mask */
	movec	%d0,%CACR		/* enable cache */
.endm
#endif /* CONFIG_M5206 || CONFIG_M5206e || CONFIG_M5272 */

#if defined(CONFIG_M523x) || defined(CONFIG_M527x)
/*
 *	New version 2 cores have a configurable split cache arrangement.
 *	For now I am just enabling instruction cache - but ultimately I
 *	think a split instruction/data cache would be better.
 */
.macro CACHE_ENABLE
	movel	#0x01400000,%d0
	movec	%d0,%CACR		/* invalidate cache */
	nop
	movel	#0x0000c000,%d0		/* set SDRAM cached only */
	movec	%d0,%ACR0
	movel	#0x00000000,%d0		/* no other regions cached */
	movec	%d0,%ACR1
	movel	#0x80400100,%d0		/* configure cache */
	movec	%d0,%CACR		/* enable cache */
	nop
.endm
#endif /* CONFIG_M523x || CONFIG_M527x */

#if defined(CONFIG_M528x)
.macro CACHE_ENABLE
	nop
	movel	#0x01000000, %d0
	movec	%d0, %CACR		/* Invalidate cache */
	nop
	movel	#0x0000c020, %d0	/* Set SDRAM cached only */
	movec	%d0, %ACR0
	movel	#0xff00c000, %d0	/* Cache Flash also */
	movec	%d0, %ACR1
	movel	#0x80000200, %d0	/* Setup cache mask */
	movec	%d0, %CACR		/* Enable cache */
	nop
.endm
#endif /* CONFIG_M528x */

#if defined(CONFIG_M5249) || defined(CONFIG_M5307)
/*
 *	The version 3 core cache. Oddly enough the version 2 core 5249
 *	has the same SDRAM and cache setup as the version 3 cores.
 *	This is a single unified instruction/data cache.
 */
.macro CACHE_ENABLE
	movel	#0x01000000,%d0		/* invalidate whole cache */
	movec	%d0,%CACR
	nop
#if defined(DEBUGGER_COMPATIBLE_CACHE) || defined(CONFIG_SECUREEDGEMP3)
	movel	#0x0000c000,%d0		/* set SDRAM cached (write-thru) */
#else
	movel	#0x0000c020,%d0		/* set SDRAM cached (copyback) */
#endif
	movec	%d0,%ACR0
	movel	#0x00000000,%d0		/* no other regions cached */
	movec	%d0,%ACR1
	movel	#0xa0000200,%d0		/* enable cache */
	movec	%d0,%CACR
	nop
.endm
#endif /* CONFIG_M5249 || CONFIG_M5307 */

#if defined(CONFIG_M5407)
/*
 *	Version 4 cores have a true harvard style separate instruction
 *	and data cache. Invalidate and enable cache, also enable write
 *	buffers and branch accelerator.
 */
.macro CACHE_ENABLE
	movel	#0x01040100,%d0		/* invalidate whole cache */
	movec	%d0,%CACR
	nop
	movel	#0x000fc000,%d0		/* set SDRAM cached only */
	movec	%d0, %ACR0
	movel	#0x00000000,%d0		/* no other regions cached */
	movec	%d0, %ACR1
	movel	#0x000fc000,%d0		/* set SDRAM cached only */
	movec	%d0, %ACR2
	movel	#0x00000000,%d0		/* no other regions cached */
	movec	%d0, %ACR3
	movel	#0xb6088400,%d0		/* enable caches */
	movec	%d0,%CACR
	nop
.endm
#endif /* CONFIG_M5407 */


/****************************************************************************/
#endif	/* __M68KNOMMU_MCFCACHE_H */
