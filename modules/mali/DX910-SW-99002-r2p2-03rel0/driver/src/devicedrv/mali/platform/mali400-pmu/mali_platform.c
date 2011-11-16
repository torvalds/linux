/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_platform.c
 * Platform specific Mali driver functions for Mali 400 PMU hardware
 */
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_platform.h"

#if USING_MALI_PMM

#include "mali_pmm.h"

/* Internal test on/off */
#define PMU_TEST 0

/** @brief PMU hardware info
 */
typedef struct platform_pmu
{
	u32 reg_base_addr;              /**< PMU registers base address */
	u32 reg_size;                   /**< PMU registers size */
	const char *name;               /**< PMU name */
	u32 irq_num;                    /**< PMU irq number */

	mali_io_address reg_mapped;     /**< IO-mapped pointer to registers */
} platform_pmu_t;

static platform_pmu_t *pmu_info = NULL;

/** @brief Register layout for hardware PMU
 */
typedef enum {
	PMU_REG_ADDR_MGMT_POWER_UP                  = 0x00,     /*< Power up register */
	PMU_REG_ADDR_MGMT_POWER_DOWN                = 0x04,     /*< Power down register */
	PMU_REG_ADDR_MGMT_STATUS                    = 0x08,     /*< Core sleep status register */
	PMU_REG_ADDR_MGMT_INT_MASK                  = 0x0C,     /*< Interrupt mask register */
	PMU_REG_ADDR_MGMT_INT_RAWSTAT               = 0x10,     /*< Interrupt raw status register */
	PMU_REG_ADDR_MGMT_INT_STAT                  = 0x14,     /*< Interrupt status register */
	PMU_REG_ADDR_MGMT_INT_CLEAR                 = 0x18,     /*< Interrupt clear register */
	PMU_REG_ADDR_MGMT_SW_DELAY                  = 0x1C,     /*< Software delay register */
	PMU_REG_ADDR_MGMT_MASTER_PWR_UP             = 0x24,     /*< Master power up register */
	PMU_REGISTER_ADDRESS_SPACE_SIZE             = 0x28,     /*< Size of register space */
} pmu_reg_addr_mgmt_addr;

/* Internal functions */
u32 pmu_reg_read(platform_pmu_t *pmu, u32 relative_address);
void pmu_reg_write(platform_pmu_t *pmu, u32 relative_address, u32 new_val);
mali_pmm_core_mask pmu_translate_cores_to_pmu(mali_pmm_core_mask cores);
#if PMU_TEST
void pmm_pmu_dump_regs( platform_pmu_t *pmu );
void pmm_pmu_test( platform_pmu_t *pmu, u32 cores );
#endif

#endif /* USING_MALI_PMM */


