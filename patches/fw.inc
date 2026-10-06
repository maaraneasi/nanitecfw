@ Known firmware symbols (V1.7.0) for assembly patches. Add as you find them.
@
@ ld cannot relocate bl/b.w to absolute symbols (their ARM/Thumb state is
@ unknown), so branches to firmware or blob addresses use the macros below,
@ which encode Thumb-2 branches from the patch address (PATCH_BASE, passed in
@ by tools/asm2patch.sh). Include this file before any code.
    .syntax unified
    .thumb
_patch_start:

    .equ dbg_printf,   0x03071e28   @ printf-style debug log (mod00)
    .equ msg_post,     0x0306b040   @ set event/message flag by id (mod00, inferred)
    .equ msg_test,     0x0306b05c   @ test event flag by id, nonzero if set (inferred)
    .equ load_module,  0x0306b6bc   @ load overlay module (id, 7)

@ bl / b.w (T4): +-16 MB
.macro _t32_b24 target, second
    .set _off, (\target) - (PATCH_BASE + (. - _patch_start) + 4)
    .set _s, (_off >> 24) & 1
    .set _j1, ~(((_off >> 23) & 1) ^ _s) & 1
    .set _j2, ~(((_off >> 22) & 1) ^ _s) & 1
    .short 0xf000 | (_s << 10) | ((_off >> 12) & 0x3ff)
    .short \second | (_j1 << 13) | (_j2 << 11) | ((_off >> 1) & 0x7ff)
.endm
.macro fbl target
    _t32_b24 \target, 0xd000
.endm
.macro fb target
    _t32_b24 \target, 0x9000
.endm

@ b<cond>.w (T3): +-1 MB
.macro _t32_bcc cond, target
    .set _off, (\target) - (PATCH_BASE + (. - _patch_start) + 4)
    .short 0xf000 | (((_off >> 20) & 1) << 10) | (\cond << 6) | ((_off >> 12) & 0x3f)
    .short 0x8000 | (((_off >> 18) & 1) << 13) | (((_off >> 19) & 1) << 11) | ((_off >> 1) & 0x7ff)
.endm
.macro fbeq target
    _t32_bcc 0, \target
.endm
.macro fbne target
    _t32_bcc 1, \target
.endm
