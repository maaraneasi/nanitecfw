@ CFW blob entry points that need register-level control.
    .syntax unified
    .thumb

@ void cfw_stub(void) - runs from the staged copy inside mod40; copies the blob
@ to RAM and clears its .bss. Position-independent enough: only literals.
    .section .stub, "ax"
    .global cfw_stub
    .type cfw_stub, %function
cfw_stub:
    ldr     r0, =__blob_load
    ldr     r1, =__blob_start
    ldr     r2, =__blob_end
1:  cmp     r1, r2
    itt     lo
    ldrlo   r3, [r0], #4
    strlo   r3, [r1], #4
    blo     1b
    ldr     r1, =__bss_start
    ldr     r2, =__bss_end
    movs    r3, #0
2:  cmp     r1, r2
    itt     lo
    strlo   r3, [r1], #4
    blo     2b
    bx      lr
    .ltorg

    .text
@ mod39 0x0307aade: bl mod39_mkrec(dst, src, 0) for a folder entry.
@ The walker's 32-byte FAT entry of that folder is at its sp+0x5b8.
    .global cfw_hook_dir
    .type cfw_hook_dir, %function
cfw_hook_dir:
    mov     r3, sp
    b       cfw_dir_record

@ mod39 0x0307b086 / 0x0307ae46: bl mod39_mkrec(dst, src, 1) for a song.
@ The walker keeps the song's record number in r10 (sl).
    .global cfw_hook_file
    .type cfw_hook_file, %function
cfw_hook_file:
    mov     r3, sl
    b       cfw_file_record

@ u32 fread_orig(void *dst, u32 n, u32 h) - the stock file read (mod00
@ 0x0305c3fa) while cfw_fread_hook has replaced its first instruction.
    .global fread_orig
    .type fread_orig, %function
fread_orig:
    push.w  {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, r11, lr}  @ displaced instruction
    ldr     ip, =0x0305c3ff     @ continue after it (thumb)
    bx      ip
    .ltorg

@ src/hooks.txt "wrap" sites: bl prof_thunk instead of the original call.
@ Times the original call without touching sp, so stack arguments still
@ reach it. Not reentrant (wrapped calls never call each other).
    .global prof_thunk
    .type prof_thunk, %function
prof_thunk:
    ldr     ip, =prof_save
    stmia   ip, {r0-r3, lr}
    mov     r0, lr
    bl      prof_begin          @ -> original target | 1
    ldr     ip, =prof_save
    str     r0, [ip, #20]
    ldmia   ip, {r0-r3}
    ldr     ip, [ip, #20]
    blx     ip
    ldr     ip, =prof_save
    strd    r0, r1, [ip, #24]
    bl      prof_end
    ldr     ip, =prof_save
    ldrd    r0, r1, [ip, #24]
    ldr     lr, [ip, #16]
    bx      lr
    .ltorg
