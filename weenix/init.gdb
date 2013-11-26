handle SIGSEGV nostop noprint nopass
break dbg_panic_halt
break hard_shutdown
break bootstrap
b vmmap_map
b main/kmain.c:343
b main/kmain.c:344
b main/kmain.c:345
b main/kmain.c:346

add-symbol-file user/bin/uname.exec 0x08048094

d break 4
b main

set gdb_wait = 0

continue
