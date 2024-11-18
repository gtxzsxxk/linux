#!/usr/bin/bash

cp vmlinux vmlinux1
riscv64-buildroot-linux-gnu-strip -g vmlinux1
riscv64-buildroot-linux-gnu-objdump vmlinux1 > vmlinux.dump -D -M numeric,no-aliases
