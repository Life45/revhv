.code

cpu_hang_unblock_nmi proc
    ; RCX contains CS selector
    
    ; 1. SS
    push 0
    
    ; 2. RSP (Use current stack)
    push rsp
    
    ; 3. RFLAGS
    pushfq
    pop rax
    or rax, 200h ; Set IF (Interrupt Flag) so we can accept normal IPIs/NMIs
    push rax
    
    ; 4. CS (Target OS CS)
    and rcx, 0FFFFh ; Ensure we only use the selector part
    push rcx
    
    ; 5. RIP (Target loop)
    lea rax, hang_loop
    push rax
    
    iretq
    
hang_loop:
    pause
    jmp hang_loop
    
cpu_hang_unblock_nmi endp

read_cs proc
    mov ax, cs
    ret
read_cs endp

read_ss proc
    mov ax, ss
    ret
read_ss endp

read_ds proc
    mov ax, ds
    ret
read_ds endp

read_es proc
    mov ax, es
    ret
read_es endp

read_fs proc
    mov ax, fs
    ret
read_fs endp

read_gs proc
    mov ax, gs
    ret
read_gs endp

read_tr proc
    str ax
    ret
read_tr endp

read_ldtr proc
    sldt ax
    ret
read_ldtr endp

write_ds proc
    mov ds, cx
    ret
write_ds endp

write_es proc
    mov es, cx
    ret
write_es endp

write_fs proc
    mov fs, cx
    ret
write_fs endp

write_gs proc
    mov gs, cx
    ret
write_gs endp

write_tr proc
    ltr cx
    ret
write_tr endp

write_ldtr proc
    lldt cx
    ret
write_ldtr endp

test_trash_rsp proc
    mov rsp, 0
    ret
test_trash_rsp endp

end