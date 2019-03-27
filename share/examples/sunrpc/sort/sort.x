/* @(#)sort.x	2.1 88/08/11 4.0 RPCSRC */
/*
 * The sort procedure receives an array of strings and returns an array
 * of strings.  This toy service handles a maximum of 64 strings.
 */
const MAXSORTSIZE  = 64;
const MAXSTRINGLEN = 64;

typedef	string  str<MAXSTRINGLEN>;  /* the string itself */

struct sortstrings {
    str ss<MAXSORTSIZE>;
};

program SORTPROG {
    version SORTVERS {
        sortstrings SORT(sortstrings) = 1;
    } = 1;
} = 22855;
