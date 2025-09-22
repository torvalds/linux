// This is a slow larval-stage kludge to help massage the generated man
// pages.  It's used like this:
const char* const usage = 
"\nTakes on stdin, whitespace-separated words of the form\n"
"\n"
"    [bits/]stl_foo.h\n"
"    [bits/]std_foo.h\n"
"\n"
"and writes on stdout the nearest matching standard header name.\n"
"\n"
"Takes no command-line arguments.\n"
"\n";

#include <string>
#include <map>
#include <iostream>

typedef std::map<std::string, std::string>   Map;

Map  headers;

void init_map()
{
    // Enter the glamourous world of data entry!!  Maintain these!
    headers["algo.h"]                   = "algorithm";
    headers["algobase.h"]               = "algorithm";
    headers["algorithm.h"]              = "algorithm";
    headers["heap.h"]                   = "algorithm";
    headers["bitset.h"]                 = "bitset";
    headers["complex.h"]                = "complex";
    //headers["construct.h"]              stl_construct.h entirely internal
    headers["deque.h"]                  = "deque";
    headers["deque.tcc"]                = "deque";
    headers["fstream.h"]                = "fstream";
    headers["fstream.tcc"]              = "fstream";
    headers["function.h"]               = "functional";
    headers["functional.h"]             = "functional";
    headers["iomanip.h"]                = "iomanip";
    headers["basic_ios.h"]              = "ios";
    headers["basic_ios.tcc"]            = "ios";
    headers["ios.h"]                    = "ios";
    headers["iosfwd.h"]                 = "iosfwd";
    headers["iostream.h"]               = "iostream";
    headers["istream.h"]                = "istream";
    headers["istream.tcc"]              = "istream";
    headers["iterator.h"]               = "iterator";
    headers["iterator_base_funcs.h"]    = "iterator";
    headers["iterator_base_types.h"]    = "iterator";
    headers["stream_iterator.h"]        = "iterator";
    headers["streambuf_iterator.h"]     = "iterator";
    headers["limits.h"]                 = "limits";
    headers["list.h"]                   = "list";
    headers["list.tcc"]                 = "list";
    headers["codecvt.h"]                = "locale";
    headers["locale.h"]                 = "locale";
    headers["localefwd.h"]              = "locale";
    headers["locale_classes.h"]         = "locale";
    headers["locale_facets.h"]          = "locale";
    headers["locale_facets.tcc"]        = "locale";
    headers["map.h"]                    = "map";
    headers["multimap.h"]               = "map";
    headers["memory.h"]                 = "memory";
    headers["allocator.h"]              = "memory";
    headers["raw_storage_iter.h"]       = "memory";
    headers["tempbuf.h"]                = "memory";
    headers["uninitialized.h"]          = "memory";
    headers["numeric.h"]                = "numeric";
    headers["ostream.h"]                = "ostream";
    headers["ostream.tcc"]              = "ostream";
    headers["queue.h"]                  = "queue";
    headers["set.h"]                    = "set";
    headers["multiset.h"]               = "set";
    headers["sstream.h"]                = "sstream";
    headers["sstream.tcc"]              = "sstream";
    headers["stack.h"]                  = "stack";
    headers["functexcept.h"]            = "stdexcept";
    headers["stdexcept.h"]              = "stdexcept";
    headers["streambuf.h"]              = "streambuf";
    headers["streambuf.tcc"]            = "streambuf";
    headers["string.h"]                 = "string";
    headers["char_traits.h"]            = "string";
    headers["postypes.h"]               = "string";
    headers["basic_string.h"]           = "string";
    headers["basic_string.tcc"]         = "string";
    headers["tree.h"]                   = "backward/tree.h";
    headers["pair.h"]                   = "utility";
    headers["utility.h"]                = "utility";
    headers["relops.h"]                 = "utility";
    headers["gslice.h"]                 = "valarray";
    headers["gslice_array.h"]           = "valarray";
    headers["indirect_array.h"]         = "valarray";
    headers["mask_array.h"]             = "valarray";
    headers["slice_array.h"]            = "valarray";
    headers["valarray.h"]               = "valarray";
    headers["valarray_after.h"]         = "valarray";
    headers["valarray_before.h"]        = "valarray";
    headers["valarray_array.h"]         = "valarray";
    headers["valarray_array.tcc"]       = "valarray";
    headers["valarray_meta.h"]          = "valarray";
    headers["bvector.h"]                = "vector";
    headers["vector.h"]                 = "vector";
    headers["vector.tcc"]               = "vector";

    //headers["concurrence.h"]             who knows
    //headers["atomicity.h"]               who knows

    // C wrappers -- probably was an easier way to do these, but oh well
    headers["cassert.h"]                = "cassert";
    headers["cctype.h"]                 = "cctype";
    headers["cerrno.h"]                 = "cerrno";
    headers["cfloat.h"]                 = "cfloat";
    headers["climits.h"]                = "climits";
    headers["clocale.h"]                = "clocale";
    headers["cmath.h"]                  = "cmath";
    headers["csetjmp.h"]                = "csetjmp";
    headers["csignal.h"]                = "csignal";
    headers["cstdarg.h"]                = "cstdarg";
    headers["cstddef.h"]                = "cstddef";
    headers["cstdio.h"]                 = "cstdio";
    headers["cstdlib.h"]                = "cstdlib";
    headers["cstring.h"]                = "cstring";
    headers["ctime.h"]                  = "ctime";
    headers["cwchar.h"]                 = "cwchar";
    headers["cwctype.h"]                = "cwctype";
}


void do_word (std::string const& longheader)
{
    std::string::size_type start = 0;

    // if it doesn't contain a "." then it's already a std header
    if (longheader.find(".") == std::string::npos)
    {
        std::cout << longheader << '\n';
        return;
    }

    if (longheader.substr(start,5) == "bits/")  start += 5;
    if ((longheader.substr(start,4) == "stl_") ||
        (longheader.substr(start,4) == "std_"))
    {
        start += 4;
    }

    // come on, gdb, find `p' already...
    const char* p = longheader.substr(start).c_str();
    Map::iterator word = headers.find(p);
    if (word != headers.end())
        std::cout << word->second << '\n';
    else std::cout << "MAYBE_AN_ERROR_MESSAGE_HERE\n";
}


int main (int argc, char**)
{
    if (argc > 1)
    {
        std::cerr << usage;
        exit(0);
    }

    init_map();

    std::string w;
    while (std::cin >> w)
        do_word (w);
}


