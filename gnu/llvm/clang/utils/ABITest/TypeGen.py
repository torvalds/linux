"""Flexible enumeration of C types."""
from __future__ import division, print_function

from Enumeration import *

# TODO:

#  - struct improvements (flexible arrays, packed &
#    unpacked, alignment)
#  - objective-c qualified id
#  - anonymous / transparent unions
#  - VLAs
#  - block types
#  - K&R functions
#  - pass arguments of different types (test extension, transparent union)
#  - varargs

###
# Actual type types


class Type(object):
    def isBitField(self):
        return False

    def isPaddingBitField(self):
        return False

    def getTypeName(self, printer):
        name = "T%d" % len(printer.types)
        typedef = self.getTypedefDef(name, printer)
        printer.addDeclaration(typedef)
        return name


class BuiltinType(Type):
    def __init__(self, name, size, bitFieldSize=None):
        self.name = name
        self.size = size
        self.bitFieldSize = bitFieldSize

    def isBitField(self):
        return self.bitFieldSize is not None

    def isPaddingBitField(self):
        return self.bitFieldSize is 0

    def getBitFieldSize(self):
        assert self.isBitField()
        return self.bitFieldSize

    def getTypeName(self, printer):
        return self.name

    def sizeof(self):
        return self.size

    def __str__(self):
        return self.name


class EnumType(Type):
    unique_id = 0

    def __init__(self, index, enumerators):
        self.index = index
        self.enumerators = enumerators
        self.unique_id = self.__class__.unique_id
        self.__class__.unique_id += 1

    def getEnumerators(self):
        result = ""
        for i, init in enumerate(self.enumerators):
            if i > 0:
                result = result + ", "
            result = result + "enum%dval%d_%d" % (self.index, i, self.unique_id)
            if init:
                result = result + " = %s" % (init)

        return result

    def __str__(self):
        return "enum { %s }" % (self.getEnumerators())

    def getTypedefDef(self, name, printer):
        return "typedef enum %s { %s } %s;" % (name, self.getEnumerators(), name)


class RecordType(Type):
    def __init__(self, index, isUnion, fields):
        self.index = index
        self.isUnion = isUnion
        self.fields = fields
        self.name = None

    def __str__(self):
        def getField(t):
            if t.isBitField():
                return "%s : %d;" % (t, t.getBitFieldSize())
            else:
                return "%s;" % t

        return "%s { %s }" % (
            ("struct", "union")[self.isUnion],
            " ".join(map(getField, self.fields)),
        )

    def getTypedefDef(self, name, printer):
        def getField(it):
            i, t = it
            if t.isBitField():
                if t.isPaddingBitField():
                    return "%s : 0;" % (printer.getTypeName(t),)
                else:
                    return "%s field%d : %d;" % (
                        printer.getTypeName(t),
                        i,
                        t.getBitFieldSize(),
                    )
            else:
                return "%s field%d;" % (printer.getTypeName(t), i)

        fields = [getField(f) for f in enumerate(self.fields)]
        # Name the struct for more readable LLVM IR.
        return "typedef %s %s { %s } %s;" % (
            ("struct", "union")[self.isUnion],
            name,
            " ".join(fields),
            name,
        )


class ArrayType(Type):
    def __init__(self, index, isVector, elementType, size):
        if isVector:
            # Note that for vectors, this is the size in bytes.
            assert size > 0
        else:
            assert size is None or size >= 0
        self.index = index
        self.isVector = isVector
        self.elementType = elementType
        self.size = size
        if isVector:
            eltSize = self.elementType.sizeof()
            assert not (self.size % eltSize)
            self.numElements = self.size // eltSize
        else:
            self.numElements = self.size

    def __str__(self):
        if self.isVector:
            return "vector (%s)[%d]" % (self.elementType, self.size)
        elif self.size is not None:
            return "(%s)[%d]" % (self.elementType, self.size)
        else:
            return "(%s)[]" % (self.elementType,)

    def getTypedefDef(self, name, printer):
        elementName = printer.getTypeName(self.elementType)
        if self.isVector:
            return "typedef %s %s __attribute__ ((vector_size (%d)));" % (
                elementName,
                name,
                self.size,
            )
        else:
            if self.size is None:
                sizeStr = ""
            else:
                sizeStr = str(self.size)
            return "typedef %s %s[%s];" % (elementName, name, sizeStr)


class ComplexType(Type):
    def __init__(self, index, elementType):
        self.index = index
        self.elementType = elementType

    def __str__(self):
        return "_Complex (%s)" % (self.elementType)

    def getTypedefDef(self, name, printer):
        return "typedef _Complex %s %s;" % (printer.getTypeName(self.elementType), name)


