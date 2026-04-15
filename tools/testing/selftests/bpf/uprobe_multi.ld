SECTIONS
{
	. = ALIGN(65536);
	.note.gnu.build-id : { *(.note.gnu.build-id) }
	. = ALIGN(65536);
}
INSERT AFTER .text;

build_id_start = ADDR(.note.gnu.build-id);
build_id_end = ADDR(.note.gnu.build-id) + SIZEOF(.note.gnu.build-id);

