@ Media Library menu: dispatcher (FUN_0307957c) cases for the cfw types.
@   7 Recent Albums: MusicTypeSelID 4 (Albums), title 0x87
@   9 Artists:       MusicTypeSelID 3 (Artists), title 0x85
@ mod23 recognises the lists by their title (recent_albums_50, track_artists_50).
@ Same as the stock path at 0x030796ac but with a fixed title; the stock path
@ takes it from the title table, indexed by type.
@ Code cave: 0x03079490..0x030794f1 is an unreferenced function.
@ Registers as at 0x03079640: r4 type, r5 0, r7 0x0300d164, r8 &MusicTypeSelID.
@ patch 22 03079490
#include "fw.inc"
    .equ    done,         0x030798b4    @ dispatcher exit
    .equ    empty,        0x030796dc    @ stock path, empty library
    .equ    open_browser, 0x030796ce    @ stock path: post window 2 {0, title} (r1 = sp)
library_menu:
    cmp     r4, #7
    beq     1f
    cmp     r4, #9
    fbne done
    movs    r0, #3
    movs    r1, #0x85
    b       2f
1:  movs    r0, #4
    movs    r1, #0x87
2:  strh.w  r0, [r8]
    ldrh    r0, [r7, #0x2e]             @ song count
    cmp     r0, #0
    fbeq empty
    strh.w  r5, [sp]
    strh.w  r1, [sp, #2]
    mov     r1, sp
    fb     open_browser
