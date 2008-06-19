#ifndef __SMC91X_H__
#define __SMC91X_H__

#define SMC91X_USE_8BIT (1 << 0)
#define SMC91X_USE_16BIT (1 << 1)
#define SMC91X_USE_32BIT (1 << 2)

#define SMC91X_NOWAIT		(1 << 3)

struct smc91x_platdata {
	unsigned long flags;
};

#endif /* __SMC91X_H__ */
