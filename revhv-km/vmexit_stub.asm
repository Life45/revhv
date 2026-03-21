.code

; ------------------------------------------------------------------
; tsc_data
; defined at stealth.h
; ------------------------------------------------------------------
tsc_data struct
    $tsc_offset                 qword ?
    $stored_tsc                 qword ?
    $vmexit_to_store_overhead   qword ?
    $instruction_overhead       qword ?
    $vmentry_overhead           qword ?
tsc_data ends

; ------------------------------------------------------------------
; guest_context
; defined at vcpu.h
; ensure 16 byte alignment after any change to the structure
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

extern ?handler@vmexit@hv@@YAPEAXPEAUguest_context@vcpu@2@PEAU442@@Z : proc

vmexit_stub proc
    ; Fast path for timing benchmarks, identified by CPUID exit reason and RCX = 52564856424E4348 (RVHVBNCH)

    ; Try a fast miss by checking lower 32 bits of RVHVBNCH ('BNCH'), saves cycles of performing a vmread 
    cmp ecx, 424E4348h 
    jne normal_path_no_restore

    ; Lower 32 bits match, likely a benchmark VMEXIT
    push r10
    push r11

    ; Check if exit reason is CPUID
    mov r10, 4402h ; VMCS_EXIT_REASON
    vmread r10, r10
    cmp r10, 0Ah ; VMX_EXIT_REASON_EXECUTE_CPUID
    jne normal_path

    ; Check if rcx matches the key
    mov r10, 52564856424E4348h ; RVHVBNCH
    cmp rcx, r10
    jne normal_path

    ; Set RIP to one provided by the guest in r8
    mov r11, 681Eh ; VMCS_GUEST_RIP
    vmwrite r11, r8

    ; Restore
    pop r11
    pop r10

    ; Measure the VMENTRY time by reading the TSC right before VMRESUME, and let the guest read it right after VMENTRY
    push rdx
    lfence
    rdtsc
    lfence

    shl rdx, 32
    or rax, rdx
    mov r9, rax ; R9 will be the TSC of right before executing VMRESUME

    ; We include a dummy VMWRITE to the TSC offset field here to mimic what's done in the time hiding path
    ; since the overhead of VMWRITE is not accounted for there (it's done after we read the final TSC)
    mov rax, 2010h ; VMCS_CTRL_TSC_OFFSET
    xor rdx,rdx
    dec rdx ; Just to have some more internal serialization since TSC offset is already 0
    vmwrite rax, rdx

    pop rdx

    ; Resume guest
    vmresume
    int 3
    
normal_path:
    ; Restore
    pop r11
    pop r10

normal_path_no_restore:
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
    rdfsbase rdx

    ; Shadow stack and call the vmexit handler
    sub rsp, 20h
    call ?handler@vmexit@hv@@YAPEAXPEAUguest_context@vcpu@2@PEAU442@@Z
    add rsp, 20h

    ; rax points to tsc_data
    mov rcx, rax

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
    ; rax is restored later
    ; rcx is restored later
    ; rdx is restored later
    ; rbx is restored later
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

    ; if rcx(tsc_data*) is non zero, we update TSC offset
    test rcx, rcx
    jne update_tsc_offset

    ; we can restore rax,rcx,rdx,rbx as TSC offset doesn't need to be updated
    mov rax, [rsp + guest_context.$rax]
    mov rcx, [rsp + guest_context.$rcx]
    mov rdx, [rsp + guest_context.$rdx]
    mov rbx, [rsp + guest_context.$rbx]

    ; Free the guest context on the stack
    add rsp, SIZEOF guest_context

    ; Resume VM
    vmresume
    int 3

update_tsc_offset:
    mov rbx, [rcx + tsc_data.$stored_tsc] ; Stored TSC

    ; desired_tsc = stored_tsc + instruction_overhead - vmentry_overhead - vmexit_to_store_overhead
    add rbx, [rcx + tsc_data.$instruction_overhead]
    sub rbx, [rcx + tsc_data.$vmentry_overhead]
    sub rbx, [rcx + tsc_data.$vmexit_to_store_overhead]

    ; Get the final TSC
    lfence
    rdtsc 
    lfence
    shl rdx, 32
    or rax, rdx

    ; offset -= (curr_tsc - desired_tsc)
    sub rax, rbx ;
    mov rbx, [rcx + tsc_data.$tsc_offset] ; Current TSC offset
    sub rbx, rax ; Subtract the current difference from the TSC offset
    mov [rcx + tsc_data.$tsc_offset], rbx ; Save the new TSC offset

    mov rax, 2010h ; VMCS_CTRL_TSC_OFFSET
    vmwrite rax, rbx ; VMWRITE the new TSC offset

    ; Restore GPRs
    mov rax, [rsp + guest_context.$rax]
    mov rcx, [rsp + guest_context.$rcx]
    mov rdx, [rsp + guest_context.$rdx]
    mov rbx, [rsp + guest_context.$rbx]

    ; Free the guest context on the stack
    add rsp, SIZEOF guest_context

    ; Resume VM
    vmresume
    int 3
vmexit_stub endp

end