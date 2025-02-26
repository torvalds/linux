# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

import collections
import importlib
import os
import yaml


# To be loaded dynamically as needed
jsonschema = None


class SpecElement:
    """Netlink spec element.

    Abstract element of the Netlink spec. Implements the dictionary interface
    for access to the raw spec. Supports iterative resolution of dependencies
    across elements and class inheritance levels. The elements of the spec
    may refer to each other, and although loops should be very rare, having
    to maintain correct ordering of instantiation is painful, so the resolve()
    method should be used to perform parts of init which require access to
    other parts of the spec.

    Attributes:
        yaml        raw spec as loaded from the spec file
        family      back reference to the full family

        name        name of the entity as listed in the spec (optional)
        ident_name  name which can be safely used as identifier in code (optional)
    """
    def __init__(self, family, yaml):
        self.yaml = yaml
        self.family = family

        if 'name' in self.yaml:
            self.name = self.yaml['name']
            self.ident_name = self.name.replace('-', '_')

        self._super_resolved = False
        family.add_unresolved(self)

    def __getitem__(self, key):
        return self.yaml[key]

    def __contains__(self, key):
        return key in self.yaml

    def get(self, key, default=None):
        return self.yaml.get(key, default)

    def resolve_up(self, up):
        if not self._super_resolved:
            up.resolve()
            self._super_resolved = True

    def resolve(self):
        pass


class SpecEnumEntry(SpecElement):
    """ Entry within an enum declared in the Netlink spec.

    Attributes:
        doc         documentation string
        enum_set    back reference to the enum
        value       numerical value of this enum (use accessors in most situations!)

    Methods:
        raw_value   raw value, i.e. the id in the enum, unlike user value which is a mask for flags
        user_value   user value, same as raw value for enums, for flags it's the mask
    """
    def __init__(self, enum_set, yaml, prev, value_start):
        if isinstance(yaml, str):
            yaml = {'name': yaml}
        super().__init__(enum_set.family, yaml)

        self.doc = yaml.get('doc', '')
        self.enum_set = enum_set

        if 'value' in yaml:
            self.value = yaml['value']
        elif prev:
            self.value = prev.value + 1
        else:
            self.value = value_start

    def has_doc(self):
        return bool(self.doc)

    def raw_value(self):
        return self.value

    def user_value(self, as_flags=None):
        if self.enum_set['type'] == 'flags' or as_flags:
            return 1 << self.value
        else:
            return self.value


class SpecEnumSet(SpecElement):
    """ Enum type

    Represents an enumeration (list of numerical constants)
    as declared in the "definitions" section of the spec.

    Attributes:
        type            enum or flags
        entries         entries by name
        entries_by_val  entries by value
    Methods:
        get_mask      for flags compute the mask of all defined values
    """
    def __init__(self, family, yaml):
        super().__init__(family, yaml)

        self.type = yaml['type']

        prev_entry = None
        value_start = self.yaml.get('value-start', 0)
        self.entries = dict()
        self.entries_by_val = dict()
        for entry in self.yaml['entries']:
            e = self.new_entry(entry, prev_entry, value_start)
            self.entries[e.name] = e
            self.entries_by_val[e.raw_value()] = e
            prev_entry = e

    def new_entry(self, entry, prev_entry, value_start):
        return SpecEnumEntry(self, entry, prev_entry, value_start)

    def has_doc(self):
        if 'doc' in self.yaml:
            return True
        return self.has_entry_doc()

    def has_entry_doc(self):
        for entry in self.entries.values():
            if entry.has_doc():
                return True
        return False

    def get_mask(self, as_flags=None):
        mask = 0
        for e in self.entries.values():
            mask += e.user_value(as_flags)
        return mask


class SpecAttr(SpecElement):
    """ Single Netlink attribute type

    Represents a single attribute type within an attr space.

    Attributes:
        type          string, attribute type
        value         numerical ID when serialized
        attr_set      Attribute Set containing this attr
        is_multi      bool, attr may repeat multiple times
        struct_name   string, name of struct definition
        sub_type      string, name of sub type
        len           integer, optional byte length of binary types
        display_hint  string, hint to help choose format specifier
                      when displaying the value
        sub_message   string, name of sub message type
        selector      string, name of attribute used to select
                      sub-message type

        is_auto_scalar bool, attr is a variable-size scalar
    """
    def __init__(self, family, attr_set, yaml, value):
        super().__init__(family, yaml)

        self.type = yaml['type']
        self.value = value
        self.attr_set = attr_set
        self.is_multi = yaml.get('multi-attr', False)
        self.struct_name = yaml.get('struct')
        self.sub_type = yaml.get('sub-type')
        self.byte_order = yaml.get('byte-order')
        self.len = yaml.get('len')
        self.display_hint = yaml.get('display-hint')
        self.sub_message = yaml.get('sub-message')
        self.selector = yaml.get('selector')

        self.is_auto_scalar = self.type == "sint" or self.type == "uint"


