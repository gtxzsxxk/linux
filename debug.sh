#!/usr/bin/bash

$HOME/qemu/build/qemu-system-riscv64 -nographic -machine virt -smp 1 \
    -m 128M -kernel arch/riscv/boot/Image \
    -d in_asm -D qemu_exec.log \
    -s -S
#    -accel tcg,one-insn-per-tb=on \
#    -plugin $HOME/qemu/build/contrib/plugins/libcache.so -d plugin -D qemu_cache_plugin_log
