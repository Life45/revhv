.code

vmx_vmlaunch_stub proc
    ; Set RIP to successful_launch
    mov rdx, 0000681Eh ; VMCS_GUEST_RIP
    mov rax, successful_launch
    vmwrite rdx, rax

    ; Set RSP to current stack pointer
    mov rdx, 0000681Ch ; VMCS_GUEST_RSP
    mov rax, rsp
    vmwrite rdx, rax

    ; Launch the VM
    vmlaunch

    ; Reaching this point means the VM failed to launch
    ; NOTE: VMLAUNCH fail doesn't necessarily need to result here. It can also result in a VMEXIT in some circumstances if the host area is set-up correctly.
    ; RFLAGS determines between VMfailInvalid and VMfailValid, so we copy it to rcx
    pushfq
    pop rax
    mov [rcx], rax

    ; Return 0
    xor rax, rax
    ret
successful_launch:

    ; If we reached this point, VM launch succeeded and we're in VMX non-root mode
    mov rax, 1
    ret

vmx_vmlaunch_stub endp

end