class SpecAttrSet(SpecElement):
    """ Netlink Attribute Set class.

    Represents a ID space of attributes within Netlink.

    Note that unlike other elements, which expose contents of the raw spec
    via the dictionary interface Attribute Set exposes attributes by name.

    Attributes:
        attrs      ordered dict of all attributes (indexed by name)
        attrs_by_val  ordered dict of all attributes (indexed by value)
        subset_of  parent set if this is a subset, otherwise None
    """
    def __init__(self, family, yaml):
        super().__init__(family, yaml)

        self.subset_of = self.yaml.get('subset-of', None)

        self.attrs = collections.OrderedDict()
        self.attrs_by_val = collections.OrderedDict()

        if self.subset_of is None:
            val = 1
            for elem in self.yaml['attributes']:
                if 'value' in elem:
                    val = elem['value']

                attr = self.new_attr(elem, val)
                self.attrs[attr.name] = attr
                self.attrs_by_val[attr.value] = attr
                val += 1
        else:
            real_set = family.attr_sets[self.subset_of]
            for elem in self.yaml['attributes']:
                real_attr = real_set[elem['name']]
                combined_elem = real_attr.yaml | elem
                attr = self.new_attr(combined_elem, real_attr.value)

                self.attrs[attr.name] = attr
                self.attrs_by_val[attr.value] = attr

    def new_attr(self, elem, value):
        return SpecAttr(self.family, self, elem, value)

    def __getitem__(self, key):
        return self.attrs[key]

    def __contains__(self, key):
        return key in self.attrs

    def __iter__(self):
        yield from self.attrs

    def items(self):
        return self.attrs.items()


class SpecStructMember(SpecElement):
    """Struct member attribute

    Represents a single struct member attribute.

    Attributes:
        type        string, type of the member attribute
        byte_order  string or None for native byte order
        enum        string, name of the enum definition
        len         integer, optional byte length of binary types
        display_hint  string, hint to help choose format specifier
                      when displaying the value
        struct      string, name of nested struct type
    """
    def __init__(self, family, yaml):
        super().__init__(family, yaml)
        self.type = yaml['type']
        self.byte_order = yaml.get('byte-order')
        self.enum = yaml.get('enum')
        self.len = yaml.get('len')
        self.display_hint = yaml.get('display-hint')
        self.struct = yaml.get('struct')


class SpecStruct(SpecElement):
    """Netlink struct type

    Represents a C struct definition.

    Attributes:
        members   ordered list of struct members
    """
    def __init__(self, family, yaml):
        super().__init__(family, yaml)

        self.members = []
        for member in yaml.get('members', []):
            self.members.append(self.new_member(family, member))

    def new_member(self, family, elem):
        return SpecStructMember(family, elem)

    def __iter__(self):
        yield from self.members

    def items(self):
        return self.members.items()


class SpecSubMessage(SpecElement):
    """ Netlink sub-message definition

    Represents a set of sub-message formats for polymorphic nlattrs
    that contain type-specific sub messages.

    Attributes:
        name     string, name of sub-message definition
        formats  dict of sub-message formats indexed by match value
    """
    def __init__(self, family, yaml):
        super().__init__(family, yaml)

        self.formats = collections.OrderedDict()
        for elem in self.yaml['formats']:
            format = self.new_format(family, elem)
            self.formats[format.value] = format

    def new_format(self, family, format):
        return SpecSubMessageFormat(family, format)


class SpecSubMessageFormat(SpecElement):
    """ Netlink sub-message format definition

    Represents a single format for a sub-message.

    Attributes:
        value         attribute value to match against type selector
        fixed_header  string, name of fixed header, or None
        attr_set      string, name of attribute set, or None
    """
    def __init__(self, family, yaml):
        super().__init__(family, yaml)

        self.value = yaml.get('value')
        self.fixed_header = yaml.get('fixed-header')
        self.attr_set = yaml.get('attribute-set')


