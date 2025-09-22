"""
Objective-C runtime wrapper for use by LLDB Python formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
import lldb
import lldb.formatters.cache
import lldb.formatters.attrib_fromdict
import functools
import lldb.formatters.Logger


class Utilities:
    @staticmethod
    def read_ascii(process, pointer, max_len=128):
        logger = lldb.formatters.Logger.Logger()
        error = lldb.SBError()
        content = None
        try:
            content = process.ReadCStringFromMemory(pointer, max_len, error)
        except:
            pass
        if content is None or len(content) == 0 or error.fail:
            return None
        return content

    @staticmethod
    def is_valid_pointer(pointer, pointer_size, allow_tagged=0, allow_NULL=0):
        logger = lldb.formatters.Logger.Logger()
        if pointer is None:
            return 0
        if pointer == 0:
            return allow_NULL
        if allow_tagged and (pointer % 2) == 1:
            return 1
        return (pointer % pointer_size) == 0

    # Objective-C runtime has a rule that pointers in a class_t will only have bits 0 thru 46 set
    # so if any pointer has bits 47 thru 63 high we know that this is not a
    # valid isa
    @staticmethod
    def is_allowed_pointer(pointer):
        logger = lldb.formatters.Logger.Logger()
        if pointer is None:
            return 0
        return (pointer & 0xFFFF800000000000) == 0

    @staticmethod
    def read_child_of(valobj, offset, type):
        logger = lldb.formatters.Logger.Logger()
        if offset == 0 and type.GetByteSize() == valobj.GetByteSize():
            return valobj.GetValueAsUnsigned()
        child = valobj.CreateChildAtOffset("childUNK", offset, type)
        if child is None or child.IsValid() == 0:
            return None
        return child.GetValueAsUnsigned()

    @staticmethod
    def is_valid_identifier(name):
        logger = lldb.formatters.Logger.Logger()
        if name is None:
            return None
        if len(name) == 0:
            return None
        # technically, the ObjC runtime does not enforce any rules about what name a class can have
        # in practice, the commonly used byte values for a class name are the letters, digits and some
        # symbols: $, %, -, _, .
        # WARNING: this means that you cannot use this runtime implementation if you need to deal
        # with class names that use anything but what is allowed here
        ok_values = dict.fromkeys(
            "$%_.-ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890"
        )
        return all(c in ok_values for c in name)

    @staticmethod
    def check_is_osx_lion(target):
        logger = lldb.formatters.Logger.Logger()
        # assume the only thing that has a Foundation.framework is a Mac
        # assume anything < Lion does not even exist
        try:
            mod = target.module["Foundation"]
        except:
            mod = None
        if mod is None or mod.IsValid() == 0:
            return None
        ver = mod.GetVersion()
        if ver is None or ver == []:
            return None
        return ver[0] < 900

    # a utility method that factors out code common to almost all the formatters
    # takes in an SBValue and a metrics object
    # returns a class_data and a wrapper (or None, if the runtime alone can't
    # decide on a wrapper)
    @staticmethod
    def prepare_class_detection(valobj, statistics):
        logger = lldb.formatters.Logger.Logger()
        class_data = ObjCRuntime(valobj)
        if class_data.is_valid() == 0:
            statistics.metric_hit("invalid_pointer", valobj)
            wrapper = InvalidPointer_Description(valobj.GetValueAsUnsigned(0) == 0)
            return class_data, wrapper
        class_data = class_data.read_class_data()
        if class_data.is_valid() == 0:
            statistics.metric_hit("invalid_isa", valobj)
            wrapper = InvalidISA_Description()
            return class_data, wrapper
        if class_data.is_kvo():
            class_data = class_data.get_superclass()
        if class_data.class_name() == "_NSZombie_OriginalClass":
            wrapper = ThisIsZombie_Description()
            return class_data, wrapper
        return class_data, None


class RoT_Data:
    def __init__(self, rot_pointer, params):
        logger = lldb.formatters.Logger.Logger()
        if Utilities.is_valid_pointer(
            rot_pointer.GetValueAsUnsigned(), params.pointer_size, allow_tagged=0
        ):
            self.sys_params = params
            self.valobj = rot_pointer
            # self.flags = Utilities.read_child_of(self.valobj,0,self.sys_params.uint32_t)
            # self.instanceStart = Utilities.read_child_of(self.valobj,4,self.sys_params.uint32_t)
            self.instanceSize = None  # lazy fetching
            offset = 24 if self.sys_params.is_64_bit else 16
            # self.ivarLayoutPtr = Utilities.read_child_of(self.valobj,offset,self.sys_params.addr_ptr_type)
            self.namePointer = Utilities.read_child_of(
                self.valobj, offset, self.sys_params.types_cache.addr_ptr_type
            )
            self.valid = 1  # self.check_valid()
        else:
            logger >> "Marking as invalid - rot is invalid"
            self.valid = 0
        if self.valid:
            self.name = Utilities.read_ascii(
                self.valobj.GetTarget().GetProcess(), self.namePointer
            )
            if not (Utilities.is_valid_identifier(self.name)):
                logger >> "Marking as invalid - name is invalid"
                self.valid = 0

    # perform sanity checks on the contents of this class_ro_t
    def check_valid(self):
        self.valid = 1
        # misaligned pointers seem to be possible for this field
        # if not(Utilities.is_valid_pointer(self.namePointer,self.sys_params.pointer_size,allow_tagged=0)):
        # 	self.valid = 0
        # 	pass

    def __str__(self):
        logger = lldb.formatters.Logger.Logger()
        return (
            "instanceSize = "
            + hex(self.instance_size())
            + "\n"
            + "namePointer = "
            + hex(self.namePointer)
            + " --> "
            + self.name
        )

    def is_valid(self):
        return self.valid

    def instance_size(self, align=0):
        logger = lldb.formatters.Logger.Logger()
        if self.is_valid() == 0:
            return None
        if self.instanceSize is None:
            self.instanceSize = Utilities.read_child_of(
                self.valobj, 8, self.sys_params.types_cache.uint32_t
            )
        if align:
            unalign = self.instance_size(0)
            if self.sys_params.is_64_bit:
                return ((unalign + 7) & ~7) % 0x100000000
            else:
                return ((unalign + 3) & ~3) % 0x100000000
        else:
            return self.instanceSize


class RwT_Data:
    def __init__(self, rwt_pointer, params):
        logger = lldb.formatters.Logger.Logger()
        if Utilities.is_valid_pointer(
            rwt_pointer.GetValueAsUnsigned(), params.pointer_size, allow_tagged=0
        ):
            self.sys_params = params
            self.valobj = rwt_pointer
            # self.flags = Utilities.read_child_of(self.valobj,0,self.sys_params.uint32_t)
            # self.version = Utilities.read_child_of(self.valobj,4,self.sys_params.uint32_t)
            self.roPointer = Utilities.read_child_of(
                self.valobj, 8, self.sys_params.types_cache.addr_ptr_type
            )
            self.check_valid()
        else:
            logger >> "Marking as invalid - rwt is invald"
            self.valid = 0
        if self.valid:
            self.rot = self.valobj.CreateValueFromData(
                "rot",
                lldb.SBData.CreateDataFromUInt64Array(
                    self.sys_params.endianness,
                    self.sys_params.pointer_size,
                    [self.roPointer],
                ),
                self.sys_params.types_cache.addr_ptr_type,
            )
            # 			self.rot = self.valobj.CreateValueFromAddress("rot",self.roPointer,self.sys_params.types_cache.addr_ptr_type).AddressOf()
            self.data = RoT_Data(self.rot, self.sys_params)

    # perform sanity checks on the contents of this class_rw_t
    def check_valid(self):
        logger = lldb.formatters.Logger.Logger()
        self.valid = 1
        if not (
            Utilities.is_valid_pointer(
                self.roPointer, self.sys_params.pointer_size, allow_tagged=0
            )
        ):
            logger >> "Marking as invalid - ropointer is invalid"
            self.valid = 0

    def __str__(self):
        logger = lldb.formatters.Logger.Logger()
        return "roPointer = " + hex(self.roPointer)

    def is_valid(self):
        logger = lldb.formatters.Logger.Logger()
        if self.valid:
            return self.data.is_valid()
        return 0


class Class_Data_V2:
    def __init__(self, isa_pointer, params):
        logger = lldb.formatters.Logger.Logger()
        if (isa_pointer is not None) and (
            Utilities.is_valid_pointer(
                isa_pointer.GetValueAsUnsigned(), params.pointer_size, allow_tagged=0
            )
        ):
            self.sys_params = params
            self.valobj = isa_pointer
            self.check_valid()
        else:
            logger >> "Marking as invalid - isa is invalid or None"
            self.valid = 0
        if self.valid:
            self.rwt = self.valobj.CreateValueFromData(
                "rwt",
                lldb.SBData.CreateDataFromUInt64Array(
                    self.sys_params.endianness,
                    self.sys_params.pointer_size,
                    [self.dataPointer],
                ),
                self.sys_params.types_cache.addr_ptr_type,
            )
            # 			self.rwt = self.valobj.CreateValueFromAddress("rwt",self.dataPointer,self.sys_params.types_cache.addr_ptr_type).AddressOf()
            self.data = RwT_Data(self.rwt, self.sys_params)

    # perform sanity checks on the contents of this class_t
    # this call tries to minimize the amount of data fetched- as soon as we have "proven"
    # that we have an invalid object, we stop reading
    def check_valid(self):
        logger = lldb.formatters.Logger.Logger()
        self.valid = 1

        self.isaPointer = Utilities.read_child_of(
            self.valobj, 0, self.sys_params.types_cache.addr_ptr_type
        )
        if not (
            Utilities.is_valid_pointer(
                self.isaPointer, self.sys_params.pointer_size, allow_tagged=0
            )
        ):
            logger >> "Marking as invalid - isaPointer is invalid"
            self.valid = 0
            return
        if not (Utilities.is_allowed_pointer(self.isaPointer)):
            logger >> "Marking as invalid - isaPointer is not allowed"
            self.valid = 0
            return

        self.cachePointer = Utilities.read_child_of(
            self.valobj,
            2 * self.sys_params.pointer_size,
            self.sys_params.types_cache.addr_ptr_type,
        )
        if not (
            Utilities.is_valid_pointer(
                self.cachePointer, self.sys_params.pointer_size, allow_tagged=0
            )
        ):
            logger >> "Marking as invalid - cachePointer is invalid"
            self.valid = 0
            return
        if not (Utilities.is_allowed_pointer(self.cachePointer)):
            logger >> "Marking as invalid - cachePointer is not allowed"
            self.valid = 0
            return
        self.dataPointer = Utilities.read_child_of(
            self.valobj,
            4 * self.sys_params.pointer_size,
            self.sys_params.types_cache.addr_ptr_type,
        )
        if not (
            Utilities.is_valid_pointer(
                self.dataPointer, self.sys_params.pointer_size, allow_tagged=0
            )
        ):
            logger >> "Marking as invalid - dataPointer is invalid"
            self.valid = 0
            return
        if not (Utilities.is_allowed_pointer(self.dataPointer)):
            logger >> "Marking as invalid - dataPointer is not allowed"
            self.valid = 0
            return

        self.superclassIsaPointer = Utilities.read_child_of(
            self.valobj,
            1 * self.sys_params.pointer_size,
            self.sys_params.types_cache.addr_ptr_type,
        )
        if not (
            Utilities.is_valid_pointer(
                self.superclassIsaPointer,
                self.sys_params.pointer_size,
                allow_tagged=0,
                allow_NULL=1,
            )
        ):
            logger >> "Marking as invalid - superclassIsa is invalid"
            self.valid = 0
            return
        if not (Utilities.is_allowed_pointer(self.superclassIsaPointer)):
            logger >> "Marking as invalid - superclassIsa is not allowed"
            self.valid = 0
            return

    # in general, KVO is implemented by transparently subclassing
    # however, there could be exceptions where a class does something else
    # internally to implement the feature - this method will have no clue that a class
    # has been KVO'ed unless the standard implementation technique is used
    def is_kvo(self):
        logger = lldb.formatters.Logger.Logger()
        if self.is_valid():
            if self.class_name().startswith("NSKVONotifying_"):
                return 1
        return 0

    # some CF classes have a valid ObjC isa in their CFRuntimeBase
    # but instead of being class-specific this isa points to a match-'em-all class
    # which is __NSCFType (the versions without __ also exists and we are matching to it
    #                      just to be on the safe side)
    def is_cftype(self):
        logger = lldb.formatters.Logger.Logger()
        if self.is_valid():
            return self.class_name() == "__NSCFType" or self.class_name() == "NSCFType"

    def get_superclass(self):
        logger = lldb.formatters.Logger.Logger()
        if self.is_valid():
            parent_isa_pointer = self.valobj.CreateChildAtOffset(
                "parent_isa",
                self.sys_params.pointer_size,
                self.sys_params.addr_ptr_type,
            )
            return Class_Data_V2(parent_isa_pointer, self.sys_params)
        else:
            return None

    def class_name(self):
        logger = lldb.formatters.Logger.Logger()
        if self.is_valid():
            return self.data.data.name
        else:
            return None

    def is_valid(self):
        logger = lldb.formatters.Logger.Logger()
        if self.valid:
            return self.data.is_valid()
        return 0

    def __str__(self):
        logger = lldb.formatters.Logger.Logger()
        return (
            "isaPointer = "
            + hex(self.isaPointer)
            + "\n"
            + "superclassIsaPointer = "
            + hex(self.superclassIsaPointer)
            + "\n"
            + "cachePointer = "
            + hex(self.cachePointer)
            + "\n"
            + "data = "
            + hex(self.dataPointer)
        )

    def is_tagged(self):
        return 0

    def instance_size(self, align=0):
        logger = lldb.formatters.Logger.Logger()
        if self.is_valid() == 0:
            return None
        return self.rwt.rot.instance_size(align)


# runtime v1 is much less intricate than v2 and stores relevant
# information directly in the class_t object


class Class_Data_V1:
    def __init__(self, isa_pointer, params):
        logger = lldb.formatters.Logger.Logger()
        if (isa_pointer is not None) and (
            Utilities.is_valid_pointer(
                isa_pointer.GetValueAsUnsigned(), params.pointer_size, allow_tagged=0
            )
        ):
            self.valid = 1
            self.sys_params = params
            self.valobj = isa_pointer
            self.check_valid()
        else:
            logger >> "Marking as invalid - isaPointer is invalid or None"
            self.valid = 0
        if self.valid:
            self.name = Utilities.read_ascii(
                self.valobj.GetTarget().GetProcess(), self.namePointer
            )
            if not (Utilities.is_valid_identifier(self.name)):
                logger >> "Marking as invalid - name is not valid"
                self.valid = 0

    # perform sanity checks on the contents of this class_t
    def check_valid(self):
        logger = lldb.formatters.Logger.Logger()
        self.valid = 1

        self.isaPointer = Utilities.read_child_of(
            self.valobj, 0, self.sys_params.types_cache.addr_ptr_type
        )
        if not (
            Utilities.is_valid_pointer(
                self.isaPointer, self.sys_params.pointer_size, allow_tagged=0
            )
        ):
            logger >> "Marking as invalid - isaPointer is invalid"
            self.valid = 0
            return

        self.superclassIsaPointer = Utilities.read_child_of(
            self.valobj,
            1 * self.sys_params.pointer_size,
            self.sys_params.types_cache.addr_ptr_type,
        )
        if not (
            Utilities.is_valid_pointer(
                self.superclassIsaPointer,
                self.sys_params.pointer_size,
                allow_tagged=0,
                allow_NULL=1,
            )
        ):
            logger >> "Marking as invalid - superclassIsa is invalid"
            self.valid = 0
            return

        self.namePointer = Utilities.read_child_of(
            self.valobj,
            2 * self.sys_params.pointer_size,
            self.sys_params.types_cache.addr_ptr_type,
        )
        # if not(Utilities.is_valid_pointer(self.namePointer,self.sys_params.pointer_size,allow_tagged=0,allow_NULL=0)):
        # 	self.valid = 0
        # 	return

    # in general, KVO is implemented by transparently subclassing
    # however, there could be exceptions where a class does something else
    # internally to implement the feature - this method will have no clue that a class
    # has been KVO'ed unless the standard implementation technique is used
    def is_kvo(self):
        logger = lldb.formatters.Logger.Logger()
        if self.is_valid():
            if self.class_name().startswith("NSKVONotifying_"):
                return 1
        return 0

    # some CF classes have a valid ObjC isa in their CFRuntimeBase
    # but instead of being class-specific this isa points to a match-'em-all class
    # which is __NSCFType (the versions without __ also exists and we are matching to it
    #                      just to be on the safe side)
    def is_cftype(self):
        logger = lldb.formatters.Logger.Logger()
        if self.is_valid():
            return self.class_name() == "__NSCFType" or self.class_name() == "NSCFType"

    def get_superclass(self):
        logger = lldb.formatters.Logger.Logger()
        if self.is_valid():
            parent_isa_pointer = self.valobj.CreateChildAtOffset(
                "parent_isa",
                self.sys_params.pointer_size,
                self.sys_params.addr_ptr_type,
            )
            return Class_Data_V1(parent_isa_pointer, self.sys_params)
        else:
            return None

    def class_name(self):
        logger = lldb.formatters.Logger.Logger()
        if self.is_valid():
            return self.name
        else:
            return None

    def is_valid(self):
        return self.valid

    def __str__(self):
        logger = lldb.formatters.Logger.Logger()
        return (
            "isaPointer = "
            + hex(self.isaPointer)
            + "\n"
            + "superclassIsaPointer = "
            + hex(self.superclassIsaPointer)
            + "\n"
            + "namePointer = "
            + hex(self.namePointer)
            + " --> "
            + self.name
            + "instanceSize = "
            + hex(self.instanceSize())
            + "\n"
        )

    def is_tagged(self):
        return 0

    def instance_size(self, align=0):
        logger = lldb.formatters.Logger.Logger()
        if self.is_valid() == 0:
            return None
        if self.instanceSize is None:
            self.instanceSize = Utilities.read_child_of(
                self.valobj,
                5 * self.sys_params.pointer_size,
                self.sys_params.types_cache.addr_ptr_type,
            )
        if align:
            unalign = self.instance_size(0)
            if self.sys_params.is_64_bit:
                return ((unalign + 7) & ~7) % 0x100000000
            else:
                return ((unalign + 3) & ~3) % 0x100000000
        else:
            return self.instanceSize


# these are the only tagged pointers values for current versions
# of OSX - they might change in future OS releases, and no-one is
# advised to rely on these values, or any of the bitmasking formulas
# in TaggedClass_Data. doing otherwise is at your own risk
TaggedClass_Values_Lion = {
    1: "NSNumber",
    5: "NSManagedObject",
    6: "NSDate",
    7: "NSDateTS",
}
TaggedClass_Values_NMOS = {
    0: "NSAtom",
    3: "NSNumber",
    4: "NSDateTS",
    5: "NSManagedObject",
    6: "NSDate",
}


class TaggedClass_Data:
    def __init__(self, pointer, params):
        logger = lldb.formatters.Logger.Logger()
        global TaggedClass_Values_Lion, TaggedClass_Values_NMOS
        self.valid = 1
        self.name = None
        self.sys_params = params
        self.valobj = pointer
        self.val = (pointer & ~0x0000000000000000FF) >> 8
        self.class_bits = (pointer & 0xE) >> 1
        self.i_bits = (pointer & 0xF0) >> 4

        if self.sys_params.is_lion:
            if self.class_bits in TaggedClass_Values_Lion:
                self.name = TaggedClass_Values_Lion[self.class_bits]
            else:
                logger >> "Marking as invalid - not a good tagged pointer for Lion"
                self.valid = 0
        else:
            if self.class_bits in TaggedClass_Values_NMOS:
                self.name = TaggedClass_Values_NMOS[self.class_bits]
            else:
                logger >> "Marking as invalid - not a good tagged pointer for NMOS"
                self.valid = 0

    def is_valid(self):
        return self.valid

    def class_name(self):
        logger = lldb.formatters.Logger.Logger()
        if self.is_valid():
            return self.name
        else:
            return 0

    def value(self):
        return self.val if self.is_valid() else None

    def info_bits(self):
        return self.i_bits if self.is_valid() else None

    def is_kvo(self):
        return 0

    def is_cftype(self):
        return 0

    # we would need to go around looking for the superclass or ask the runtime
    # for now, we seem not to require support for this operation so we will merrily
    # pretend to be at a root point in the hierarchy
    def get_superclass(self):
        return None

    # anything that is handled here is tagged
    def is_tagged(self):
        return 1

    # it seems reasonable to say that a tagged pointer is the size of a pointer
    def instance_size(self, align=0):
        logger = lldb.formatters.Logger.Logger()
        if self.is_valid() == 0:
            return None
        return self.sys_params.pointer_size


class InvalidClass_Data:
    def __init__(self):
        pass

    def is_valid(self):
        return 0


class Version:
    def __init__(self, major, minor, release, build_string):
        self._major = major
        self._minor = minor
        self._release = release
        self._build_string = build_string

    def get_major(self):
        return self._major

    def get_minor(self):
        return self._minor

    def get_release(self):
        return self._release

    def get_build_string(self):
        return self._build_string

    major = property(get_major, None)
    minor = property(get_minor, None)
    release = property(get_release, None)
    build_string = property(get_build_string, None)

    def __lt__(self, other):
        if self.major < other.major:
            return 1
        if self.minor < other.minor:
            return 1
        if self.release < other.release:
            return 1
        # build strings are not compared since they are heavily platform-dependent and might not always
        # be available
        return 0

    def __eq__(self, other):
        return (
            (self.major == other.major)
            and (self.minor == other.minor)
            and (self.release == other.release)
            and (self.build_string == other.build_string)
        )

    # Python 2.6 doesn't have functools.total_ordering, so we have to implement
    # other comparators
    def __gt__(self, other):
        return other < self

    def __le__(self, other):
        return not other < self

    def __ge__(self, other):
        return not self < other


runtime_version = lldb.formatters.cache.Cache()
os_version = lldb.formatters.cache.Cache()
types_caches = lldb.formatters.cache.Cache()
isa_caches = lldb.formatters.cache.Cache()


class SystemParameters:
    def __init__(self, valobj):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture(valobj)
        self.adjust_for_process(valobj)

    def adjust_for_process(self, valobj):
        logger = lldb.formatters.Logger.Logger()
        global runtime_version
        global os_version
        global types_caches
        global isa_caches

        process = valobj.GetTarget().GetProcess()
        # using the unique ID for added guarantees (see svn revision 172628 for
        # further details)
        self.pid = process.GetUniqueID()

        if runtime_version.look_for_key(self.pid):
            self.runtime_version = runtime_version.get_value(self.pid)
        else:
            self.runtime_version = ObjCRuntime.runtime_version(process)
            runtime_version.add_item(self.pid, self.runtime_version)

        if os_version.look_for_key(self.pid):
            self.is_lion = os_version.get_value(self.pid)
        else:
            self.is_lion = Utilities.check_is_osx_lion(valobj.GetTarget())
            os_version.add_item(self.pid, self.is_lion)

        if types_caches.look_for_key(self.pid):
            self.types_cache = types_caches.get_value(self.pid)
        else:
            self.types_cache = lldb.formatters.attrib_fromdict.AttributesDictionary(
                allow_reset=0
            )
            self.types_cache.addr_type = valobj.GetType().GetBasicType(
                lldb.eBasicTypeUnsignedLong
            )
            self.types_cache.addr_ptr_type = self.types_cache.addr_type.GetPointerType()
            self.types_cache.uint32_t = valobj.GetType().GetBasicType(
                lldb.eBasicTypeUnsignedInt
            )
            types_caches.add_item(self.pid, self.types_cache)

        if isa_caches.look_for_key(self.pid):
            self.isa_cache = isa_caches.get_value(self.pid)
        else:
            self.isa_cache = lldb.formatters.cache.Cache()
            isa_caches.add_item(self.pid, self.isa_cache)

    def adjust_for_architecture(self, valobj):
        process = valobj.GetTarget().GetProcess()
        self.pointer_size = process.GetAddressByteSize()
        self.is_64_bit = self.pointer_size == 8
        self.endianness = process.GetByteOrder()
        self.is_little = self.endianness == lldb.eByteOrderLittle
        self.cfruntime_size = 16 if self.is_64_bit else 8

    # a simple helper function that makes it more explicit that one is calculating
    # an offset that is made up of X pointers and Y bytes of additional data
    # taking into account pointer size - if you know there is going to be some padding
    # you can pass that in and it will be taken into account (since padding may be different between
    # 32 and 64 bit versions, you can pass padding value for both, the right
    # one will be used)
    def calculate_offset(self, num_pointers=0, bytes_count=0, padding32=0, padding64=0):
        value = bytes_count + num_pointers * self.pointer_size
        return value + padding64 if self.is_64_bit else value + padding32


class ObjCRuntime:
    # the ObjC runtime has no explicit "version" field that we can use
    # instead, we discriminate v1 from v2 by looking for the presence
    # of a well-known section only present in v1
    @staticmethod
    def runtime_version(process):
        logger = lldb.formatters.Logger.Logger()
        if process.IsValid() == 0:
            logger >> "No process - bailing out"
            return None
        target = process.GetTarget()
        num_modules = target.GetNumModules()
        module_objc = None
        for idx in range(num_modules):
            module = target.GetModuleAtIndex(idx)
            if module.GetFileSpec().GetFilename() == "libobjc.A.dylib":
                module_objc = module
                break
        if module_objc is None or module_objc.IsValid() == 0:
            logger >> "no libobjc - bailing out"
            return None
        num_sections = module.GetNumSections()
        section_objc = None
        for idx in range(num_sections):
            section = module.GetSectionAtIndex(idx)
            if section.GetName() == "__OBJC":
                section_objc = section
                break
        if section_objc is not None and section_objc.IsValid():
            logger >> "found __OBJC: v1"
            return 1
        logger >> "no __OBJC: v2"
        return 2

    @staticmethod
    def runtime_from_isa(isa):
        logger = lldb.formatters.Logger.Logger()
        runtime = ObjCRuntime(isa)
        runtime.isa = isa
        return runtime

    def __init__(self, valobj):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.adjust_for_architecture()
        self.sys_params = SystemParameters(self.valobj)
        self.unsigned_value = self.valobj.GetValueAsUnsigned()
        self.isa_value = None

    def adjust_for_architecture(self):
        pass

    # an ObjC pointer can either be tagged or must be aligned
    def is_tagged(self):
        logger = lldb.formatters.Logger.Logger()
        if self.valobj is None:
            return 0
        return Utilities.is_valid_pointer(
            self.unsigned_value, self.sys_params.pointer_size, allow_tagged=1
        ) and not (
            Utilities.is_valid_pointer(
                self.unsigned_value, self.sys_params.pointer_size, allow_tagged=0
            )
        )

    def is_valid(self):
        logger = lldb.formatters.Logger.Logger()
        if self.valobj is None:
            return 0
        if self.valobj.IsInScope() == 0:
            return 0
        return Utilities.is_valid_pointer(
            self.unsigned_value, self.sys_params.pointer_size, allow_tagged=1
        )

    def is_nil(self):
        return self.unsigned_value == 0

    def read_isa(self):
        logger = lldb.formatters.Logger.Logger()
        if self.isa_value is not None:
            logger >> "using cached isa"
            return self.isa_value
        self.isa_pointer = self.valobj.CreateChildAtOffset(
            "cfisa", 0, self.sys_params.types_cache.addr_ptr_type
        )
        if self.isa_pointer is None or self.isa_pointer.IsValid() == 0:
            logger >> "invalid isa - bailing out"
            return None
        self.isa_value = self.isa_pointer.GetValueAsUnsigned(1)
        if self.isa_value == 1:
            logger >> "invalid isa value - bailing out"
            return None
        return Ellipsis

    def read_class_data(self):
        logger = lldb.formatters.Logger.Logger()
        global isa_cache
        if self.is_tagged():
            # tagged pointers only exist in ObjC v2
            if self.sys_params.runtime_version == 2:
                logger >> "on v2 and tagged - maybe"
                # not every odd-valued pointer is actually tagged. most are just plain wrong
                # we could try and predetect this before even creating a TaggedClass_Data object
                # but unless performance requires it, this seems a cleaner way
                # to tackle the task
                tentative_tagged = TaggedClass_Data(
                    self.unsigned_value, self.sys_params
                )
                if tentative_tagged.is_valid():
                    logger >> "truly tagged"
                    return tentative_tagged
                else:
                    logger >> "not tagged - error"
                    return InvalidClass_Data()
            else:
                logger >> "on v1 and tagged - error"
                return InvalidClass_Data()
        if self.is_valid() == 0 or self.read_isa() is None:
            return InvalidClass_Data()
        data = self.sys_params.isa_cache.get_value(self.isa_value, default=None)
        if data is not None:
            return data
        if self.sys_params.runtime_version == 2:
            data = Class_Data_V2(self.isa_pointer, self.sys_params)
        else:
            data = Class_Data_V1(self.isa_pointer, self.sys_params)
        if data is None:
            return InvalidClass_Data()
        if data.is_valid():
            self.sys_params.isa_cache.add_item(self.isa_value, data, ok_to_replace=1)
        return data


# these classes below can be used by the data formatters to provide a
# consistent message that describes a given runtime-generated situation


class SpecialSituation_Description:
    def message(self):
        return ""


class InvalidPointer_Description(SpecialSituation_Description):
    def __init__(self, nil):
        self.is_nil = nil

    def message(self):
        if self.is_nil:
            return '@"<nil>"'
        else:
            return "<invalid pointer>"


class InvalidISA_Description(SpecialSituation_Description):
    def __init__(self):
        pass

    def message(self):
        return "<not an Objective-C object>"


class ThisIsZombie_Description(SpecialSituation_Description):
    def message(self):
        return "<freed object>"