class FunctionType(Type):
    def __init__(self, index, returnType, argTypes):
        self.index = index
        self.returnType = returnType
        self.argTypes = argTypes

    def __str__(self):
        if self.returnType is None:
            rt = "void"
        else:
            rt = str(self.returnType)
        if not self.argTypes:
            at = "void"
        else:
            at = ", ".join(map(str, self.argTypes))
        return "%s (*)(%s)" % (rt, at)

    def getTypedefDef(self, name, printer):
        if self.returnType is None:
            rt = "void"
        else:
            rt = str(self.returnType)
        if not self.argTypes:
            at = "void"
        else:
            at = ", ".join(map(str, self.argTypes))
        return "typedef %s (*%s)(%s);" % (rt, name, at)


###
# Type enumerators


class TypeGenerator(object):
    def __init__(self):
        self.cache = {}

    def setCardinality(self):
        abstract

    def get(self, N):
        T = self.cache.get(N)
        if T is None:
            assert 0 <= N < self.cardinality
            T = self.cache[N] = self.generateType(N)
        return T

    def generateType(self, N):
        abstract


class FixedTypeGenerator(TypeGenerator):
    def __init__(self, types):
        TypeGenerator.__init__(self)
        self.types = types
        self.setCardinality()

    def setCardinality(self):
        self.cardinality = len(self.types)

    def generateType(self, N):
        return self.types[N]


# Factorial
def fact(n):
    result = 1
    while n > 0:
        result = result * n
        n = n - 1
    return result


# Compute the number of combinations (n choose k)
def num_combinations(n, k):
    return fact(n) // (fact(k) * fact(n - k))


# Enumerate the combinations choosing k elements from the list of values
def combinations(values, k):
    # From ActiveState Recipe 190465: Generator for permutations,
    # combinations, selections of a sequence
    if k == 0:
        yield []
    else:
        for i in range(len(values) - k + 1):
            for cc in combinations(values[i + 1 :], k - 1):
                yield [values[i]] + cc


class EnumTypeGenerator(TypeGenerator):
    def __init__(self, values, minEnumerators, maxEnumerators):
        TypeGenerator.__init__(self)
        self.values = values
        self.minEnumerators = minEnumerators
        self.maxEnumerators = maxEnumerators
        self.setCardinality()

    def setCardinality(self):
        self.cardinality = 0
        for num in range(self.minEnumerators, self.maxEnumerators + 1):
            self.cardinality += num_combinations(len(self.values), num)

    def generateType(self, n):
        # Figure out the number of enumerators in this type
        numEnumerators = self.minEnumerators
        valuesCovered = 0
        while numEnumerators < self.maxEnumerators:
            comb = num_combinations(len(self.values), numEnumerators)
            if valuesCovered + comb > n:
                break
            numEnumerators = numEnumerators + 1
            valuesCovered += comb

        # Find the requested combination of enumerators and build a
        # type from it.
        i = 0
        for enumerators in combinations(self.values, numEnumerators):
            if i == n - valuesCovered:
                return EnumType(n, enumerators)

            i = i + 1

        assert False


class ComplexTypeGenerator(TypeGenerator):
    def __init__(self, typeGen):
        TypeGenerator.__init__(self)
        self.typeGen = typeGen
        self.setCardinality()

    def setCardinality(self):
        self.cardinality = self.typeGen.cardinality

    def generateType(self, N):
        return ComplexType(N, self.typeGen.get(N))


class VectorTypeGenerator(TypeGenerator):
    def __init__(self, typeGen, sizes):
        TypeGenerator.__init__(self)
        self.typeGen = typeGen
        self.sizes = tuple(map(int, sizes))
        self.setCardinality()

    def setCardinality(self):
        self.cardinality = len(self.sizes) * self.typeGen.cardinality

    def generateType(self, N):
        S, T = getNthPairBounded(N, len(self.sizes), self.typeGen.cardinality)
        return ArrayType(N, True, self.typeGen.get(T), self.sizes[S])


class FixedArrayTypeGenerator(TypeGenerator):
    def __init__(self, typeGen, sizes):
        TypeGenerator.__init__(self)
        self.typeGen = typeGen
        self.sizes = tuple(size)
        self.setCardinality()

    def setCardinality(self):
        self.cardinality = len(self.sizes) * self.typeGen.cardinality

    def generateType(self, N):
        S, T = getNthPairBounded(N, len(self.sizes), self.typeGen.cardinality)
        return ArrayType(N, false, self.typeGen.get(T), self.sizes[S])


