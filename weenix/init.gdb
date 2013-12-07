handle SIGSEGV nostop noprint nopass
break dbg_panic_halt
break hard_shutdown
break bootstrap


add-symbol-file user/usr/bin/eatmem.exec 0x08048094

b main


set gdb_wait = 0

continue