_mali_osk_errcode_t mali_platform_init(_mali_osk_resource_t *resource)
{
#if USING_MALI_PMM
	if( resource == NULL )
	{
		/* Nothing to set up for the system */	
	}
	else if( resource->type == PMU )
	{
		if( (resource->base == 0) ||
			(resource->description == NULL) )
		{
			/* NOTE: We currently don't care about any other resource settings */
			MALI_PRINT_ERROR(("PLATFORM mali400-pmu: Missing PMU set up information\n"));
			MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
		}

		MALI_DEBUG_ASSERT( pmu_info == NULL );
		pmu_info = (platform_pmu_t *)_mali_osk_malloc(sizeof(*pmu_info));
		MALI_CHECK_NON_NULL( pmu_info, _MALI_OSK_ERR_NOMEM );	

		/* All values get 0 as default */
		_mali_osk_memset(pmu_info, 0, sizeof(*pmu_info));
		
		pmu_info->reg_base_addr = resource->base;
		pmu_info->reg_size = (u32)PMU_REGISTER_ADDRESS_SPACE_SIZE;
		pmu_info->name = resource->description; 
		pmu_info->irq_num = resource->irq;

		if( _MALI_OSK_ERR_OK != _mali_osk_mem_reqregion(pmu_info->reg_base_addr, pmu_info->reg_size, pmu_info->name) )
		{
			MALI_PRINT_ERROR(("PLATFORM mali400-pmu: Could not request register region (0x%08X - 0x%08X) for %s\n",
					 pmu_info->reg_base_addr, pmu_info->reg_base_addr + pmu_info->reg_size - 1, pmu_info->name));
			goto cleanup;
		}
		else
		{
			MALI_DEBUG_PRINT( 4, ("PLATFORM mali400-pmu: Success: request_mem_region: (0x%08X - 0x%08X) for %s\n",
					 pmu_info->reg_base_addr, pmu_info->reg_base_addr + pmu_info->reg_size - 1, pmu_info->name));
		}

		pmu_info->reg_mapped = _mali_osk_mem_mapioregion( pmu_info->reg_base_addr, pmu_info->reg_size, pmu_info->name );

		if( 0 == pmu_info->reg_mapped )
		{
			MALI_PRINT_ERROR(("PLATFORM mali400-pmu: Could not ioremap registers for %s .\n", pmu_info->name));
			_mali_osk_mem_unreqregion( pmu_info->reg_base_addr, pmu_info->reg_size );
			goto cleanup;
		}
		else
		{
			MALI_DEBUG_PRINT( 4, ("PLATFORM mali400-pmu: Success: ioremap_nocache: Internal ptr: (0x%08X - 0x%08X) for %s\n",
					(u32) pmu_info->reg_mapped,
					((u32)pmu_info->reg_mapped)+ pmu_info->reg_size - 1,
					pmu_info->name));
		}

		MALI_DEBUG_PRINT( 4, ("PLATFORM mali400-pmu: Success: Mapping registers to %s\n", pmu_info->name));

#if PMU_TEST
		pmu_test(pmu_info, (MALI_PMM_CORE_GP));
		pmu_test(pmu_info, (MALI_PMM_CORE_GP|MALI_PMM_CORE_L2|MALI_PMM_CORE_PP0));
#endif

		MALI_DEBUG_PRINT( 4, ("PLATFORM mali400-pmu: Initialized - %s\n", pmu_info->name) );		
	}
	else
	{
		/* Didn't expect a different resource */
		MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
	}	

	MALI_SUCCESS;
	
cleanup:
	_mali_osk_free(pmu_info);
	pmu_info = NULL;
	MALI_ERROR(_MALI_OSK_ERR_NOMEM);

#else
	/* Nothing to do when not using PMM - as mali already on */
	MALI_SUCCESS;
#endif

}

_mali_osk_errcode_t mali_platform_deinit(_mali_osk_resource_type_t *type)
{
#if USING_MALI_PMM
	if( type == NULL )
	{
		/* Nothing to tear down for the system */
	}	
	else if (*type == PMU)
	{
		if( pmu_info )
		{
			_mali_osk_mem_unmapioregion(pmu_info->reg_base_addr, pmu_info->reg_size, pmu_info->reg_mapped);
			_mali_osk_mem_unreqregion(pmu_info->reg_base_addr, pmu_info->reg_size);
			_mali_osk_free(pmu_info);
			pmu_info = NULL;

			MALI_DEBUG_PRINT( 4, ("PLATFORM mali400-pmu: Terminated PMU\n") );
		}
	}
	else
	{
		/* Didn't expect a different resource */
		MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
	}	
		
	MALI_SUCCESS;

#else
	/* Nothing to do when not using PMM */
	MALI_SUCCESS;
#endif
}

