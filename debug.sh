#!/usr/bin/bash

$HOME/qemu/build/qemu-system-riscv64 -nographic -machine virt \
    -m 128M -kernel arch/riscv/boot/Image \
    -s -S
#    -plugin $HOME/qemu/build/contrib/plugins/libcache.so -d plugin -D qemu_cache_plugin_log
