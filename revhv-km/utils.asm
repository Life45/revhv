.code

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

end