_mali_osk_errcode_t mali_platform_powerdown(u32 cores)
{
#if USING_MALI_PMM
	u32 stat;
	u32 timeout;
	u32 cores_pmu;

	MALI_DEBUG_ASSERT_POINTER(pmu_info);
	MALI_DEBUG_ASSERT( cores != 0 ); /* Shouldn't receive zero from PMM */
	MALI_DEBUG_PRINT( 4, ("PLATFORM mali400-pmu: power down (0x%x)\n", cores) );

	cores_pmu = pmu_translate_cores_to_pmu(cores);
	pmu_reg_write( pmu_info, (u32)PMU_REG_ADDR_MGMT_POWER_DOWN, cores_pmu );

	/* Wait for cores to be powered down */
	timeout = 10; /* 10ms */ 
	do
	{
		/* Get status of sleeping cores */
		stat = pmu_reg_read( pmu_info, (u32)PMU_REG_ADDR_MGMT_STATUS );
		stat &= cores_pmu;
		if( stat == cores_pmu ) break; /* All cores we wanted are now asleep */
		_mali_osk_time_ubusydelay(1000); /* 1ms */
		timeout--;
	} while( timeout > 0 );

	if( timeout == 0 ) MALI_ERROR(_MALI_OSK_ERR_TIMEOUT);

	MALI_SUCCESS;

#else
	/* Nothing to do when not using PMM */
	MALI_SUCCESS;
#endif
}

_mali_osk_errcode_t mali_platform_powerup(u32 cores)
{
#if USING_MALI_PMM
	u32 cores_pmu;
	u32 stat;
	u32 timeout;
	
	MALI_DEBUG_ASSERT_POINTER(pmu_info);
	MALI_DEBUG_ASSERT( cores != 0 ); /* Shouldn't receive zero from PMM */
	MALI_DEBUG_PRINT( 4, ("PLATFORM mali400-pmu: power up (0x%x)\n", cores) );

	/* Don't use interrupts - just poll status */
	pmu_reg_write( pmu_info, (u32)PMU_REG_ADDR_MGMT_INT_MASK, 0 );
	cores_pmu = pmu_translate_cores_to_pmu(cores);
	pmu_reg_write( pmu_info, (u32)PMU_REG_ADDR_MGMT_POWER_UP, cores_pmu );

	timeout = 10; /* 10ms */ 
	do
	{
		/* Get status of sleeping cores */
		stat = pmu_reg_read( pmu_info, (u32)PMU_REG_ADDR_MGMT_STATUS );
		stat &= cores_pmu;
		if( stat == 0 ) break; /* All cores we wanted are now awake */
		_mali_osk_time_ubusydelay(1000); /* 1ms */
		timeout--;
	} while( timeout > 0 );

	if( timeout == 0 ) MALI_ERROR(_MALI_OSK_ERR_TIMEOUT);

	MALI_SUCCESS;

#else
	/* Nothing to do when not using PMM */
	MALI_SUCCESS;
#endif
}

void mali_gpu_utilization_handler(u32 utilization)
{
}

#if USING_MALI_PMM

/***** INTERNAL *****/

/** @brief Internal PMU function to translate the cores bit mask
 *         into something the hardware PMU understands
 *
 * @param cores PMM cores bitmask
 * @return PMU hardware cores bitmask
 */
u32 pmu_translate_cores_to_pmu(mali_pmm_core_mask cores)
{
	/* For Mali 400 PMU the cores mask is already the same as what
	 * the hardware PMU expects.
	 * For other hardware, some translation can be done here, by
	 * translating the MALI_PMM_CORE_* bits into specific hardware
	 * bits
	 */
	 return cores;
}

/** @brief Internal PMU function to read a PMU register
 *
 * @param pmu handle that identifies the PMU hardware
 * @param relative_address relative PMU hardware address to read from
 * @return 32-bit value that was read from the address
 */
u32 pmu_reg_read(platform_pmu_t *pmu, u32 relative_address)
{
	u32 read_val;

	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT((relative_address & 0x03) == 0);
	MALI_DEBUG_ASSERT(relative_address < pmu->reg_size);

	read_val = _mali_osk_mem_ioread32(pmu->reg_mapped, relative_address);

	MALI_DEBUG_PRINT( 5, ("PMU: reg_read: %s Addr:0x%04X Val:0x%08x\n",
			pmu->name, relative_address, read_val));

	return read_val;
}

