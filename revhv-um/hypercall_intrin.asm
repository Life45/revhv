.code

__vmcall proc
	mov rax, [rsp + 28h] ; Since MSVC's x64 convention passes the first 4 arguments in rcx, rdx, r8, and r9, the 5th argument is at [rsp + 28h]
	vmcall
	ret
__vmcall endp

end