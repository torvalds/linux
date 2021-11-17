/*
 * Copyright (C) 2017 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#ifndef _DT_BINDINGS_GPIO_UNIPHIER_H
#define _DT_BINDINGS_GPIO_UNIPHIER_H

#define UNIPHIER_GPIO_LINES_PER_BANK	8

#define UNIPHIER_GPIO_IRQ_OFFSET	((UNIPHIER_GPIO_LINES_PER_BANK) * 15)

#define UNIPHIER_GPIO_PORT(bank, line)	\
			((UNIPHIER_GPIO_LINES_PER_BANK) * (bank) + (line))

#define UNIPHIER_GPIO_IRQ(n)		((UNIPHIER_GPIO_IRQ_OFFSET) + (n))

#endif /* _DT_BINDINGS_GPIO_UNIPHIER_H */