/** @brief Internal PMU function to write to a PMU register
 *
 * @param pmu handle that identifies the PMU hardware
 * @param relative_address relative PMU hardware address to write to
 * @param new_val new 32-bit value to write into the address
 */
void pmu_reg_write(platform_pmu_t *pmu, u32 relative_address, u32 new_val)
{
	MALI_DEBUG_ASSERT_POINTER(pmu);
	MALI_DEBUG_ASSERT((relative_address & 0x03) == 0);
	MALI_DEBUG_ASSERT(relative_address < pmu->reg_size);

	MALI_DEBUG_PRINT( 5, ("PMU: reg_write: %s Addr:0x%04X Val:0x%08x\n",
			pmu->name, relative_address, new_val));

	_mali_osk_mem_iowrite32(pmu->reg_mapped, relative_address, new_val);
}

#if MALI_POWER_MGMT_TEST_SUITE

u32 pmu_get_power_up_down_info(void)
{
	return pmu_reg_read(pmu_info, (u32)PMU_REG_ADDR_MGMT_STATUS);
}

#endif /* MALI_POWER_MGMT_TEST_SUITE */

#endif /* USING_MALI_PMM */


#if USING_MALI_PMM && PMU_TEST

/***** TEST *****/

void pmu_dump_regs( platform_pmu_t *pmu )
{
	u32 addr;
	for( addr = 0x0; addr < PMU_REGISTER_ADDRESS_SPACE_SIZE; addr += 0x4 )
	{
		MALI_PRINT( ("PMU_REG: 0x%08x: 0x%04x\n", (addr + pmu->reg_base_addr), pmu_reg_read( pmu, addr ) ) );
	}
}

/* This function is an internal test for the PMU without any Mali h/w interaction */
void pmu_test( platform_pmu_t *pmu, u32 cores )
{
	u32 stat;
	u32 timeout;
	
	MALI_PRINT( ("PMU_TEST: Start\n") );
	
	pmu_dump_regs( pmu );
	
	MALI_PRINT( ("PMU_TEST: Power down cores: 0x%x\n", cores) );
	_mali_pmm_pmu_power_down( pmu, cores, MALI_TRUE );
	
	stat = pmu_reg_read( pmu, (u32)PMU_REG_ADDR_MGMT_STATUS );
	MALI_PRINT( ("PMU_TEST: %s\n", (stat & cores) == cores ? "SUCCESS" : "FAIL" ) );
	
	pmu_dump_regs( pmu );
	
	MALI_PRINT( ("PMU_TEST: Power up cores: 0x%x\n", cores) );
	_mali_pmm_pmu_power_up( pmu, cores, MALI_FALSE );
	
	MALI_PRINT( ("PMU_TEST: Waiting for power up...\n") );
	timeout = 1000; /* 1 sec */
	while( !_mali_pmm_pmu_irq_power_up(pmu) && timeout > 0 )
	{
		_mali_osk_time_ubusydelay(1000); /* 1ms */
		timeout--;		
	} 

	MALI_PRINT( ("PMU_TEST: Waited %dms for interrupt\n", (1000-timeout)) );
	stat = pmu_reg_read( pmu, (u32)PMU_REG_ADDR_MGMT_STATUS );
	MALI_PRINT( ("PMU_TEST: %s\n", (stat & cores) == 0 ? "SUCCESS" : "FAIL" ) );

	_mali_pmm_pmu_irq_power_up_clear(pmu);

	pmu_dump_regs( pmu );

	MALI_PRINT( ("PMU_TEST: Finish\n") );
}
#endif /* USING_MALI_PMM && PMU_TEST */
