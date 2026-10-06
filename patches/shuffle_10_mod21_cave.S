@ Shuffle: mod21 code cave (FUN_03079502..0x03079639, the never-called
@ "UNICODEXDIC" writer): 6-entry icon table, shuffle icon, shuffle_route.
@ patch 21 03079504
#include "fw.inc"
    .equ    media_library, 0x03079b90   @ dispatcher: strh r0,[MusicTypeSelID]; open window 1
    .equ    open_window,   0x03079b9a   @ dispatcher: bl FUN_0306b26a(r0, r1); return 1
icon_table:                             @ stock 5 (mod00 0x03004124) + shuffle
    .word   0x03072dff, 0x03072daf, 0x03072d5f, 0x03072d87, 0x03072dd7, shuffle_icon - _patch_start + PATCH_BASE
shuffle_icon:                           @ 20x16, 1 bpp, page-major: byte (y>>3)*20+x, bit y&7
    @ ....................
    @ ......########......
    @ .....##########.....
    @ ....###......###....
    @ ....###.............
    @ ....####............
    @ .....#######........
    @ ......#########.....
    @ ........#######.....
    @ ............####....
    @ .............###....
    @ ....###......###....
    @ ....####....####....
    @ .....##########.....
    @ ......########......
    @ ....................
    .byte   0x00,0x00,0x00,0x00,0x38,0x7c,0xfe,0xe6,0xc6,0xc6,0xc6,0xc6,0x86,0x8e,0x8c,0x08,0x00,0x00,0x00,0x00
    .byte   0x00,0x00,0x00,0x00,0x18,0x38,0x78,0x70,0x61,0x61,0x61,0x61,0x73,0x7f,0x3f,0x1e,0x00,0x00,0x00,0x00
    .org    0x40                        @ shuffle_route = 0x03079544 (shuffle_11_mod21_route.S)
@ Replaces "ldr r1,=MusicTypeSelID; movs r0,#0" at 0x03079b8c (shuffle_11_mod21_route.S).
@ Item 5: play mode shuffle, then the media browser on All Songs with title
@ 0x82; mod23's shuffle_key (shuffle_20_mod23_key.S) picks and plays a random song.
shuffle_route:
    ldr     r1, =0x0300d482             @ MusicTypeSelID
    ldr     r2, =0x0300e708             @ main-menu cursor
    ldrh    r2, [r2]
    cmp     r2, #5
    bne     1f
    ldr     r3, =0x0300d164             @ settings / library counters
    ldrh    r0, [r3, #0x30]             @ All Songs count
    cbz     r0, 1f
    movs    r0, #1
    strh    r0, [r1]                    @ MusicTypeSelID = All Songs
    strb    r0, [r3, #0x6f]             @ play order: shuffle
    ldrb    r0, [r3, #0x6d]
    and     r0, r0, #4                  @ keep folder-jump bit
    orr     r0, r0, #3                  @ play mode: shuffle
    strb    r0, [r3, #0x6d]
    movw    r0, #0x113                  @ same messages as Music Settings > Shuffle Play
    fbl     msg_post
    movw    r0, #0x114
    fbl     msg_post
    ldr     r1, =0x0300d592             @ cleared by mod22 before opening the browser
    movs    r0, #0
    strb    r0, [r1]
    strh    r0, [sp]
    movs    r0, #0x82
    strh    r0, [sp, #2]                @ window 2 {0, title 0x82}
    mov     r1, sp
    movs    r0, #2
    fb      open_window
1:  movs    r0, #0
    fb      media_library
    .ltorg

@ The main menu draws a fixed strip of items base-2..base+2 around base = 2
@ (state byte +0xa never changes: the paint loop exits before updating it) and
@ selects by sliding the strip (x = (4 - sel) * 25 + 4). A sixth item is never
@ drawn, so before the selection highlight (bl 0x3050642, both in the slide
@ loop and in the final frame: shuffle_12/13_mod21_strip.S) draw base+3 at x+75.
    .org    0xb0                        @ strip_extra = 0x030795b4
strip_extra:
    push    {r0-r6, lr}
    ldr     r4, =0x03005a00             @ strip state: +0 y, +2 x, +0xa base item
    ldrb    r2, [r4, #0xa]
    adds    r2, r2, #3
    cmp     r2, #6
    bhs     1f
    ldrsh.w r0, [r4, #2]
    adds    r0, r0, #75
    ldrsh.w r1, [r4]
    adr     r3, icon_table
    fbl     0x03050868                  @ draw icon table[r2] at (r0, r1)
1:  pop     {r0-r6, lr}
    fb      0x03050642
    .ltorg
