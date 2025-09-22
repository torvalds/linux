Use this benchmark command line utility as follows:

  benchmark [-n] <file name> <buffer size> <# iterations>

The command line arguments are:

  -n             ... optional; if supplied, namespace processing is turned on
  <file name>    ... name/path of test xml file
  <buffer size>  ... size of processing buffer;
                     the file is parsed in chunks of this size
  <# iterations> ... how often will the file be parsed

Returns:

  The time (in seconds) it takes to parse the test file,
  averaged over the number of iterations.@
