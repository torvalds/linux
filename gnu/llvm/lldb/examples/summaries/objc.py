# Summaries for common ObjC types that require Python scripting
# to be generated fit into this file


def BOOL_SummaryProvider(valobj, dict):
    if not (valobj.IsValid()):
        return "<invalid>"
    if valobj.GetValueAsUnsigned() == 0:
        return "NO"
    else:
        return "YES"


def BOOLRef_SummaryProvider(valobj, dict):
    return BOOL_SummaryProvider(valobj.GetChildAtIndex(0), dict)


def BOOLPtr_SummaryProvider(valobj, dict):
    return BOOL_SummaryProvider(valobj.Dereference(), dict)
