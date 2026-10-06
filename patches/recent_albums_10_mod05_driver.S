@ Recent Albums / faster scan: mod05 refresh driver runs the CFW blob.
@ Stock 0x030794a2..0x030794c9: load mod39, walk, RECORD pass, progress(1),
@ load mod40, sort(r4 + 0x2e, *r6). The blob is staged inside mod40, so load
@ mod40 first, copy the blob to RAM, and let cfw_scan run the same sequence.
@ patch 5 030794a2
#include "fw.inc"
#include "syms.inc"
    .equ    resume, 0x030794ca
    movs    r1, #7
    movs    r0, #0x28
    fbl     load_module
    fbl     cfw_stub
    mov     r0, r4              @ library counters (0x0300d164)
    ldr     r1, [r6]            @ DB base sector
    fbl     cfw_scan
    fb     resume