class SpecOperation(SpecElement):
    """Netlink Operation

    Information about a single Netlink operation.

    Attributes:
        value           numerical ID when serialized, None if req/rsp values differ

        req_value       numerical ID when serialized, user -> kernel
        rsp_value       numerical ID when serialized, user <- kernel
        modes           supported operation modes (do, dump, event etc.)
        is_call         bool, whether the operation is a call
        is_async        bool, whether the operation is a notification
        is_resv         bool, whether the operation does not exist (it's just a reserved ID)
        attr_set        attribute set name
        fixed_header    string, optional name of fixed header struct

        yaml            raw spec as loaded from the spec file
    """
    def __init__(self, family, yaml, req_value, rsp_value):
        super().__init__(family, yaml)

        self.value = req_value if req_value == rsp_value else None
        self.req_value = req_value
        self.rsp_value = rsp_value

        self.modes = yaml.keys() & {'do', 'dump', 'event', 'notify'}
        self.is_call = 'do' in yaml or 'dump' in yaml
        self.is_async = 'notify' in yaml or 'event' in yaml
        self.is_resv = not self.is_async and not self.is_call
        self.fixed_header = self.yaml.get('fixed-header', family.fixed_header)

        # Added by resolve:
        self.attr_set = None
        delattr(self, "attr_set")

    def resolve(self):
        self.resolve_up(super())

        if 'attribute-set' in self.yaml:
            attr_set_name = self.yaml['attribute-set']
        elif 'notify' in self.yaml:
            msg = self.family.msgs[self.yaml['notify']]
            attr_set_name = msg['attribute-set']
        elif self.is_resv:
            attr_set_name = ''
        else:
            raise Exception(f"Can't resolve attribute set for op '{self.name}'")
        if attr_set_name:
            self.attr_set = self.family.attr_sets[attr_set_name]


class SpecMcastGroup(SpecElement):
    """Netlink Multicast Group

    Information about a multicast group.

    Value is only used for classic netlink families that use the
    netlink-raw schema. Genetlink families use dynamic ID allocation
    where the ids of multicast groups get resolved at runtime. Value
    will be None for genetlink families.

    Attributes:
        name      name of the mulitcast group
        value     integer id of this multicast group for netlink-raw or None
        yaml      raw spec as loaded from the spec file
    """
    def __init__(self, family, yaml):
        super().__init__(family, yaml)
        self.value = self.yaml.get('value')


