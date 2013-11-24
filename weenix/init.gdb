handle SIGSEGV nostop noprint nopass
break dbg_panic_halt
break hard_shutdown
break bootstrap
b vmmap_map

set gdb_wait = 0

continue
