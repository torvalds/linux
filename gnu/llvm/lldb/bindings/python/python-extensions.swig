%extend lldb::SBBreakpoint {
    %pythoncode %{
        def __eq__(self, rhs):
            if not isinstance(rhs, type(self)):
                return False

            return getattr(_lldb,self.__class__.__name__+"___eq__")(self, rhs)

        def __ne__(self, rhs):
            if not isinstance(rhs, type(self)):
                return True

            return getattr(_lldb,self.__class__.__name__+"___ne__")(self, rhs)
    %}
}

%extend lldb::SBBroadcaster {
    %pythoncode %{
        def __eq__(self, rhs):
            if not isinstance(rhs, type(self)):
                return False

            return getattr(_lldb,self.__class__.__name__+"___eq__")(self, rhs)

        def __ne__(self, rhs):
            if not isinstance(rhs, type(self)):
                return True

            return getattr(_lldb,self.__class__.__name__+"___ne__")(self, rhs)
    %}
}

%extend lldb::SBCommandReturnObject {
        /* the write() and flush() calls are not part of the SB API proper, and are solely for Python usage
        they are meant to make an SBCommandReturnObject into a file-like object so that instructions of the sort
        print >>sb_command_return_object, "something"
        will work correctly */

        void lldb::SBCommandReturnObject::write (const char* str)
        {
            if (str)
                $self->Printf("%s",str);
        }
        void lldb::SBCommandReturnObject::flush ()
        {}
}

%extend lldb::SBCompileUnit {
    %pythoncode %{
        def __eq__(self, rhs):
            if not isinstance(rhs, type(self)):
                return False

            return getattr(_lldb,self.__class__.__name__+"___eq__")(self, rhs)

        def __ne__(self, rhs):
            if not isinstance(rhs, type(self)):
                return True

            return getattr(_lldb,self.__class__.__name__+"___ne__")(self, rhs)
    %}
}

%extend lldb::SBDeclaration {
    %pythoncode %{
        def __eq__(self, rhs):
            if not isinstance(rhs, type(self)):
                return False

            return getattr(_lldb,self.__class__.__name__+"___eq__")(self, rhs)

        def __ne__(self, rhs):
            if not isinstance(rhs, type(self)):
                return True

            return getattr(_lldb,self.__class__.__name__+"___ne__")(self, rhs)
    %}
}

%extend lldb::SBFunction {
    %pythoncode %{
        def __eq__(self, rhs):
            if not isinstance(rhs, type(self)):
                return False

            return getattr(_lldb,self.__class__.__name__+"___eq__")(self, rhs)

        def __ne__(self, rhs):
            if not isinstance(rhs, type(self)):
                return True

            return getattr(_lldb,self.__class__.__name__+"___ne__")(self, rhs)
    %}
}

%extend lldb::SBLineEntry {
    %pythoncode %{
        def __eq__(self, rhs):
            if not isinstance(rhs, type(self)):
                return False

            return getattr(_lldb,self.__class__.__name__+"___eq__")(self, rhs)

        def __ne__(self, rhs):
            if not isinstance(rhs, type(self)):
                return True

            return getattr(_lldb,self.__class__.__name__+"___ne__")(self, rhs)
    %}
}

%extend lldb::SBModule {
    %pythoncode %{
        def __eq__(self, rhs):
            if not isinstance(rhs, type(self)):
                return False

            return getattr(_lldb,self.__class__.__name__+"___eq__")(self, rhs)

        def __ne__(self, rhs):
            if not isinstance(rhs, type(self)):
                return True

            return getattr(_lldb,self.__class__.__name__+"___ne__")(self, rhs)
    %}
}

%extend lldb::SBSection {
    %pythoncode %{
        def __eq__(self, rhs):
            if not isinstance(rhs, type(self)):
                return False

            return getattr(_lldb,self.__class__.__name__+"___eq__")(self, rhs)

        def __ne__(self, rhs):
            if not isinstance(rhs, type(self)):
                return True

            return getattr(_lldb,self.__class__.__name__+"___ne__")(self, rhs)
    %}
}
%extend lldb::SBStream {
        /* the write() and flush() calls are not part of the SB API proper, and are solely for Python usage
        they are meant to make an SBStream into a file-like object so that instructions of the sort
        print >>sb_stream, "something"
        will work correctly */

        void lldb::SBStream::write (const char* str)
        {
            if (str)
                $self->Printf("%s",str);
        }
        void lldb::SBStream::flush ()
        {}
}
%extend lldb::SBSymbol {
    %pythoncode %{
        def __eq__(self, rhs):
            if not isinstance(rhs, type(self)):
                return False

            return getattr(_lldb,self.__class__.__name__+"___eq__")(self, rhs)

        def __ne__(self, rhs):
            if not isinstance(rhs, type(self)):
                return True

            return getattr(_lldb,self.__class__.__name__+"___ne__")(self, rhs)
    %}
}

