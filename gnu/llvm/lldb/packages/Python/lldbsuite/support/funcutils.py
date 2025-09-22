import inspect


def requires_self(func):
    func_argc = len(inspect.getfullargspec(func).args)
    if (
        func_argc == 0
        or (getattr(func, "im_self", None) is not None)
        or (hasattr(func, "__self__"))
    ):
        return False
    else:
        return True
