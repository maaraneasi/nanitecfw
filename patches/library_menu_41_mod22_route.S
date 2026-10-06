@ Media Library menu: in the mod22 dispatcher, types 7 and 9 fall through to
@ "bne.w done" after "cmp r4, #8"; send them to library_menu
@ (library_menu_40_mod22_dispatch.S) instead.
@ patch 22 03079640
#include "fw.inc"
    .equ    library_menu, 0x03079490
    fbne library_menu