%extend lldb::SBTarget {
    %pythoncode %{
        def __eq__(self, rhs):
            if not isinstance(rhs, type(self)):
                return False

            return getattr(_lldb,self.__class__.__name__+"___eq__")(self, rhs)

        def __ne__(self, rhs):
            if not isinstance(rhs, type(self)):
                return True

            return getattr(_lldb,self.__class__.__name__+"___ne__")(self, rhs)
    %}
}

%extend lldb::SBTypeFilter {
    %pythoncode %{
        def __eq__(self, rhs):
            if not isinstance(rhs, type(self)):
                return False

            return getattr(_lldb,self.__class__.__name__+"___eq__")(self, rhs)

        def __ne__(self, rhs):
            if not isinstance(rhs, type(self)):
                return True

            return getattr(_lldb,self.__class__.__name__+"___ne__")(self, rhs)
    %}
}

%extend lldb::SBTypeNameSpecifier {
    %pythoncode %{
        def __eq__(self, rhs):
            if not isinstance(rhs, type(self)):
                return False

            return getattr(_lldb,self.__class__.__name__+"___eq__")(self, rhs)

        def __ne__(self, rhs):
            if not isinstance(rhs, type(self)):
                return True

            return getattr(_lldb,self.__class__.__name__+"___ne__")(self, rhs)
    %}
}

%extend lldb::SBTypeSummary {
    %pythoncode %{
        def __eq__(self, rhs):
            if not isinstance(rhs, type(self)):
                return False

            return getattr(_lldb,self.__class__.__name__+"___eq__")(self, rhs)

        def __ne__(self, rhs):
            if not isinstance(rhs, type(self)):
                return True

            return getattr(_lldb,self.__class__.__name__+"___ne__")(self, rhs)
    %}
}

%extend lldb::SBTypeSynthetic {
    %pythoncode %{
        def __eq__(self, rhs):
            if not isinstance(rhs, type(self)):
                return False

            return getattr(_lldb,self.__class__.__name__+"___eq__")(self, rhs)

        def __ne__(self, rhs):
            if not isinstance(rhs, type(self)):
                return True

            return getattr(_lldb,self.__class__.__name__+"___ne__")(self, rhs)
    %}
}

%extend lldb::SBThread {
    %pythoncode %{
        def __eq__(self, rhs):
            if not isinstance(rhs, type(self)):
                return False

            return getattr(_lldb,self.__class__.__name__+"___eq__")(self, rhs)

        def __ne__(self, rhs):
            if not isinstance(rhs, type(self)):
                return True

            return getattr(_lldb,self.__class__.__name__+"___ne__")(self, rhs)
    %}
}

