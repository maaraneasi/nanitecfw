@ Media Library menu: mod22_start "bl FUN_030794f2" -> menu_restore.
@ patch 22 0307941c
#include "fw.inc"
    .equ    menu_restore, 0x0300e744
    fbl     menu_restore
