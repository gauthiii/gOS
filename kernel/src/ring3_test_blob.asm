; Wraps tools/userland/ring3_test.bin (assembled separately as a flat
; binary by the Makefile) as linkable symbols, so kernel/src/usermode.c can
; copy it into a freshly-mapped user page. See usermode.h/usermode.c.

bits 64
section .rodata

global ring3_test_blob
global ring3_test_blob_end

ring3_test_blob:
    incbin "build/ring3_test.bin"
ring3_test_blob_end:
