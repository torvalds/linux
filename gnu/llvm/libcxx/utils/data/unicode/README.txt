Contains various Unicode data files used in the library for Unicode support

To update all files to the last published Unicode version issue the following
command in the directory containing this file.

wget \
    https://www.unicode.org/Public/UCD/latest/ucd/DerivedCoreProperties.txt \
    https://www.unicode.org/Public/UCD/latest/ucd/auxiliary/GraphemeBreakProperty.txt \
    https://www.unicode.org/Public/UCD/latest/ucd/auxiliary/GraphemeBreakTest.txt \
    https://www.unicode.org/Public/UCD/latest/ucd/emoji/emoji-data.txt \
    https://www.unicode.org/Public/UCD/latest/ucd/extracted/DerivedGeneralCategory.txt \
    https://www.unicode.org/Public/UCD/latest/ucd/EastAsianWidth.txt

Afterwards build the `libcxx-generate-files` target to update the generated
Unicode files.

GraphemeBreakProperty.txt
Source: https://www.unicode.org/Public/UCD/latest/ucd/auxiliary/GraphemeBreakProperty.txt
Usage:  libcxx/utils/generate_extended_grapheme_cluster_table.py

emoji-data.txt
Source: https://www.unicode.org/Public/UCD/latest/ucd/emoji/emoji-data.txt
Usage:  libcxx/utils/generate_extended_grapheme_cluster_table.py

GraphemeBreakTest.txt
Source: https://www.unicode.org/Public/UCD/latest/ucd/auxiliary/GraphemeBreakTest.txt
Usage:  libcxx/utils/generate_extended_grapheme_cluster_test.py

DerivedCoreProperties.txt
Source: https://www.unicode.org/Public/UCD/latest/ucd/DerivedCoreProperties.txt
Usage: libcxx/utils/generate_escaped_output_table.py

DerivedGeneralCategory.txt
Source: https://www.unicode.org/Public/UCD/latest/ucd/extracted/DerivedGeneralCategory.txt
Usage: libcxx/utils/generate_escaped_output_table.py

EastAsianWidth.txt
https://www.unicode.org/Public/UCD/latest/ucd/EastAsianWidth.txt
Usage: libcxx/utils/generate_width_estimation_table.py
