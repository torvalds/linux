SECTIONS
{
        .payload16 0 : {
                guest16 = .;
                guest16.o(.text)
                guest16_end = .;
        }
        .payload64 0 : AT(LOADADDR(.payload16)+SIZEOF(.payload16)) {
                guest64 = .;
                guest64.img.o
                guest64_end = .;
        }
} 