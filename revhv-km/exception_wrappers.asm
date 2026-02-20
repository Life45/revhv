.code

rdmsr_wrapper proc
    push r15
    push r14
    lea r14, exception_instruction
    lea r15, exception_handler
exception_instruction:
    rdmsr

    ; result is saved in EDX:EAX
    shl rdx, 32
    or rax, rdx
exception_handler:
    pop r14
    pop r15
    ret
rdmsr_wrapper endp

wrmsr_wrapper proc
    push r15
    push r14
    lea r14, exception_instruction
    lea r15, exception_handler

    ; writes the value in EDX:EAX to the MSR specified by the ECX register
    ; move lower 32 bits of RDX to EAX
    mov eax, edx
    ; move upper 32 bits of RDX to EDX
    shr rdx, 32
exception_instruction:
    wrmsr

exception_handler:
    pop r14
    pop r15
    ret
wrmsr_wrapper endp

invd_wrapper proc
    push r15
    push r14
    lea r14, exception_instruction
    lea r15, exception_handler
exception_instruction:
    invd
exception_handler:
    pop r14
    pop r15
    ret
invd_wrapper endp

xsetbv_wrapper proc
    push r15
    push r14
    lea r14, exception_instruction
    lea r15, exception_handler

    ; writes the value in EDX:EAX to the MSR specified by the ECX register
    ; move lower 32 bits of RDX to EAX
    mov eax, edx
    ; move upper 32 bits of RDX to EDX
    shr rdx, 32
exception_instruction:
    xsetbv
exception_handler:
    pop r14
    pop r15
    ret
xsetbv_wrapper endp

memcpy_wrapper proc
    push r15
    push r14
    lea r14, exception_instruction
    lea r15, exception_handler

    ; rcx = dest, rdx = src, r8 = count
    ; rep movsb uses rdi = dest, rsi = src, rcx = count
    push rdi
    push rsi
    mov rdi, rcx
    mov rsi, rdx
    mov rcx, r8
exception_instruction:
    rep movsb
exception_handler:
    pop rsi
    pop rdi
    pop r14
    pop r15
    ret
memcpy_wrapper endp

end