.code

stealth_vmexit_bench_one proc
    ; cpuid "clobbers" rbx in this case
    push rbx
    ; rcx will be the pointer to vmentry time
    push rcx

    ; Load r8 with the next RIP
    lea r8, next_rip
    ; Load rcx with the CPUID key for benchmarking
    mov rcx, 52564856424E4348h
    
    lfence
    rdtsc
    lfence

    shl rdx, 32
    or rax, rdx
    mov r10, rax

    lfence
    cpuid ; Trigger VMEXIT, fast path in the VMEXIT stub should resume us in next_rip
next_rip:
    lfence
    rdtsc
    lfence

    shl rdx, 32
    or rax, rdx ; RAX is now the TSC right after VMRESUME

    ; R9 has the the TSC from right before exexcuting vmresume, so we can calculate the VMENTRY time by subtracting it from the TSC right after VMRESUME
    mov rbx, rax ; Since rbx is already clobbered by cpuid, we can use it to store the TSC right after VMRESUME
    sub rbx, r9 ; RBX is now the elapsed time
    pop rcx ; Restore rcx (pointer to vmentry time)
    mov [rcx], rbx ; Store the elapsed time to the pointer provided by the caller

    mov rax, r10 ; RAX has the TSC of right before causing a VMEXIT
    pop rbx
    ret
stealth_vmexit_bench_one endp

end