import sys
import multiprocessing


_current = None
_total = None


def _init(current, total):
    global _current
    global _total
    _current = current
    _total = total


def _wrapped_func(func_and_args):
    func, argument, should_print_progress, filter_ = func_and_args

    if should_print_progress:
        with _current.get_lock():
            _current.value += 1
        sys.stdout.write("\r\t{} of {}".format(_current.value, _total.value))
        sys.stdout.flush()

    return func(argument, filter_)


def pmap(
    func, iterable, processes, should_print_progress, filter_=None, *args, **kwargs
):
    """
    A parallel map function that reports on its progress.

    Applies `func` to every item of `iterable` and return a list of the
    results. If `processes` is greater than one, a process pool is used to run
    the functions in parallel. `should_print_progress` is a boolean value that
    indicates whether a string 'N of M' should be printed to indicate how many
    of the functions have finished being run.
    """
    global _current
    global _total
    _current = multiprocessing.Value("i", 0)
    _total = multiprocessing.Value("i", len(iterable))

    func_and_args = [(func, arg, should_print_progress, filter_) for arg in iterable]
    if processes == 1:
        result = list(map(_wrapped_func, func_and_args, *args, **kwargs))
    else:
        pool = multiprocessing.Pool(
            initializer=_init,
            initargs=(
                _current,
                _total,
            ),
            processes=processes,
        )
        result = pool.map(_wrapped_func, func_and_args, *args, **kwargs)
        pool.close()
        pool.join()

    if should_print_progress:
        sys.stdout.write("\r")
    return result