%pythoncode %{

def command(command_name=None, doc=None):
    import lldb
    """A decorator function that registers an LLDB command line
        command that is bound to the function it is attached to."""
    def callable(function):
        """Registers an lldb command for the decorated function."""
        command = "command script add -f %s.%s %s" % (function.__module__, function.__name__, command_name or function.__name__)
        lldb.debugger.HandleCommand(command)
        if doc:
            function.__doc__ = doc
        return function

    return callable

class declaration(object):
    '''A class that represents a source declaration location with file, line and column.'''
    def __init__(self, file, line, col):
        self.file = file
        self.line = line
        self.col = col

class value_iter(object):
    '''Allows iterating over the children of an :py:class:`SBValue`.'''
    def __iter__(self):
        return self

    def __next__(self):
        if self.index >= self.length:
            raise StopIteration()
        child_sbvalue = self.sbvalue.GetChildAtIndex(self.index)
        self.index += 1
        return value(child_sbvalue)

    def next(self):
        return self.__next__()

    def __eq__(self, other):
        return not self.__ne__(other)

    def __len__(self):
        return self.length

    def __init__(self,value):
        self.index = 0
        self.length = 0
        self.sbvalue = value
        if type(self.sbvalue) is value:
            self.sbvalue = self.sbvalue.sbvalue
        self.length = self.sbvalue.GetNumChildren()

class value(object):
    '''Wraps :py:class:`SBValue` objects so the resulting object can be used as a variable would be in code.

    So if you have a Point structure variable in your code in the current frame named "pt",
    you can initialize an instance of this class with it: ::

        pt = lldb.value(lldb.frame.FindVariable("pt"))
        print pt
        print pt.x
        print pt.y

        pt = lldb.value(lldb.frame.FindVariable("rectangle_array"))
        print rectangle_array[12]
        print rectangle_array[5].origin.x'''
    def __init__(self, sbvalue):
        self.sbvalue = sbvalue

    def __nonzero__(self):
        return self.sbvalue.__nonzero__()

    def __bool__(self):
        return self.sbvalue.__bool__()

    def __str__(self):
        return self.sbvalue.__str__()

    def __getitem__(self, key):
        # Allow array access if this value has children...
        if type(key) is value:
            key = int(key)
        if type(key) is int:
            child_sbvalue = (self.sbvalue.GetValueForExpressionPath("[%i]" % key))
            if child_sbvalue and child_sbvalue.IsValid():
                return value(child_sbvalue)
            raise IndexError("Index '%d' is out of range" % key)
        raise TypeError("No array item of type %s" % str(type(key)))

    def __iter__(self):
        return value_iter(self.sbvalue)

    def __getattr__(self, name):
        child_sbvalue = self.sbvalue.GetChildMemberWithName (name)
        if child_sbvalue and child_sbvalue.IsValid():
            return value(child_sbvalue)
        raise AttributeError("Attribute '%s' is not defined" % name)

    def __add__(self, other):
        return int(self) + int(other)

    def __sub__(self, other):
        return int(self) - int(other)

    def __mul__(self, other):
        return int(self) * int(other)

    def __floordiv__(self, other):
        return int(self) // int(other)

    def __mod__(self, other):
        return int(self) % int(other)

    def __divmod__(self, other):
        return int(self) % int(other)

    def __pow__(self, other):
        return int(self) ** int(other)

    def __lshift__(self, other):
        return int(self) << int(other)

    def __rshift__(self, other):
        return int(self) >> int(other)

    def __and__(self, other):
        return int(self) & int(other)

    def __xor__(self, other):
        return int(self) ^ int(other)

    def __or__(self, other):
        return int(self) | int(other)

    def __div__(self, other):
        return int(self) / int(other)

    def __truediv__(self, other):
        return int(self) / int(other)

    def __iadd__(self, other):
        result = self.__add__(other)
        self.sbvalue.SetValueFromCString (str(result))
        return result

    def __isub__(self, other):
        result = self.__sub__(other)
        self.sbvalue.SetValueFromCString (str(result))
        return result

    def __imul__(self, other):
        result = self.__mul__(other)
        self.sbvalue.SetValueFromCString (str(result))
        return result

    def __idiv__(self, other):
        result = self.__div__(other)
        self.sbvalue.SetValueFromCString (str(result))
        return result

    def __itruediv__(self, other):
        result = self.__truediv__(other)
        self.sbvalue.SetValueFromCString (str(result))
        return result

    def __ifloordiv__(self, other):
        result =  self.__floordiv__(self, other)
        self.sbvalue.SetValueFromCString (str(result))
        return result

    def __imod__(self, other):
        result =  self.__and__(self, other)
        self.sbvalue.SetValueFromCString (str(result))
        return result

    def __ipow__(self, other):
        result = self.__pow__(self, other)
        self.sbvalue.SetValueFromCString (str(result))
        return result

    def __ipow__(self, other, modulo):
        result = self.__pow__(self, other, modulo)
        self.sbvalue.SetValueFromCString (str(result))
        return result

    def __ilshift__(self, other):
        result = self.__lshift__(other)
        self.sbvalue.SetValueFromCString (str(result))
        return result

    def __irshift__(self, other):
        result =  self.__rshift__(other)
        self.sbvalue.SetValueFromCString (str(result))
        return result

    def __iand__(self, other):
        result =  self.__and__(self, other)
        self.sbvalue.SetValueFromCString (str(result))
        return result

    def __ixor__(self, other):
        result =  self.__xor__(self, other)
        self.sbvalue.SetValueFromCString (str(result))
        return result

    def __ior__(self, other):
        result =  self.__ior__(self, other)
        self.sbvalue.SetValueFromCString (str(result))
        return result

    def __neg__(self):
        return -int(self)

    def __pos__(self):
        return +int(self)

    def __abs__(self):
        return abs(int(self))

    def __invert__(self):
        return ~int(self)

    def __complex__(self):
        return complex (int(self))

    def __int__(self):
        is_num,is_sign = is_numeric_type(self.sbvalue.GetType().GetCanonicalType().GetBasicType())
        if is_num and not is_sign: return self.sbvalue.GetValueAsUnsigned()
        return self.sbvalue.GetValueAsSigned()

    def __long__(self):
        return self.__int__()

    def __float__(self):
        return float (self.sbvalue.GetValueAsSigned())

    def __oct__(self):
        return '0%o' % self.sbvalue.GetValueAsUnsigned()

    def __hex__(self):
        return '0x%x' % self.sbvalue.GetValueAsUnsigned()

    def __len__(self):
        return self.sbvalue.GetNumChildren()

    def __eq__(self, other):
        if type(other) is int:
                return int(self) == other
        elif type(other) is str:
                return str(self) == other
        elif type(other) is value:
                self_err = SBError()
                other_err = SBError()
                self_val = self.sbvalue.GetValueAsUnsigned(self_err)
                if self_err.fail:
                        raise ValueError("unable to extract value of self")
                other_val = other.sbvalue.GetValueAsUnsigned(other_err)
                if other_err.fail:
                        raise ValueError("unable to extract value of other")
                return self_val == other_val
        raise TypeError("Unknown type %s, No equality operation defined." % str(type(other)))

    def __ne__(self, other):
        return not self.__eq__(other)
%}

