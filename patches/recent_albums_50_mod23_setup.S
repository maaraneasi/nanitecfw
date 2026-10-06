@ Recent Albums: mod23 browser window init (FUN_03079f1e, r4 = window param
@ {u16 0, u16 title id, ...}) calls the level setup FUN_0307a144 through here.
@ For title 0x87 the album level reads the newest-first group table written by
@ the CFW blob (DB base + 0x52380) instead of base + 0x52160.
@ Code cave: 0x03079d78..0x03079ef5 is an unreferenced function.
@ patch 23 03079d78
#include "fw.inc"
    .equ    setup, 0x0307a144
recent_setup:
    push    {r3, lr}
    fbl     setup
    ldrh    r0, [r4, #2]
    cmp     r0, #0x87
    bne     1f
    ldr     r1, =0x0300e808             @ level descriptor: +0 base, +0x0c.. tables
    ldr     r0, [r1]
    ldr     r2, =0x52380
    add     r0, r2
    str     r0, [r1, #0x10]             @ album groups
1:  pop     {r3, pc}
    .ltorg
