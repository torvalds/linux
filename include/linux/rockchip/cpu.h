#ifndef __MACH_ROCKCHIP_CPU_H
#define __MACH_ROCKCHIP_CPU_H

extern unsigned long rockchip_soc_id;

static inline bool cpu_is_rockchip(void) { return rockchip_soc_id; }

#define ROCKCHIP_CPU_MASK       0xffff0000
#define ROCKCHIP_CPU_RK2928     0x29280000
#define ROCKCHIP_CPU_RK3026     0x30260000
#define ROCKCHIP_CPU_RK312X     0x31260000
#define ROCKCHIP_CPU_RK3036     0x30360000
#define ROCKCHIP_CPU_RK30XX     0x30660000
#define ROCKCHIP_CPU_RK3066B    0x31680000
#define ROCKCHIP_CPU_RK3188     0x31880000
#define ROCKCHIP_CPU_RK319X     0x31900000
#define ROCKCHIP_CPU_RK3288     0x32880000

static inline bool cpu_is_rk2928(void)  { return (rockchip_soc_id & ROCKCHIP_CPU_MASK) == ROCKCHIP_CPU_RK2928; }
static inline bool cpu_is_rk3026(void)  { return (rockchip_soc_id & ROCKCHIP_CPU_MASK) == ROCKCHIP_CPU_RK3026; }
static inline bool cpu_is_rk312x(void)  { return (rockchip_soc_id & ROCKCHIP_CPU_MASK) == ROCKCHIP_CPU_RK312X; }
static inline bool cpu_is_rk3036(void)  { return (rockchip_soc_id & ROCKCHIP_CPU_MASK) == ROCKCHIP_CPU_RK3036; }
static inline bool cpu_is_rk30xx(void)  { return (rockchip_soc_id & ROCKCHIP_CPU_MASK) == ROCKCHIP_CPU_RK30XX; }
static inline bool cpu_is_rk3066b(void) { return (rockchip_soc_id & ROCKCHIP_CPU_MASK) == ROCKCHIP_CPU_RK3066B; }
static inline bool cpu_is_rk3188(void)  { return (rockchip_soc_id & ROCKCHIP_CPU_MASK) == ROCKCHIP_CPU_RK3188; }
static inline bool cpu_is_rk319x(void)  { return (rockchip_soc_id & ROCKCHIP_CPU_MASK) == ROCKCHIP_CPU_RK319X; }
static inline bool cpu_is_rk3288(void)  { return (rockchip_soc_id & ROCKCHIP_CPU_MASK) == ROCKCHIP_CPU_RK3288; }

#define ROCKCHIP_SOC_RK2926     (ROCKCHIP_CPU_RK2928 | 0x00)
#define ROCKCHIP_SOC_RK2928G    (ROCKCHIP_CPU_RK2928 | 0x01)
#define ROCKCHIP_SOC_RK2928L    (ROCKCHIP_CPU_RK2928 | 0x02)
#define ROCKCHIP_SOC_RK3028A    (ROCKCHIP_CPU_RK3026 | 0x03)
#define ROCKCHIP_SOC_RK3026     (ROCKCHIP_CPU_RK3026 | 0x04)
#define ROCKCHIP_SOC_RK3126     (ROCKCHIP_CPU_RK312X | 0x00)
#define ROCKCHIP_SOC_RK3128     (ROCKCHIP_CPU_RK312X | 0x01)
#define ROCKCHIP_SOC_RK3036     (ROCKCHIP_CPU_RK3036 | 0x00)
#define ROCKCHIP_SOC_RK3000     (ROCKCHIP_CPU_RK30XX | 0x00)
#define ROCKCHIP_SOC_RK3066     (ROCKCHIP_CPU_RK30XX | 0x01)
#define ROCKCHIP_SOC_RK3068     (ROCKCHIP_CPU_RK30XX | 0x02)
#define ROCKCHIP_SOC_RK3066B    (ROCKCHIP_CPU_RK3066B| 0x00)
#define ROCKCHIP_SOC_RK3168     (ROCKCHIP_CPU_RK3066B| 0x01)
#define ROCKCHIP_SOC_RK3028     (ROCKCHIP_CPU_RK3066B| 0x03)
#define ROCKCHIP_SOC_RK3188     (ROCKCHIP_CPU_RK3188 | 0x00)
#define ROCKCHIP_SOC_RK3188PLUS (ROCKCHIP_CPU_RK3188 | 0x10)
#define ROCKCHIP_SOC_RK3190     (ROCKCHIP_CPU_RK319X | 0x00)
#define ROCKCHIP_SOC_RK3288     (ROCKCHIP_CPU_RK3288 | 0x00)

static inline bool soc_is_rk2926(void)  { return rockchip_soc_id == ROCKCHIP_SOC_RK2926; }
static inline bool soc_is_rk2928g(void) { return rockchip_soc_id == ROCKCHIP_SOC_RK2928G; }
static inline bool soc_is_rk2928l(void) { return rockchip_soc_id == ROCKCHIP_SOC_RK2928L; }
static inline bool soc_is_rk3028a(void) { return rockchip_soc_id == ROCKCHIP_SOC_RK3028A; }
static inline bool soc_is_rk3026(void)  { return rockchip_soc_id == ROCKCHIP_SOC_RK3026; }
static inline bool soc_is_rk3126(void)  { return rockchip_soc_id == ROCKCHIP_SOC_RK3126; }
static inline bool soc_is_rk3128(void)  { return rockchip_soc_id == ROCKCHIP_SOC_RK3128; }
static inline bool soc_is_rk3036(void)  { return rockchip_soc_id == ROCKCHIP_SOC_RK3036; }
static inline bool soc_is_rk3000(void)  { return rockchip_soc_id == ROCKCHIP_SOC_RK3000; }
static inline bool soc_is_rk3066(void)  { return rockchip_soc_id == ROCKCHIP_SOC_RK3066; }
static inline bool soc_is_rk3068(void)  { return rockchip_soc_id == ROCKCHIP_SOC_RK3068; }
static inline bool soc_is_rk3066b(void) { return rockchip_soc_id == ROCKCHIP_SOC_RK3066B; }
static inline bool soc_is_rk3168(void)  { return rockchip_soc_id == ROCKCHIP_SOC_RK3168; }
static inline bool soc_is_rk3028(void)  { return rockchip_soc_id == ROCKCHIP_SOC_RK3028; }
static inline bool soc_is_rk3188(void)  { return rockchip_soc_id == ROCKCHIP_SOC_RK3188; }
static inline bool soc_is_rk3188plus(void) { return rockchip_soc_id == ROCKCHIP_SOC_RK3188PLUS; }
static inline bool soc_is_rk3190(void)  { return rockchip_soc_id == ROCKCHIP_SOC_RK3190; }
static inline bool soc_is_rk3288(void)  { return rockchip_soc_id == ROCKCHIP_SOC_RK3288; }

#endif
