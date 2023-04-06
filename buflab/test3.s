pushl $0x0804e120
pushl $0x080498a2
movl 0x804e048, %eax
movl %eax, *0x0804e018
movl $0x24, %eax
movl $0x55683e40, %ebp
movl $0x804e000, %ebx
ret
