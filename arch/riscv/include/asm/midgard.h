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
	uint8_t key_cnt;
	uint8_t is_leaf;

	/* 用于给处理器看的，通过 sanitize 刷新 */
	uint64_t *phys_children[MIDGARD_B_TREE_GRADE];
};

extern struct midgard_key *midgard_scratch;

struct midgard_node *midgard_search_close_bound(struct midgard_node *root, uintptr_t va_base, int *pos);

struct midgard_node *midgard_search(struct midgard_node *root, uintptr_t va_base, int *pos);

void midgard_copy(struct midgard_node *src, struct midgard_node **dest);

void midgard_full_sanitize_and_update_csr(struct midgard_node **root);

/* 指定 midgard addr，而不是自动生成 */
uintptr_t midgard_insert_specified_vma(struct midgard_node **root, uintptr_t ma_base, uintptr_t va_base, phys_addr_t size, uint8_t prot, bool update_csr);

/* 返回输入虚拟地址的 midgard 地址，并且根据需要自动重填 SAMT */
uintptr_t midgard_insert_vma(struct midgard_node **root, uintptr_t va_base, phys_addr_t size, uint8_t prot, bool update_csr);

void midgard_print(struct midgard_node *root);

#endif
