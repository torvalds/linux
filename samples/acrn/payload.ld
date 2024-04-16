/* SPDX-License-Identifier: GPL-2.0 */
SECTIONS
{
        .payload16 0 : {
                guest16 = .;
                guest16.o(.text)
                guest16_end = .;
        }
}
