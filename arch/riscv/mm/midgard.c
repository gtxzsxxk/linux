#include <asm/midgard.h>
#include <asm/csr.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <linux/panic.h>

#define MIDGARD_BRUTE_NODES		8192
/* 暂且暴力分配 */
static struct midgard_node node_pool[MIDGARD_BRUTE_NODES] __page_aligned_bss;
static int node_alloc_counter = 0;

struct midgard_key *midgard_scratch = NULL;
static uint64_t midgard_addr_counter = 1;

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

struct midgard_node *midgard_search_close_bound(struct midgard_node *root, uintptr_t va_base, int *pos) {
	int i = 0;

	if (!root) {
		return NULL;
	}

	while (i < root->key_cnt && va_base >= root->keys[i].bound) {
		i++;
	}

	if(i < root->key_cnt && va_base >= root->keys[i].base && va_base <= root->keys[i].bound) {
		*pos = i;
		return root;
	}

	if(root->is_leaf) {
		return NULL;
	}

	return midgard_search_close_bound(root->children[i], va_base, pos);
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

void midgard_copy(struct midgard_node *src, struct midgard_node **dest) {
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

void midgard_full_sanitize_and_update_csr(struct midgard_node **root) {
	struct midgard_node *root_cp = NULL;
	midgard_copy(*root, &root_cp);
	midgard_sanitize(root_cp);
	csr_write(CSR_SAMT, __pa_symbol(root_cp));
}

uintptr_t midgard_insert_specified_vma(struct midgard_node **root, uintptr_t ma_base, uintptr_t va_base, phys_addr_t size, uint8_t prot, bool update_csr) {
	uintptr_t midgard_addr = ma_base;
	int pos = -1;

	struct midgard_node *lookup = midgard_search(*root, va_base, &pos);
	if(lookup && pos != -1) {
		pr_err("Existing midgard va address!");
		return lookup->keys[pos].offset + va_base;
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

	/* TODO: 检查有的地方是否应该 update_csr */
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

uintptr_t midgard_insert_vma(struct midgard_node **root, uintptr_t va_base, phys_addr_t size, uint8_t prot, bool update_csr) {
	uintptr_t midgard_addr = 0xffaf100000000000 | ((midgard_addr_counter++) << (8 * 4)) | (va_base & 0xfff);

	return midgard_insert_specified_vma(root, midgard_addr, va_base, size, prot, update_csr);
}

/* 打印指定数量的缩进 */
static void print_indent(int level) {
	for (int i = 0; i < level; i++) {
		printk(KERN_CONT "    "); // 每一层增加 4 个空格作为缩进
	}
}

/* 打印当前节点的信息 */
static void midgard_print_node_debug(struct midgard_node *node, int level) {
	if (!node) return;

	/* 打印当前节点所在的层级 */
	print_indent(level);
	printk(KERN_CONT "Level %d: Node at %016llx, is_leaf = %d, key_cnt = %d\n", 
		level, (uint64_t)node, node->is_leaf, node->key_cnt);

	/* 打印当前节点的所有键值 */
	for (int i = 0; i < node->key_cnt; i++) {
		print_indent(level + 1);
		printk(KERN_CONT "  Key %d: base = %016llx, bound = %016llx, offset = %016llx\n", 
			i, node->keys[i].base, node->keys[i].bound, node->keys[i].offset);
	}

	/* 如果是非叶节点，递归打印其子节点 */
	if (!node->is_leaf) {
		for (int i = 0; i <= node->key_cnt; i++) {
			print_indent(level + 1);
			printk(KERN_CONT "Child %d:\n", i);
			midgard_print_node_debug(node->children[i], level + 2);
		}
	}
}

/* 打印整棵 B 树 */
void midgard_print(struct midgard_node *root) {
	pr_err("Starting B-tree debug output...\n");
	midgard_print_node_debug(root, 0);
	pr_err("B-tree debug output completed.\n");
}
