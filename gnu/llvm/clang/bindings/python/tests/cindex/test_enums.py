import unittest

from clang.cindex import (
    TokenKind,
    CursorKind,
    TemplateArgumentKind,
    ExceptionSpecificationKind,
    AvailabilityKind,
    AccessSpecifier,
    TypeKind,
    RefQualifierKind,
    LinkageKind,
    TLSKind,
    StorageClass,
    BinaryOperator,
)


class TestEnums(unittest.TestCase):
    enums = [
        TokenKind,
        CursorKind,
        TemplateArgumentKind,
        ExceptionSpecificationKind,
        AvailabilityKind,
        AccessSpecifier,
        TypeKind,
        RefQualifierKind,
        LinkageKind,
        TLSKind,
        StorageClass,
        BinaryOperator,
    ]

    def test_from_id(self):
        """Check that kinds can be constructed from valid IDs"""
        for enum in self.enums:
            self.assertEqual(enum.from_id(2), enum(2))
            max_value = max([variant.value for variant in enum])
            with self.assertRaises(ValueError):
                enum.from_id(max_value + 1)
            with self.assertRaises(ValueError):
                enum.from_id(-1)

    def test_duplicate_ids(self):
        """Check that no two kinds have the same id"""
        # for enum in self.enums:
        for enum in self.enums:
            num_declared_variants = len(enum._member_map_.keys())
            num_unique_variants = len(list(enum))
            self.assertEqual(num_declared_variants, num_unique_variants)
