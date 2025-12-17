.code

; ------------------------------------------------------------------
; guest_context
; defined at vcpu.h
; ------------------------------------------------------------------
guest_context struct
    ; --------------------------------------------------------------
    ; General-purpose registers (8 bytes each)
    ; --------------------------------------------------------------
    $rax     qword ?
    $rcx     qword ?
    $rdx     qword ?
    $rbx     qword ?
    $rbp     qword ?
    $rsi     qword ?
    $rdi     qword ?
    $r8      qword ?
    $r9      qword ?
    $r10     qword ?
    $r11     qword ?
    $r12     qword ?
    $r13     qword ?
    $r14     qword ?
    $r15     qword ?

    ; --------------------------------------------------------------
    ; Control registers
    ; --------------------------------------------------------------
    $cr2     qword ?
    $cr8     qword ?

    ; --------------------------------------------------------------
    ; Debug registers
    ; --------------------------------------------------------------
    $dr0     qword ?
    $dr1     qword ?
    $dr2     qword ?
    $dr3     qword ?
    $dr6     qword ?

    ; --------------------------------------------------------------
    ; XMM registers
    ; --------------------------------------------------------------
    $xmm0    oword ?
    $xmm1    oword ?
    $xmm2    oword ?
    $xmm3    oword ?
    $xmm4    oword ?
    $xmm5    oword ?
    $xmm6    oword ?
    $xmm7    oword ?
    $xmm8    oword ?
    $xmm9    oword ?
    $xmm10   oword ?
    $xmm11   oword ?
    $xmm12   oword ?
    $xmm13   oword ?
    $xmm14   oword ?
    $xmm15   oword ?

    ; --------------------------------------------------------------
    ; MXCSR (32-bit)
    ; Pad to keep struct size 16 byte aligned
    ; --------------------------------------------------------------
    $mxcsr   dword ?
    $pad1    dword ?        ; padding for alignment
    $pad2    dword ?        ; padding for alignment
    $pad3    dword ?        ; padding for alignment
guest_context ends

IF (SIZEOF guest_context) MOD 16 NE 0
    .ERR <guest_context size must be 16-byte multiple>
ENDIF

extern ?handler@vmexit@hv@@YAXPEAUguest_context@vcpu@2@PEAU442@@Z : proc

