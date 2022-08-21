[bits 32]
[extern x86_start_kernel]

constants:
    STACK_ADDR  equ  0x00f00000                             ; Stack starts at the 16MB address & grows down
    MB_MAGIC    equ  0x1BADB002                             ; 'magic number' lets bootloader find the header
    FLAG_ALIGN  equ  1 << 0                                 ; 4KB alignment for the bootloader
    FLAG_MEM    equ  1 << 1                                 ; provide memory map
    FLAG_VIDEO  equ  0 << 2                                 ; provide video mode information

    MB_FLAGS    equ  FLAG_ALIGN | FLAG_MEM | FLAG_VIDEO
    MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)                  ; checksum of above, to prove we are multiboot

section .multiboot.data
    align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

section .multiboot.text
    global _mos_x86_multiboot_start:function (_mos_x86_multiboot_start.end - _mos_x86_multiboot_start)

_mos_x86_multiboot_start:
    xor ebp, ebp
    mov esp, stack_top
    ; Reset EFLAGS
    push 0
    popf
    push ebx                        ; Push multiboot2 header pointer
    push eax                        ; Push multiboot2 magic value
    call x86_start_kernel           ; start the kernel
    cli
.hang:
    hlt
    jmp .hang
.end:

section .bss
    align 16
stack_bottom:
    resd 4096 ; 16 KiB
stack_top:

