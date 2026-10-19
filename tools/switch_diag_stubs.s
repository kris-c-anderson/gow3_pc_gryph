# Source of the switch-attack diagnostic stubs in src/switch_hook.h (GNU as, x86-64).
# as --64 -o stubs.o switch_diag_stubs.s; each stub is the bytes from its label to its _end label.
.intel_syntax noprefix
# STATE placeholder 0x1111111111111111. Every stub ends where the C builder appends jmp [rip] + resume.
.macro SAVE
    push r8
    push r9
    push r10
    push r11
    push rcx
.endm
.macro RESTORE
    pop rcx
    pop r11
    pop r10
    pop r9
    pop r8
.endm
# r11 state, r10d seq (index+1), r9 record
.macro ALLOC
    movabs r11, 0x1111111111111111
    mov r10d, 1
    lock xadd dword ptr [r11], r10d
    mov r9d, r10d
    and r9d, 1023
    shl r9d, 7
    lea r9, [r11 + r9 + 64]
    inc r10d
.endm
.macro COPY7 src, at
    mov rcx, [\src]
    mov [r9 + \at], rcx
    mov rcx, [\src + 8]
    mov [r9 + \at + 8], rcx
    mov rcx, [\src + 16]
    mov [r9 + \at + 16], rcx
    mov rcx, [\src + 24]
    mov [r9 + \at + 24], rcx
    mov rcx, [\src + 32]
    mov [r9 + \at + 32], rcx
    mov rcx, [\src + 40]
    mov [r9 + \at + 40], rcx
    mov rcx, [\src + 48]
    mov [r9 + \at + 48], rcx
.endm
.macro DONE kind
    mov byte ptr [r9 + 4], \kind
    mov dword ptr [r9], r10d
.endm

.globl wc_entry
wc_entry:
    push r11
    movabs r11, 0x1111111111111111
    mov [r11 + 24], rdi
    mov [r11 + 32], edx
    pop r11
    push rbp
    mov rbp, rsp
    push r14
wc_entry_end:

.globl wc_exit
wc_exit:
    SAVE
    movzx r8d, byte ptr [r14 + 0x2f]
    sub r8d, 0x10
    cmp r8d, 3
    ja 1f
    ALLOC
    mov [r9 + 8], r14
    mov [r9 + 16], eax
    mov ecx, [r11 + 32]
    mov [r9 + 20], ecx
    mov r8, [r11 + 24]
    mov [r9 + 24], r8
    mov rcx, [r8 + 0x168]
    mov [r9 + 32], rcx
    mov rcx, [r8 + 0x208]
    mov [r9 + 40], rcx
    mov rcx, [r14 + 0x10]
    mov [r9 + 48], rcx
    mov rcx, [rbp + 8]
    mov [r9 + 56], rcx
    COPY7 r14, 64
    DONE 2
1:
    RESTORE
    add rsp, 0x10
    pop rbx
wc_exit_end:

.globl eval_exit
eval_exit:
    SAVE
    movzx r8d, byte ptr [rbx + 0x2f]
    sub r8d, 0x10
    cmp r8d, 3
    jbe 2f
    cmp eax, 1
    jne 1f
    cmp dword ptr [rbp + 0x18], -1
    je 1f
2:
    ALLOC
    mov [r9 + 8], rbx
    mov [r9 + 16], eax
    mov [r9 + 20], edx
    mov ecx, [rbp + 0x18]
    mov [r9 + 24], ecx
    mov ecx, [rbp + 0x20]
    mov [r9 + 28], ecx
    mov ecx, [rbp + 0x28]
    mov [r9 + 32], ecx
    mov ecx, [rbp + 0x30]
    mov [r9 + 36], ecx
    mov rcx, [rbp + 0x40]
    mov [r9 + 40], rcx
    mov rcx, [rbp + 0x10]
    mov [r9 + 48], rcx
    mov rcx, [rbp + 8]
    mov [r9 + 56], rcx
    xor r8d, r8d
    cmp rcx, [r11 + 16]
    jne 3f
    mov rcx, [rbp]
    mov rcx, [rcx - 0x1c8]
    mov r8, [rcx + 0xa8]
3:
    mov [r9 + 64], r8
    COPY7 rbx, 72
    DONE 1
1:
    RESTORE
    add rsp, 8
    pop rbx
eval_exit_end:

.globl queue_entry
queue_entry:
    SAVE
    ALLOC
    mov [r9 + 8], rdi
    mov [r9 + 16], rsi
    movd ecx, xmm0
    mov [r9 + 24], ecx
    mov rcx, [rsp + 40]
    mov [r9 + 32], rcx
    mov rcx, [rdi + 0xa8]
    mov [r9 + 40], rcx
    mov rcx, [rdi + 0xd0]
    mov [r9 + 48], rcx
    COPY7 rsi, 56
    DONE 3
    RESTORE
    movzx eax, byte ptr [rdi + 0xd2]
queue_entry_end:

.macro ENTRY name, kind
.globl \name
\name:
    SAVE
    ALLOC
    mov [r9 + 8], rdi
    mov [r9 + 16], rsi
    mov [r9 + 24], rdx
    mov rcx, [rsp]
    mov [r9 + 32], rcx
    mov rcx, [rsp + 40]
    mov [r9 + 40], rcx
    mov rcx, [rdi + 0x168]
    mov [r9 + 48], rcx
    mov rcx, [rdi + 0x208]
    mov [r9 + 56], rcx
    mov rcx, [rdi + 0x1c0]
    mov [r9 + 64], rcx
    DONE \kind
    RESTORE
.endm

ENTRY dpad_entry, 4
    push rbp
    mov rbp, rsp
    push rbx
    push rax
dpad_entry_end:
ENTRY apply_entry, 5
    push rbp
    mov rbp, rsp
    push r15
apply_entry_end:
ENTRY select_entry, 6
    push rbp
    mov rbp, rsp
    push r15
select_entry_end:
