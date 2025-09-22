import lldb


def pyobj_summary(value, unused):
    if value is None or not value.IsValid() or value.GetValueAsUnsigned(0) == 0:
        return "<invalid>"
    refcnt = value.GetChildMemberWithName("ob_refcnt")
    expr = "(char*)PyString_AsString( (PyObject*)PyObject_Str( (PyObject*)0x%x) )" % (
        value.GetValueAsUnsigned(0)
    )
    expr_summary = value.target.EvaluateExpression(
        expr, lldb.SBExpressionOptions()
    ).GetSummary()
    refcnt_value = "rc = %d" % (refcnt.GetValueAsUnsigned(0))
    return "%s (%s)" % (expr_summary, refcnt_value)


def __lldb_init_module(debugger, unused):
    debugger.HandleCommand(
        "type summary add PyObject --python-function pysummary.pyobj_summary"
    )
    debugger.HandleCommand(
        "type summary add lldb_private::PythonObject -s ${var.m_py_obj%S}"
    )
    debugger.HandleCommand(
        "type summary add lldb_private::PythonDictionary -s ${var.m_py_obj%S}"
    )
    debugger.HandleCommand(
        "type summary add lldb_private::PythonString -s ${var.m_py_obj%S}"
    )