class SpecFamily(SpecElement):
    """ Netlink Family Spec class.

    Netlink family information loaded from a spec (e.g. in YAML).
    Takes care of unfolding implicit information which can be skipped
    in the spec itself for brevity.

    The class can be used like a dictionary to access the raw spec
    elements but that's usually a bad idea.

    Attributes:
        proto     protocol type (e.g. genetlink)
        msg_id_model   enum-model for operations (unified, directional etc.)
        license   spec license (loaded from an SPDX tag on the spec)

        attr_sets  dict of attribute sets
        msgs       dict of all messages (index by name)
        sub_msgs   dict of all sub messages (index by name)
        ops        dict of all valid requests / responses
        ntfs       dict of all async events
        consts     dict of all constants/enums
        fixed_header  string, optional name of family default fixed header struct
        mcast_groups  dict of all multicast groups (index by name)
        kernel_family   dict of kernel family attributes
    """
    def __init__(self, spec_path, schema_path=None, exclude_ops=None):
        with open(spec_path, "r") as stream:
            prefix = '# SPDX-License-Identifier: '
            first = stream.readline().strip()
            if not first.startswith(prefix):
                raise Exception('SPDX license tag required in the spec')
            self.license = first[len(prefix):]

            stream.seek(0)
            spec = yaml.safe_load(stream)

        self._resolution_list = []

        super().__init__(self, spec)

        self._exclude_ops = exclude_ops if exclude_ops else []

        self.proto = self.yaml.get('protocol', 'genetlink')
        self.msg_id_model = self.yaml['operations'].get('enum-model', 'unified')

        if schema_path is None:
            schema_path = os.path.dirname(os.path.dirname(spec_path)) + f'/{self.proto}.yaml'
        if schema_path:
            global jsonschema

            with open(schema_path, "r") as stream:
                schema = yaml.safe_load(stream)

            if jsonschema is None:
                jsonschema = importlib.import_module("jsonschema")

            jsonschema.validate(self.yaml, schema)

        self.attr_sets = collections.OrderedDict()
        self.sub_msgs = collections.OrderedDict()
        self.msgs = collections.OrderedDict()
        self.req_by_value = collections.OrderedDict()
        self.rsp_by_value = collections.OrderedDict()
        self.ops = collections.OrderedDict()
        self.ntfs = collections.OrderedDict()
        self.consts = collections.OrderedDict()
        self.mcast_groups = collections.OrderedDict()
        self.kernel_family = collections.OrderedDict(self.yaml.get('kernel-family', {}))

        last_exception = None
        while len(self._resolution_list) > 0:
            resolved = []
            unresolved = self._resolution_list
            self._resolution_list = []

            for elem in unresolved:
                try:
                    elem.resolve()
                except (KeyError, AttributeError) as e:
                    self._resolution_list.append(elem)
                    last_exception = e
                    continue

                resolved.append(elem)

            if len(resolved) == 0:
                raise last_exception

    def new_enum(self, elem):
        return SpecEnumSet(self, elem)

    def new_attr_set(self, elem):
        return SpecAttrSet(self, elem)

    def new_struct(self, elem):
        return SpecStruct(self, elem)

    def new_sub_message(self, elem):
        return SpecSubMessage(self, elem);

    def new_operation(self, elem, req_val, rsp_val):
        return SpecOperation(self, elem, req_val, rsp_val)

    def new_mcast_group(self, elem):
        return SpecMcastGroup(self, elem)

    def add_unresolved(self, elem):
        self._resolution_list.append(elem)

    def _dictify_ops_unified(self):
        self.fixed_header = self.yaml['operations'].get('fixed-header')
        val = 1
        for elem in self.yaml['operations']['list']:
            if 'value' in elem:
                val = elem['value']

            op = self.new_operation(elem, val, val)
            val += 1

            self.msgs[op.name] = op

    def _dictify_ops_directional(self):
        self.fixed_header = self.yaml['operations'].get('fixed-header')
        req_val = rsp_val = 1
        for elem in self.yaml['operations']['list']:
            if 'notify' in elem or 'event' in elem:
                if 'value' in elem:
                    rsp_val = elem['value']
                req_val_next = req_val
                rsp_val_next = rsp_val + 1
                req_val = None
            elif 'do' in elem or 'dump' in elem:
                mode = elem['do'] if 'do' in elem else elem['dump']

                v = mode.get('request', {}).get('value', None)
                if v:
                    req_val = v
                v = mode.get('reply', {}).get('value', None)
                if v:
                    rsp_val = v

                rsp_inc = 1 if 'reply' in mode else 0
                req_val_next = req_val + 1
                rsp_val_next = rsp_val + rsp_inc
            else:
                raise Exception("Can't parse directional ops")

            if req_val == req_val_next:
                req_val = None
            if rsp_val == rsp_val_next:
                rsp_val = None

            skip = False
            for exclude in self._exclude_ops:
                skip |= bool(exclude.match(elem['name']))
            if not skip:
                op = self.new_operation(elem, req_val, rsp_val)

            req_val = req_val_next
            rsp_val = rsp_val_next

            self.msgs[op.name] = op

    def find_operation(self, name):
      """
      For a given operation name, find and return operation spec.
      """
      for op in self.yaml['operations']['list']:
        if name == op['name']:
          return op
      return None

    def resolve(self):
        self.resolve_up(super())

        definitions = self.yaml.get('definitions', [])
        for elem in definitions:
            if elem['type'] == 'enum' or elem['type'] == 'flags':
                self.consts[elem['name']] = self.new_enum(elem)
            elif elem['type'] == 'struct':
                self.consts[elem['name']] = self.new_struct(elem)
            else:
                self.consts[elem['name']] = elem

        for elem in self.yaml['attribute-sets']:
            attr_set = self.new_attr_set(elem)
            self.attr_sets[elem['name']] = attr_set

        for elem in self.yaml.get('sub-messages', []):
            sub_message = self.new_sub_message(elem)
            self.sub_msgs[sub_message.name] = sub_message

        if self.msg_id_model == 'unified':
            self._dictify_ops_unified()
        elif self.msg_id_model == 'directional':
            self._dictify_ops_directional()

        for op in self.msgs.values():
            if op.req_value is not None:
                self.req_by_value[op.req_value] = op
            if op.rsp_value is not None:
                self.rsp_by_value[op.rsp_value] = op
            if not op.is_async and 'attribute-set' in op:
                self.ops[op.name] = op
            elif op.is_async:
                self.ntfs[op.name] = op

        mcgs = self.yaml.get('mcast-groups')
        if mcgs:
            for elem in mcgs['list']:
                mcg = self.new_mcast_group(elem)
                self.mcast_groups[elem['name']] = mcg
