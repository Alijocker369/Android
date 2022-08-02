; SPDX-License-Identifier: GPL-3.0-or-later

%define GDT_SEGMENT_KDATA 0x10 ; as in x86.h
%define IRQ_BASE          0x20 ; as in x86.h

%define ISR_MAX_COUNT     32
%define IRQ_MAX_COUNT     16

extern x86_handle_interrupt

global irq_stub_table
global isr_stub_table

; ! When the CPU calls the interrupt handlers, the CPU pushes these values onto the stack in this order:
; ! EFLAGS -> CS -> EIP
; ! If the gate type is not a trap gate, the CPU will clear the interrupt flag.

; handler for a ISR with its error code (already pushed onto the stack)
%macro ISR_handler_ec 1
isr_stub_%+%1:
    nop                   ; ! If the interrupt is an exception, the CPU will push an error code onto the stack, as a doubleword.
    push %1               ; interrupt number
    jmp  ISR_handler_impl
%endmacro

; handler for a ISR
%macro ISR_handler 1
isr_stub_%+%1:
    push 0                ; error code (not used)
    push %1               ; interrupt number
    jmp  ISR_handler_impl
%endmacro

; handler for an IRQ
%macro IRQ_handler 1
irq_stub_%1:
    push 0                ; error code (not used)
    push %1 + IRQ_BASE    ; IRQ number
    jmp  ISR_handler_impl
%endmacro

ISR_handler     0 ; Divide-by-zero Error
ISR_handler     1 ; Debug
ISR_handler     2 ; NMI (Non-maskable Interrupt)
ISR_handler     3 ; Breakpoint
ISR_handler     4 ; Overflow
ISR_handler     5 ; Bounds Range Exceeded
ISR_handler     6 ; Invalid Opcode
ISR_handler     7 ; Device Not Available
ISR_handler_ec  8 ; Double Fault
ISR_handler     9 ; Coprocessor Segment Overrun (Since the 486 this is handled by a GPF instead like it already did with non-FPU memory accesses)
ISR_handler_ec 10 ; Invalid TSS
ISR_handler_ec 11 ; Segment Not Present
ISR_handler_ec 12 ; Stack-Segment Fault
ISR_handler_ec 13 ; General Protection Fault
ISR_handler_ec 14 ; Page Fault
ISR_handler    15 ; Reserved
ISR_handler    16 ; x87 FPU Floating-Point Error
ISR_handler_ec 17 ; Alignment Check
ISR_handler    18 ; Machine Check
ISR_handler    19 ; SIMD Floating-Point Exception
ISR_handler    20 ; Virtualization Exception
ISR_handler    21 ; Control Protection Exception
ISR_handler    22 ; Reserved
ISR_handler    23 ; Reserved
ISR_handler    24 ; Reserved
ISR_handler    25 ; Reserved
ISR_handler    26 ; Reserved
ISR_handler    27 ; Reserved
ISR_handler    28 ; Hypervisor Injection Exception
ISR_handler    29 ; VMM Communication Exception
ISR_handler_ec 30 ; Security Exception
ISR_handler    31 ; Reserved

IRQ_handler     0 ; Programmable Interrupt Timer Interrupt
IRQ_handler     1 ; Keyboard Interrupt
IRQ_handler     2 ; Cascade (used internally by the two PICs. never raised)
IRQ_handler     3 ; COM2
IRQ_handler     4 ; COM1
IRQ_handler     5 ; LPT2
IRQ_handler     6 ; Floppy Disk
IRQ_handler     7 ; LPT1 (probably spurious)
IRQ_handler     8 ; CMOS RTC
IRQ_handler     9 ; Free for peripherals / legacy SCSI / NIC
IRQ_handler    10 ; Free for peripherals / SCSI / NIC
IRQ_handler    11 ; Free for peripherals / SCSI / NIC
IRQ_handler    12 ; PS2 Mouse
IRQ_handler    13 ; FPU / Coprocessor / Inter-processor
IRQ_handler    14 ; Primary ATA Hard Disk
IRQ_handler    15 ; Secondary ATA Hard Disk

isr_stub_table:
    %assign i 0
    %rep ISR_MAX_COUNT
        dd isr_stub_%+i ; use DQ instead if targeting 64-bit
    %assign i i+1
    %endrep

irq_stub_table:
    %assign i 0
    %rep IRQ_MAX_COUNT
        dd irq_stub_%+i
    %assign i i+1
    %endrep

ISR_handler_impl:
    ; save all registers according to the `stack_frame` structure in x86.h
    push gs
    push fs
    push es
    push ds
    pushad

    ; Clears the DF flag in the EFLAGS register.
    ; When the DF flag is set to 0, string operations increment the index registers (ESI and/or EDI).
    cld

    ; * Since we are using a flat memory model, we don't have to worry about segmentation.
    ; mov ax, GDT_SEGMENT_KDATA
    ; mov ds, ax
    ; mov es, ax
    ; mov fs, ax
    ; mov gs, ax

    ; x86_handle_interrupt(u32 esp)
    mov  eax, esp
    push eax
    call x86_handle_interrupt

    ; remove the pushed esp parameter
    add  esp, 4

    popad
    pop ds
    pop es
    pop fs
    pop gs

    ; remove the pushed error code and interrupt (or IRQ) number
    add esp, 8
    iret
