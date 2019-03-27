Zstandard wrapper for zlib
================================

The main objective of creating a zstd wrapper for [zlib](http://zlib.net/) is to allow a quick and smooth transition to zstd for projects already using zlib.

#### Required files

To build the zstd wrapper for zlib the following files are required:
- zlib.h
- a static or dynamic zlib library
- zlibWrapper/zstd_zlibwrapper.h
- zlibWrapper/zstd_zlibwrapper.c
- zlibWrapper/gz*.c files (gzclose.c, gzlib.c, gzread.c, gzwrite.c)
- zlibWrapper/gz*.h files (gzcompatibility.h, gzguts.h)
- a static or dynamic zstd library

The first two files are required by all projects using zlib and they are not included with the zstd distribution.
The further files are supplied with the zstd distribution.


#### Embedding the zstd wrapper within your project

Let's assume that your project that uses zlib is compiled with:
```gcc project.o -lz```

To compile the zstd wrapper with your project you have to do the following:
- change all references with `#include "zlib.h"` to `#include "zstd_zlibwrapper.h"`
- compile your project with `zstd_zlibwrapper.c`, `gz*.c` and a static or dynamic zstd library

The linking should be changed to:
```gcc project.o zstd_zlibwrapper.o gz*.c -lz -lzstd```


#### Enabling zstd compression within your project

After embedding the zstd wrapper within your project the zstd library is turned off by default.
Your project should work as before with zlib. There are two options to enable zstd compression:
- compilation with `-DZWRAP_USE_ZSTD=1` (or using `#define ZWRAP_USE_ZSTD 1` before `#include "zstd_zlibwrapper.h"`)
- using the `void ZWRAP_useZSTDcompression(int turn_on)` function (declared in `#include "zstd_zlibwrapper.h"`)

During decompression zlib and zstd streams are automatically detected and decompressed using a proper library.
This behavior can be changed using `ZWRAP_setDecompressionType(ZWRAP_FORCE_ZLIB)` what will make zlib decompression slightly faster.


#### Example
We have take the file `test/example.c` from [the zlib library distribution](http://zlib.net/) and copied it to [zlibWrapper/examples/example.c](examples/example.c).
After compilation and execution it shows the following results: 
```
zlib version 1.2.8 = 0x1280, compile flags = 0x65
uncompress(): hello, hello!
gzread(): hello, hello!
gzgets() after gzseek:  hello!
inflate(): hello, hello!
large_inflate(): OK
after inflateSync(): hello, hello!
inflate with dictionary: hello, hello!
```
Then we have changed `#include "zlib.h"` to `#include "zstd_zlibwrapper.h"`, compiled the [example.c](examples/example.c) file
with `-DZWRAP_USE_ZSTD=1` and linked with additional `zstd_zlibwrapper.o gz*.c -lzstd`.
We were forced to turn off the following functions: `test_flush`, `test_sync` which use currently unsupported features.
After running it shows the following results:
```
zlib version 1.2.8 = 0x1280, compile flags = 0x65
uncompress(): hello, hello!
gzread(): hello, hello!
gzgets() after gzseek:  hello!
inflate(): hello, hello!
large_inflate(): OK
inflate with dictionary: hello, hello!
```
The script used for compilation can be found at [zlibWrapper/Makefile](Makefile).


#### The measurement of performace of Zstandard wrapper for zlib

The zstd distribution contains a tool called `zwrapbench` which can measure speed and ratio of zlib, zstd, and the wrapper.
The benchmark is conducted using given filenames or synthetic data if filenames are not provided.
The files are read into memory and processed independently.
It makes benchmark more precise as it eliminates I/O overhead. 
Many filenames can be supplied as multiple parameters, parameters with wildcards or names of directories can be used as parameters with the -r option.
One can select compression levels starting from `-b` and ending with `-e`. The `-i` parameter selects minimal time used for each of tested levels.
With `-B` option bigger files can be divided into smaller, independently compressed blocks. 
The benchmark tool can be compiled with `make zwrapbench` using [zlibWrapper/Makefile](Makefile).


#### Improving speed of streaming compression

During streaming compression the compressor never knows how big is data to compress.
Zstandard compression can be improved by providing size of source data to the compressor. By default streaming compressor assumes that data is bigger than 256 KB but it can hurt compression speed on smaller data. 
The zstd wrapper provides the `ZWRAP_setPledgedSrcSize()` function that allows to change a pledged source size for a given compression stream.
The function will change zstd compression parameters what may improve compression speed and/or ratio.
It should be called just after `deflateInit()`or `deflateReset()` and before `deflate()` or `deflateSetDictionary()`. The function is only helpful when data is compressed in blocks. There will be no change in case of `deflateInit()` or `deflateReset()`  immediately followed by `deflate(strm, Z_FINISH)`
as this case is automatically detected.


#### Reusing contexts

The ordinary zlib compression of two files/streams allocates two contexts:
- for the 1st file calls `deflateInit`, `deflate`, `...`, `deflate`, `defalateEnd`
- for the 2nd file calls `deflateInit`, `deflate`, `...`, `deflate`, `defalateEnd`

The speed of compression can be improved with reusing a single context with following steps:
- initialize the context with `deflateInit`
- for the 1st file call `deflate`, `...`, `deflate`
- for the 2nd file call `deflateReset`, `deflate`, `...`, `deflate`
- free the context with `deflateEnd`

To check the difference we made experiments using `zwrapbench -ri6b6` with zstd and zlib compression (both at level 6).
The input data was decompressed git repository downloaded from https://github.com/git/git/archive/master.zip which contains 2979 files.
The table below shows that reusing contexts has a minor influence on zlib but it gives improvement for zstd.
In our example (the last 2 lines) it gives 4% better compression speed and 5% better decompression speed.

| Compression type                                  | Compression | Decompress.| Compr. size | Ratio |
| ------------------------------------------------- | ------------| -----------| ----------- | ----- |
| zlib 1.2.8                                        |  30.51 MB/s | 219.3 MB/s |     6819783 | 3.459 |
| zlib 1.2.8 not reusing a context                  |  30.22 MB/s | 218.1 MB/s |     6819783 | 3.459 |
| zlib 1.2.8 with zlibWrapper and reusing a context |  30.40 MB/s | 218.9 MB/s |     6819783 | 3.459 |
| zlib 1.2.8 with zlibWrapper not reusing a context |  30.28 MB/s | 218.1 MB/s |     6819783 | 3.459 |
| zstd 1.1.0 using ZSTD_CCtx                        |  68.35 MB/s | 430.9 MB/s |     6868521 | 3.435 |
| zstd 1.1.0 using ZSTD_CStream                     |  66.63 MB/s | 422.3 MB/s |     6868521 | 3.435 |
| zstd 1.1.0 with zlibWrapper and reusing a context |  54.01 MB/s | 403.2 MB/s |     6763482 | 3.488 |
| zstd 1.1.0 with zlibWrapper not reusing a context |  51.59 MB/s | 383.7 MB/s |     6763482 | 3.488 |


#### Compatibility issues
After enabling zstd compression not all native zlib functions are supported. When calling unsupported methods they put error message into `strm->msg` and return Z_STREAM_ERROR.

Supported methods:
- deflateInit
- deflate (with exception of Z_FULL_FLUSH, Z_BLOCK, and Z_TREES)
- deflateSetDictionary
- deflateEnd
- deflateReset
- deflateBound
- inflateInit
- inflate
- inflateSetDictionary
- inflateReset
- inflateReset2
- compress
- compress2
- compressBound
- uncompress
- gzip file access functions

Ignored methods (they do nothing):
- deflateParams

Unsupported methods:
- deflateCopy
- deflateTune
- deflatePending
- deflatePrime
- deflateSetHeader
- inflateGetDictionary
- inflateCopy
- inflateSync
- inflatePrime
- inflateMark
- inflateGetHeader
- inflateBackInit
- inflateBack
- inflateBackEnd
