/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KUnit API to allow symbols to be conditionally visible during KUnit
 * testing
 *
 * Copyright (C) 2022, Google LLC.
 * Author: Rae Moar <rmoar@google.com>
 */

#ifndef _KUNIT_VISIBILITY_H
#define _KUNIT_VISIBILITY_H

#if IS_ENABLED(CONFIG_KUNIT)
    /**
     * VISIBLE_IF_KUNIT - A macro that sets symbols to be static if
     * CONFIG_KUNIT is not enabled. Otherwise if CONFIG_KUNIT is enabled
     * there is no change to the symbol definition.
     */
    #define VISIBLE_IF_KUNIT
    /**
     * EXPORT_SYMBOL_IF_KUNIT(symbol) - Exports symbol into
     * EXPORTED_FOR_KUNIT_TESTING namespace only if CONFIG_KUNIT is
     * enabled. Must use MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING")
     * in test file in order to use symbols.
     * @symbol: the symbol identifier to export
     */
    #define EXPORT_SYMBOL_IF_KUNIT(symbol) EXPORT_SYMBOL_NS(symbol, "EXPORTED_FOR_KUNIT_TESTING")
#else
    #define VISIBLE_IF_KUNIT static
    #define EXPORT_SYMBOL_IF_KUNIT(symbol)
#endif

#endif /* _KUNIT_VISIBILITY_H */
