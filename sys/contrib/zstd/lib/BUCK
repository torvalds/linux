cxx_library(
    name='zstd',
    header_namespace='',
    exported_headers=['zstd.h'],
    visibility=['PUBLIC'],
    deps=[
        ':common',
        ':compress',
        ':decompress',
        ':deprecated',
    ],
)

cxx_library(
    name='compress',
    header_namespace='',
    visibility=['PUBLIC'],
    exported_headers=subdir_glob([
        ('compress', 'zstd*.h'),
    ]),
    srcs=glob(['compress/zstd*.c', 'compress/hist.c']),
    deps=[':common'],
)

cxx_library(
    name='decompress',
    header_namespace='',
    visibility=['PUBLIC'],
    headers=subdir_glob([
        ('decompress', '*_impl.h'),
    ]),
    srcs=glob(['decompress/zstd*.c']),
    deps=[
        ':common',
        ':legacy',
    ],
)

cxx_library(
    name='deprecated',
    header_namespace='',
    visibility=['PUBLIC'],
    exported_headers=subdir_glob([
        ('deprecated', '*.h'),
    ]),
    srcs=glob(['deprecated/*.c']),
    deps=[':common'],
)

cxx_library(
    name='legacy',
    header_namespace='',
    visibility=['PUBLIC'],
    exported_headers=subdir_glob([
        ('legacy', '*.h'),
    ]),
    srcs=glob(['legacy/*.c']),
    deps=[':common'],
    exported_preprocessor_flags=[
        '-DZSTD_LEGACY_SUPPORT=4',
    ],
)

cxx_library(
    name='zdict',
    header_namespace='',
    visibility=['PUBLIC'],
    exported_headers=subdir_glob([
        ('dictBuilder', 'zdict.h'),
    ]),
    headers=subdir_glob([
        ('dictBuilder', 'divsufsort.h'),
        ('dictBuilder', 'cover.h'),
    ]),
    srcs=glob(['dictBuilder/*.c']),
    deps=[':common'],
)

cxx_library(
    name='compiler',
    header_namespace='',
    visibility=['PUBLIC'],
    exported_headers=subdir_glob([
        ('common', 'compiler.h'),
    ]),
)

cxx_library(
    name='cpu',
    header_namespace='',
    visibility=['PUBLIC'],
    exported_headers=subdir_glob([
        ('common', 'cpu.h'),
    ]),
)

cxx_library(
    name='bitstream',
    header_namespace='',
    visibility=['PUBLIC'],
    exported_headers=subdir_glob([
        ('common', 'bitstream.h'),
    ]),
)

cxx_library(
    name='entropy',
    header_namespace='',
    visibility=['PUBLIC'],
    exported_headers=subdir_glob([
        ('common', 'fse.h'),
        ('common', 'huf.h'),
    ]),
    srcs=[
        'common/entropy_common.c',
        'common/fse_decompress.c',
        'compress/fse_compress.c',
        'compress/huf_compress.c',
        'decompress/huf_decompress.c',
    ],
    deps=[
        ':debug',
        ':bitstream',
        ':compiler',
        ':errors',
        ':mem',
    ],
)

cxx_library(
    name='errors',
    header_namespace='',
    visibility=['PUBLIC'],
    exported_headers=subdir_glob([
        ('common', 'error_private.h'),
        ('common', 'zstd_errors.h'),
    ]),
    srcs=['common/error_private.c'],
)

cxx_library(
    name='mem',
    header_namespace='',
    visibility=['PUBLIC'],
    exported_headers=subdir_glob([
        ('common', 'mem.h'),
    ]),
)

cxx_library(
    name='pool',
    header_namespace='',
    visibility=['PUBLIC'],
    exported_headers=subdir_glob([
        ('common', 'pool.h'),
    ]),
    srcs=['common/pool.c'],
    deps=[
        ':threading',
        ':zstd_common',
    ],
)

cxx_library(
    name='threading',
    header_namespace='',
    visibility=['PUBLIC'],
    exported_headers=subdir_glob([
        ('common', 'threading.h'),
    ]),
    srcs=['common/threading.c'],
    exported_preprocessor_flags=[
        '-DZSTD_MULTITHREAD',
    ],
    exported_linker_flags=[
        '-pthread',
    ],
)

cxx_library(
    name='xxhash',
    header_namespace='',
    visibility=['PUBLIC'],
    exported_headers=subdir_glob([
        ('common', 'xxhash.h'),
    ]),
    srcs=['common/xxhash.c'],
    exported_preprocessor_flags=[
        '-DXXH_NAMESPACE=ZSTD_',
    ],
)

cxx_library(
    name='zstd_common',
    header_namespace='',
    visibility=['PUBLIC'],
    exported_headers=subdir_glob([
        ('', 'zstd.h'),
        ('common', 'zstd_internal.h'),
    ]),
    srcs=['common/zstd_common.c'],
    deps=[
        ':compiler',
        ':errors',
        ':mem',
    ],
)

cxx_library(
    name='debug',
    header_namespace='',
    visibility=['PUBLIC'],
    exported_headers=subdir_glob([
        ('common', 'debug.h'),
    ]),
    srcs=['common/debug.c'],
)

cxx_library(
    name='common',
    deps=[
        ':debug',
        ':bitstream',
        ':compiler',
        ':cpu',
        ':entropy',
        ':errors',
        ':mem',
        ':pool',
        ':threading',
        ':xxhash',
        ':zstd_common',
    ]
)
