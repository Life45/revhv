.code

extern ?handle_exception@exception@hv@@YAXPEAUtrap_frame@12@PEAUvcpu@42@@Z : proc

generic_isr proc
    ; complete the trap frame by pushing general-purpose registers
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rbp
    push rbx
    push rdx
    push rcx
    push rax

    ; first argument is rsp(trap frame)
    mov rcx, rsp
    ; second argument is the vcpu pointer (fsbase)
    rdfsbase rdx

    sub rsp, 20h
    call ?handle_exception@exception@hv@@YAXPEAUtrap_frame@12@PEAUvcpu@42@@Z
    add rsp, 20h

    ; restore the general-purpose registers
    pop rax
    pop rcx
    pop rdx
    pop rbx
    pop rbp
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    ; pop vector
    add rsp, 8

    ; pop error code
    add rsp, 8

    iretq
generic_isr endp

ISR_NOERR macro vec
public isr_&vec
isr_&vec proc
    push 0              ; dummy error code
    push vec            ; vector
    jmp  generic_isr
isr_&vec endp
endm

ISR_ERR macro vec
public isr_&vec
isr_&vec proc
    push vec            ; vector
    jmp  generic_isr
isr_&vec endp
endm

; --- Define ISRs ---

ISR_NOERR 0   ; #DE
ISR_NOERR 1   ; #DB
ISR_NOERR 2   ; NMI
ISR_NOERR 3   ; #BP
ISR_NOERR 4   ; #OF
ISR_NOERR 5   ; #BR
ISR_NOERR 6   ; #UD
ISR_NOERR 7   ; #NM
ISR_ERR   8   ; #DF
; 9 reserved
ISR_ERR   10  ; #TS
ISR_ERR   11  ; #NP
ISR_ERR   12  ; #SS
ISR_ERR   13  ; #GP
ISR_ERR   14  ; #PF
; 15 reserved
ISR_NOERR 16  ; #MF
ISR_ERR   17  ; #AC
ISR_NOERR 18  ; #MC
ISR_NOERR 19  ; #XM
ISR_NOERR 20  ; #VE
ISR_ERR   21  ; #CP

END