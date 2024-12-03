#include <asm/midgard.h>
#include <asm/csr.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <linux/panic.h>

#define MIDGARD_BRUTE_NODES		4096
/* 暂且暴力分配 */
static struct midgard_node node_pool[MIDGARD_BRUTE_NODES] __page_aligned_bss;
static int node_alloc_counter = 0;

static struct midgard_node *alloc_node(void) {
	if(node_alloc_counter == MIDGARD_BRUTE_NODES) {
		panic("No enough space for midgard nodes");
	}
	return &node_pool[node_alloc_counter++];
}

/* 创建一个新的 B 树节点 */ 
static struct midgard_node *create_node(int is_leaf) {
	struct midgard_node *node = alloc_node();
	node->key_cnt = 0;
	node->is_leaf = is_leaf;
	for (int i = 0; i < MIDGARD_B_TREE_GRADE; i++) {
		node->children[i] = NULL;
	}
	return node;
}

/* 分裂子节点 */ 
static void split_child(struct midgard_node *parent, int i) {
	struct midgard_node *fullChild = parent->children[i];
	struct midgard_node *newChild = create_node(fullChild->is_leaf);
	int mid = (MIDGARD_B_TREE_GRADE - 1) / 2;

	/* 新子节点继承后一半的键 */
	newChild->key_cnt = MIDGARD_B_TREE_GRADE / 2 - 1;
	for (int j = 0; j < MIDGARD_B_TREE_GRADE / 2 - 1; j++) {
		newChild->keys[j] = fullChild->keys[j + mid + 1];
	}

	/* 如果不是叶节点，继承后一半的孩子指针 */
	if (!fullChild->is_leaf) {
		for (int j = 0; j < MIDGARD_B_TREE_GRADE / 2; j++) {
			newChild->children[j] = fullChild->children[j + mid + 1];
		}
	}

	/* 更新满节点的键数 */ 
	fullChild->key_cnt = mid;

	// 父节点插入新的键和孩子指针
	for (int j = parent->key_cnt; j >= i + 1; j--) {
		parent->children[j + 1] = parent->children[j];
	}
	parent->children[i + 1] = newChild;

	for (int j = parent->key_cnt - 1; j >= i; j--) {
		parent->keys[j + 1] = parent->keys[j];
	}
	parent->keys[i] = fullChild->keys[mid];
	parent->key_cnt++;
}

/* 在非满节点中插入键 */ 
static void insert_non_full(struct midgard_node *node, struct midgard_key *key) {
	int i = node->key_cnt - 1;

	if (node->is_leaf) {
		/* 在叶节点中插入键 */ 
		while (i >= 0 && key->base < node->keys[i].base) {
			node->keys[i + 1] = node->keys[i];
			i--;
		}
		node->keys[i + 1] = *key;
		node->key_cnt++;
	} else {
		/* 在内部节点中寻找插入位置 */ 
		while (i >= 0 && key->base < node->keys[i].base) {
			i--;
		}
		i++;
		/* 如果子节点满了，先分裂 */ 
		if (node->children[i]->key_cnt == MIDGARD_B_TREE_GRADE - 1) {
			split_child(node, i);
			if (key->base > node->keys[i].base) {
				i++;
			}
		}
		insert_non_full(node->children[i], key);
	}
}

/* 插入键到 B 树中 */ 
static void insert(struct midgard_node **root, struct midgard_key *key) {
	struct midgard_node *r = *root;
	if (r->key_cnt == MIDGARD_B_TREE_GRADE - 1) {
		struct midgard_node *s = create_node(0);
		s->children[0] = r;
		split_child(s, 0);
		insert_non_full(s, key);
		*root = s;
	} else {
		insert_non_full(r, key);
	}
}

struct midgard_node *midgard_search(struct midgard_node *root, uintptr_t va_base, int *pos) {
	int i = 0;

	if (!root) {
		return NULL;
	}

	while (i < root->key_cnt && va_base >= root->keys[i].bound) {
		i++;
	}

	if(i < root->key_cnt && va_base >= root->keys[i].base && va_base < root->keys[i].bound) {
		*pos = i;
		return root;
	}

	if(root->is_leaf) {
		return NULL;
	}

	return midgard_search(root->children[i], va_base, pos);
}

static void midgard_copy(struct midgard_node *src, struct midgard_node **dest) {
	for (int i = 0; i < src->key_cnt; i++) {
		if (*dest == NULL) {
			*dest = create_node(1);
		}
		insert(dest, &src->keys[i]);
	}
	for (int i = 0; i <= src->key_cnt; i++) {
		if (!src->children[i]) {
			continue;
		}
		midgard_copy(src->children[i], dest);
	}
}

/* 对 midgard b 树填入物理地址，这个 b 树一般需要是一个拷贝 */
static void midgard_sanitize(struct midgard_node *root) {
	int need_pa_symbol = ((uintptr_t)midgard_sanitize >> 32) & 0xffffffff;
	for (int i = 0; i <= root->key_cnt; i++) {
		if (!root->children[i]) {
			root->phys_children[i] = NULL;
			continue;
		}
		midgard_sanitize(root->children[i]);
		root->phys_children[i] = need_pa_symbol ? (phys_addr_t*)__pa_symbol(root->children[i]) : (phys_addr_t*)root->children[i];
	}
}

uintptr_t midgard_insert_vma(struct midgard_node **root, uintptr_t va_base, phys_addr_t size, uint8_t prot, bool update_csr) {
	static uint64_t counter = 1;
	uintptr_t midgard_addr = 0xffaf100000000000 | ((counter++) << (8 * 4)) | (va_base & 0xfff);
	int pos = -1;

	struct midgard_node *lookup = midgard_search(*root, va_base, &pos);
	if(lookup) {
		panic("Existing midgard va address!");
	}

	struct midgard_key key = {
		.base = va_base,
		.bound = va_base + size,
		.offset = midgard_addr - va_base,
		.prot = prot,
	};

	if (*root == NULL) {
		/* TODO: 区分early(__init) 和late */
		*root = create_node(1);
	}

	insert(root, &key);

	if (update_csr) {
		struct midgard_node *root_cp = NULL;
		midgard_copy(*root, &root_cp);
		midgard_sanitize(root_cp);
		csr_write(CSR_SAMT, __pa_symbol(root_cp));
	} else {
		midgard_sanitize(*root);
	}

	return midgard_addr;
}
