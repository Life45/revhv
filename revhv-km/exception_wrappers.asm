.code

rdmsr_wrapper proc
    push rdi
    push rsi
    lea rsi, exception_instruction
    lea rdi, exception_handler
exception_instruction:
    rdmsr

    ; result is saved in EDX:EAX
    shl rdx, 32
    or rax, rdx
exception_handler:
    pop rsi
    pop rdi
    ret
rdmsr_wrapper endp

wrmsr_wrapper proc
    push rdi
    push rsi
    lea rsi, exception_instruction
    lea rdi, exception_handler

    ; writes the value in EDX:EAX to the MSR specified by the ECX register
    ; move lower 32 bits of RDX to EAX
    mov eax, edx
    ; move upper 32 bits of RDX to EDX
    shr rdx, 32
exception_instruction:
    wrmsr

exception_handler:
    pop rsi
    pop rdi
    ret
wrmsr_wrapper endp

invd_wrapper proc
    push rdi
    push rsi
    lea rsi, exception_instruction
    lea rdi, exception_handler
exception_instruction:
    invd
exception_handler:
    pop rsi
    pop rdi
    ret
invd_wrapper endp

xsetbv_wrapper proc
    push rdi
    push rsi
    lea rsi, exception_instruction
    lea rdi, exception_handler

    ; writes the value in EDX:EAX to the MSR specified by the ECX register
    ; move lower 32 bits of RDX to EAX
    mov eax, edx
    ; move upper 32 bits of RDX to EDX
    shr rdx, 32
exception_instruction:
    xsetbv
exception_handler:
    pop rsi
    pop rdi
    ret
xsetbv_wrapper endp

end