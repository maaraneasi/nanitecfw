@ Media Library menu: cursor on return from a list. mod22_start passes the
@ window parameter (the browser posts {MusicTypeSelID}) to FUN_030794f2, which
@ sets MusicTypeSelID and puts the cursor on position = type. With the 10-entry
@ table (library_menu.patch) types and positions differ: call it, then move the
@ cursor to the type's position.
@ Lives in unused mod22 data (reloaded with the module), after the menu table.
@ patch 22 0300e744
#include "fw.inc"
    .equ    restore, 0x030794f2
    .equ    NITEMS,  10
menu_restore:
    push    {r0, lr}
    fbl     restore
    pop     {r0}
    ldrh    r0, [r0]                    @ type
    cmp     r0, #10
    bhi     1f
    adr     r1, pos
    ldrb    r0, [r1, r0]
    ldr     r1, =0x0300e8a8             @ menu state: [1] top, [2] row, [4] rows shown, [5] cursor
    strb    r0, [r1, #5]
    bic     r2, r0, #1                  @ two rows per page
    strb    r2, [r1, #1]
    subs    r0, r0, r2
    strb    r0, [r1, #2]
    rsb     r0, r2, #NITEMS
    cmp     r0, #2
    it      gt
    movgt   r0, #2
    strb    r0, [r1, #4]
1:  pop     {pc}
    .align  2
pos:                                    @ position of type 0..10
    .byte   0, 1, 2, 4, 5, 6, 9, 8, 0, 3, 7
    .align  2
    .ltorg
