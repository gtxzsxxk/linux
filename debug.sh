#!/usr/bin/bash

$HOME/qemu/build/qemu-system-riscv64 -nographic -machine virt -smp 1 \
    -m 128M -kernel arch/riscv/boot/Image \
    -append "root=/dev/vda rw console=ttyS0" \
    -drive if=none,file=../buildroot-2024.08.2/output/images/rootfs.ext2,format=raw,id=hd0  \
	-device virtio-blk-device,drive=hd0 \
    -d in_asm -D qemu_exec.log \
    -s -S
#    -accel tcg,one-insn-per-tb=on \
#    -plugin $HOME/qemu/build/contrib/plugins/libcache.so -d plugin -D qemu_cache_plugin_log
