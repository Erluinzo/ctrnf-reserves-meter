# Entry points the patched game code jumps into (USA CUSA13795 1.21, ELF addresses).
# Game addresses are symbols set by build.py; all references are RIP-relative.

    .intel_syntax noprefix
    .text

# Hooks placed in the middle of game code: keep every register a C call may change (also the
# vector registers) and align the stack, whatever its alignment was.
.macro SAVE
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push rbx
    mov rbx, rsp
    and rsp, -16
    sub rsp, 256
    .irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    movdqu [rsp + \n * 16], xmm\n
    .endr
.endm

.macro RESTORE
    .irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    movdqu xmm\n, [rsp + \n * 16]
    .endr
    mov rsp, rbx
    pop rbx
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
.endm

# Replaces "vmovss xmm0, [rdx+0x18]; jmp boost_update" at 0xe75240, the boost component's per-frame
# callback (rdi = component, [rdx+0x18] = frame time). After the game's update, rv_boosts_updated
# reads the boosts on the same thread that changes them. rax and xmm0 of the game's call are kept.
    .globl hook_boost
hook_boost:
    push rbx
    sub rsp, 32                             # also keeps the stack 16-byte aligned for the calls
    mov rbx, rdi
    vmovss xmm0, dword ptr [rdx + 0x18]
    call game_boost_update
    mov [rsp], rax
    vmovdqu [rsp + 16], xmm0
    mov rdi, rbx
    call rv_boosts_updated
    mov rax, [rsp]
    vmovdqu xmm0, [rsp + 16]
    add rsp, 32
    pop rbx
    ret

# Replaces "push rbp; mov rbp, rsp; push r15" at 0xe5aa30, the start of the racer HUD component's
# per-frame callback (rdi = component). The game's code goes on at 0xe5aa36.
    .globl hook_hud
hook_hud:
    SAVE
    call rv_hud_update                      # rdi is still the component
    RESTORE
    push rbp
    mov rbp, rsp
    push r15
    jmp game_hud_body
