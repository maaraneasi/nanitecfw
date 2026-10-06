@ Shuffle: mod23 media browser key handler (FUN_0307a78e) fetches the key via
@ "bl 0x03063242" at 0x0307a796 (shuffle_21_mod23_call.S). When the browser was
@ opened by the main-menu Shuffle item (title 0x82), restore the All Songs title,
@ put the cursor on a random song and report the select key, so the stock path
@ starts playback (mod24) with shuffle already on.
@ Cave: after recent_setup in the unreferenced 0x03079d78..0x03079ef5.
@ patch 23 03079d98
#include "fw.inc"
    .equ    get_key, 0x03063242
shuffle_key:
    push    {r4, lr}
    fbl     get_key
    ldr     r1, =0x0300e800             @ browser title string id
    ldrh    r2, [r1]
    cmp     r2, #0x82
    bne     1f
    movs    r2, #0x5a                   @ "All Songs"
    strh    r2, [r1]
    ldr     r4, =0x0300ea3c             @ cursor: [0] count, [1] top, [5] level, [6] index
    ldrh    r1, [r4]
    cbz     r1, 1f
    ldr     r2, =0x0300d144             @ 10 ms ticks
    ldr     r0, [r2]
    ldr     r2, =0xe000e018             @ SysTick current value: sub-tick entropy
    ldr     r2, [r2]
    eor     r0, r0, r2
    udiv    r2, r0, r1
    mls     r0, r2, r1, r0
    strh    r0, [r4, #0xc]              @ index = random % count
    ldr     r0, =0x40000040             @ select key
1:  pop     {r4, pc}
    .ltorg
