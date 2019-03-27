gen_html - a program for automatic generation of zstd manual 
============================================================

#### Introduction

This simple C++ program generates a single-page HTML manual from `zstd.h`.

The format of recognized comment blocks is following:
- comments of type `/*!` mean: this is a function declaration; switch comments with declarations
- comments of type `/**` and `/*-` mean: this is a comment; use a `<H2>` header for the first line
- comments of type `/*=` and `/**=` mean: use a `<H3>` header and show also all functions until first empty line
- comments of type `/*X` where `X` is different from above-mentioned are ignored

Moreover:
- `ZSTDLIB_API` is removed to improve readability
- `typedef` are detected and included even if uncommented
- comments of type `/**<` and `/*!<` are detected and only function declaration is highlighted (bold)


#### Usage

The program requires 3 parameters:
```
gen_html [zstd_version] [input_file] [output_html]
```

To compile program and generate zstd manual we have used: 
```
make
./gen_html.exe 1.1.1 ../../lib/zstd.h zstd_manual.html
```