vmexit_stub proc
    ; Allocate space for the guest context on the stack
    sub rsp, SIZEOF guest_context

    ; Save GPRs
    mov [rsp + guest_context.$rax], rax
    mov [rsp + guest_context.$rcx], rcx
    mov [rsp + guest_context.$rdx], rdx
    mov [rsp + guest_context.$rbx], rbx
    mov [rsp + guest_context.$rbp], rbp
    mov [rsp + guest_context.$rsi], rsi
    mov [rsp + guest_context.$rdi], rdi
    mov [rsp + guest_context.$r8], r8
    mov [rsp + guest_context.$r9], r9
    mov [rsp + guest_context.$r10], r10
    mov [rsp + guest_context.$r11], r11
    mov [rsp + guest_context.$r12], r12
    mov [rsp + guest_context.$r13], r13
    mov [rsp + guest_context.$r14], r14
    mov [rsp + guest_context.$r15], r15

    ; Save control registers
    mov rax, cr2
    mov [rsp + guest_context.$cr2], rax
    mov rax, cr8
    mov [rsp + guest_context.$cr8], rax

    ; Save debug registers
    mov rax, dr0
    mov [rsp + guest_context.$dr0], rax
    mov rax, dr1
    mov [rsp + guest_context.$dr1], rax
    mov rax, dr2
    mov [rsp + guest_context.$dr2], rax
    mov rax, dr3
    mov [rsp + guest_context.$dr3], rax
    mov rax, dr6
    mov [rsp + guest_context.$dr6], rax

    ; Save XMM registers
    movaps [rsp + guest_context.$xmm0], xmm0
    movaps [rsp + guest_context.$xmm1], xmm1
    movaps [rsp + guest_context.$xmm2], xmm2
    movaps [rsp + guest_context.$xmm3], xmm3
    movaps [rsp + guest_context.$xmm4], xmm4
    movaps [rsp + guest_context.$xmm5], xmm5
    movaps [rsp + guest_context.$xmm6], xmm6
    movaps [rsp + guest_context.$xmm7], xmm7
    movaps [rsp + guest_context.$xmm8], xmm8
    movaps [rsp + guest_context.$xmm9], xmm9
    movaps [rsp + guest_context.$xmm10], xmm10
    movaps [rsp + guest_context.$xmm11], xmm11
    movaps [rsp + guest_context.$xmm12], xmm12
    movaps [rsp + guest_context.$xmm13], xmm13
    movaps [rsp + guest_context.$xmm14], xmm14
    movaps [rsp + guest_context.$xmm15], xmm15

    ; Save MXCSR register
    stmxcsr [rsp + guest_context.$mxcsr]

    ; First argument is the guest context pointer
    mov rcx, rsp
    ; Second argument is the vcpu pointer
    mov rdx, fs:[0]

    ; Shadow stack and call the vmexit handler
    sub rsp, 20h
    call ?handler@vmexit@hv@@YAXPEAUguest_context@vcpu@2@PEAU442@@Z
    add rsp, 20h

    ; Restore MXCSR register
    ldmxcsr [rsp + guest_context.$mxcsr]

    ; Restore XMM registers
    movaps xmm0, [rsp + guest_context.$xmm0]
    movaps xmm1, [rsp + guest_context.$xmm1]
    movaps xmm2, [rsp + guest_context.$xmm2]
    movaps xmm3, [rsp + guest_context.$xmm3]
    movaps xmm4, [rsp + guest_context.$xmm4]
    movaps xmm5, [rsp + guest_context.$xmm5]
    movaps xmm6, [rsp + guest_context.$xmm6]
    movaps xmm7, [rsp + guest_context.$xmm7]
    movaps xmm8, [rsp + guest_context.$xmm8]
    movaps xmm9, [rsp + guest_context.$xmm9]
    movaps xmm10, [rsp + guest_context.$xmm10]
    movaps xmm11, [rsp + guest_context.$xmm11]
    movaps xmm12, [rsp + guest_context.$xmm12]
    movaps xmm13, [rsp + guest_context.$xmm13]
    movaps xmm14, [rsp + guest_context.$xmm14]
    movaps xmm15, [rsp + guest_context.$xmm15]

    ; Restore debug registers
    mov rax, [rsp + guest_context.$dr0]
    mov dr0, rax
    mov rax, [rsp + guest_context.$dr1]
    mov dr1, rax
    mov rax, [rsp + guest_context.$dr2]
    mov dr2, rax
    mov rax, [rsp + guest_context.$dr3]
    mov dr3, rax
    mov rax, [rsp + guest_context.$dr6]
    mov dr6, rax
    
    ; Restore control registers (even though CR2 and CR8 shouldn't really be modified, we want to respect what the VMEXIT handler modifies)
    mov rax, [rsp + guest_context.$cr2]
    mov cr2, rax
    mov rax, [rsp + guest_context.$cr8]
    mov cr8, rax

    ; Restore GPRs
    mov rax, [rsp + guest_context.$rax]
    mov rcx, [rsp + guest_context.$rcx]
    mov rdx, [rsp + guest_context.$rdx]
    mov rbx, [rsp + guest_context.$rbx]
    mov rbp, [rsp + guest_context.$rbp]
    mov rsi, [rsp + guest_context.$rsi]
    mov rdi, [rsp + guest_context.$rdi]
    mov r8, [rsp + guest_context.$r8]
    mov r9, [rsp + guest_context.$r9]
    mov r10, [rsp + guest_context.$r10]
    mov r11, [rsp + guest_context.$r11]
    mov r12, [rsp + guest_context.$r12]
    mov r13, [rsp + guest_context.$r13]
    mov r14, [rsp + guest_context.$r14]
    mov r15, [rsp + guest_context.$r15]

    ; Free the guest context on the stack
    add rsp, SIZEOF guest_context

    ; Resume VM
    vmresume
vmexit_stub endp

end