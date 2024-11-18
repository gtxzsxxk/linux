#ifndef _ASM_RISCV_MIDGARD_H
#define _ASM_RISCV_MIDGARD_H

#include <linux/types.h>

#define MIDGARD_B_TREE_GRADE	4

struct midgard_key {
	uint64_t base;
	uint64_t bound;
	uint64_t offset;
	uint8_t prot;
};

struct midgard_node {
	struct midgard_key keys[MIDGARD_B_TREE_GRADE - 1];
	struct midgard_node *children[MIDGARD_B_TREE_GRADE];
	int key_cnt;
	int is_leaf;
};

/* return the midgard address of the input virtual address */
uintptr_t midgard_insert_vma(uintptr_t va_base, phys_addr_t size, uint8_t prot);

#endif
