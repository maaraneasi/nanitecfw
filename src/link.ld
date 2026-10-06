/*
 * CFW blob: C code that runs from free RAM during the media-library refresh.
 *
 * The image is staged inside mod40 (its first 64 KB sort buffer, zero in the
 * stock image and cleared by mod40 before use). mod05's refresh driver loads
 * mod40, calls cfw_stub (copies .text/.data to RAM, clears .bss), then
 * calls cfw_scan. RAM 0x0309b694..0x0309fff0 lies above every module and
 * below the reboot flag at 0x0309fff0; nothing else uses it.
 */
MEMORY
{
    STAGE (rx)  : ORIGIN = 0x0307b674, LENGTH = 0x10000
    RAM   (rwx) : ORIGIN = 0x0309b694, LENGTH = 0x0309fff0 - 0x0309b694
}

ENTRY(cfw_stub)

SECTIONS
{
    .stub : { KEEP(*(.stub)) } > STAGE

    .text : ALIGN(4)
    {
        __blob_start = .;
        *(.text*)
        *(.rodata*)
        *(.data*)
        . = ALIGN(4);
        __blob_end = .;
    } > RAM AT > STAGE
    __blob_load = LOADADDR(.text);

    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) *(.note*) }
}

ASSERT(__bss_end <= 0x0309fff0, "cfw blob does not fit in free RAM")