class ArrayTypeGenerator(TypeGenerator):
    def __init__(self, typeGen, maxSize, useIncomplete=False, useZero=False):
        TypeGenerator.__init__(self)
        self.typeGen = typeGen
        self.useIncomplete = useIncomplete
        self.useZero = useZero
        self.maxSize = int(maxSize)
        self.W = useIncomplete + useZero + self.maxSize
        self.setCardinality()

    def setCardinality(self):
        self.cardinality = self.W * self.typeGen.cardinality

    def generateType(self, N):
        S, T = getNthPairBounded(N, self.W, self.typeGen.cardinality)
        if self.useIncomplete:
            if S == 0:
                size = None
                S = None
            else:
                S = S - 1
        if S is not None:
            if self.useZero:
                size = S
            else:
                size = S + 1
        return ArrayType(N, False, self.typeGen.get(T), size)


class RecordTypeGenerator(TypeGenerator):
    def __init__(self, typeGen, useUnion, maxSize):
        TypeGenerator.__init__(self)
        self.typeGen = typeGen
        self.useUnion = bool(useUnion)
        self.maxSize = int(maxSize)
        self.setCardinality()

    def setCardinality(self):
        M = 1 + self.useUnion
        if self.maxSize is aleph0:
            S = aleph0 * self.typeGen.cardinality
        else:
            S = 0
            for i in range(self.maxSize + 1):
                S += M * (self.typeGen.cardinality**i)
        self.cardinality = S

    def generateType(self, N):
        isUnion, I = False, N
        if self.useUnion:
            isUnion, I = (I & 1), I >> 1
        fields = [
            self.typeGen.get(f)
            for f in getNthTuple(I, self.maxSize, self.typeGen.cardinality)
        ]
        return RecordType(N, isUnion, fields)


class FunctionTypeGenerator(TypeGenerator):
    def __init__(self, typeGen, useReturn, maxSize):
        TypeGenerator.__init__(self)
        self.typeGen = typeGen
        self.useReturn = useReturn
        self.maxSize = maxSize
        self.setCardinality()

    def setCardinality(self):
        if self.maxSize is aleph0:
            S = aleph0 * self.typeGen.cardinality()
        elif self.useReturn:
            S = 0
            for i in range(1, self.maxSize + 1 + 1):
                S += self.typeGen.cardinality**i
        else:
            S = 0
            for i in range(self.maxSize + 1):
                S += self.typeGen.cardinality**i
        self.cardinality = S

    def generateType(self, N):
        if self.useReturn:
            # Skip the empty tuple
            argIndices = getNthTuple(N + 1, self.maxSize + 1, self.typeGen.cardinality)
            retIndex, argIndices = argIndices[0], argIndices[1:]
            retTy = self.typeGen.get(retIndex)
        else:
            retTy = None
            argIndices = getNthTuple(N, self.maxSize, self.typeGen.cardinality)
        args = [self.typeGen.get(i) for i in argIndices]
        return FunctionType(N, retTy, args)


class AnyTypeGenerator(TypeGenerator):
    def __init__(self):
        TypeGenerator.__init__(self)
        self.generators = []
        self.bounds = []
        self.setCardinality()
        self._cardinality = None

    def getCardinality(self):
        if self._cardinality is None:
            return aleph0
        else:
            return self._cardinality

    def setCardinality(self):
        self.bounds = [g.cardinality for g in self.generators]
        self._cardinality = sum(self.bounds)

    cardinality = property(getCardinality, None)

    def addGenerator(self, g):
        self.generators.append(g)
        for i in range(100):
            prev = self._cardinality
            self._cardinality = None
            for g in self.generators:
                g.setCardinality()
            self.setCardinality()
            if (self._cardinality is aleph0) or prev == self._cardinality:
                break
        else:
            raise RuntimeError("Infinite loop in setting cardinality")

    def generateType(self, N):
        index, M = getNthPairVariableBounds(N, self.bounds)
        return self.generators[index].get(M)


def test():
    fbtg = FixedTypeGenerator(
        [BuiltinType("char", 4), BuiltinType("char", 4, 0), BuiltinType("int", 4, 5)]
    )

    fields1 = AnyTypeGenerator()
    fields1.addGenerator(fbtg)

    fields0 = AnyTypeGenerator()
    fields0.addGenerator(fbtg)
    #    fields0.addGenerator( RecordTypeGenerator(fields1, False, 4) )

    btg = FixedTypeGenerator([BuiltinType("char", 4), BuiltinType("int", 4)])
    etg = EnumTypeGenerator([None, "-1", "1", "1u"], 0, 3)

    atg = AnyTypeGenerator()
    atg.addGenerator(btg)
    atg.addGenerator(RecordTypeGenerator(fields0, False, 4))
    atg.addGenerator(etg)
    print("Cardinality:", atg.cardinality)
    for i in range(100):
        if i == atg.cardinality:
            try:
                atg.get(i)
                raise RuntimeError("Cardinality was wrong")
            except AssertionError:
                break
        print("%4d: %s" % (i, atg.get(i)))


if __name__ == "__main__":
    test()