%pythoncode %{

class SBSyntheticValueProvider(object):
    def __init__(self,valobj):
        pass

    def num_children(self):
        return 0

    def get_child_index(self,name):
        return None

    def get_child_at_index(self,idx):
        return None

    def update(self):
        pass

    def has_children(self):
        return False

    def __len__(self):
      return self.num_children()

    def __iter__(self):
      '''Iterate over all children in a lldb.SBSyntheticValueProvider object.'''
      return lldb_iter(self, 'num_children', 'get_child_at_index')

%}

%pythoncode %{

# given an lldb.SBBasicType it returns a tuple
# (is_numeric, is_signed)
# the value of is_signed is undefined if is_numeric == false
def is_numeric_type(basic_type):
    if basic_type == eBasicTypeInvalid: return (False,False)
    if basic_type == eBasicTypeVoid: return (False,False)
    if basic_type == eBasicTypeChar: return (True,False)
    if basic_type == eBasicTypeSignedChar: return (True,True)
    if basic_type == eBasicTypeUnsignedChar: return (True,False)
    if basic_type == eBasicTypeWChar: return (True,False)
    if basic_type == eBasicTypeSignedWChar: return (True,True)
    if basic_type == eBasicTypeUnsignedWChar: return (True,False)
    if basic_type == eBasicTypeChar16: return (True,False)
    if basic_type == eBasicTypeChar32: return (True,False)
    if basic_type == eBasicTypeChar8: return (True,False)
    if basic_type == eBasicTypeShort: return (True,True)
    if basic_type == eBasicTypeUnsignedShort: return (True,False)
    if basic_type == eBasicTypeInt: return (True,True)
    if basic_type == eBasicTypeUnsignedInt: return (True,False)
    if basic_type == eBasicTypeLong: return (True,True)
    if basic_type == eBasicTypeUnsignedLong: return (True,False)
    if basic_type == eBasicTypeLongLong: return (True,True)
    if basic_type == eBasicTypeUnsignedLongLong: return (True,False)
    if basic_type == eBasicTypeInt128: return (True,True)
    if basic_type == eBasicTypeUnsignedInt128: return (True,False)
    if basic_type == eBasicTypeBool: return (False,False)
    if basic_type == eBasicTypeHalf: return (True,True)
    if basic_type == eBasicTypeFloat: return (True,True)
    if basic_type == eBasicTypeDouble: return (True,True)
    if basic_type == eBasicTypeLongDouble: return (True,True)
    if basic_type == eBasicTypeFloatComplex: return (True,True)
    if basic_type == eBasicTypeDoubleComplex: return (True,True)
    if basic_type == eBasicTypeLongDoubleComplex: return (True,True)
    if basic_type == eBasicTypeObjCID: return (False,False)
    if basic_type == eBasicTypeObjCClass: return (False,False)
    if basic_type == eBasicTypeObjCSel: return (False,False)
    if basic_type == eBasicTypeNullPtr: return (False,False)
    #if basic_type == eBasicTypeOther:
    return (False,False)

%}
