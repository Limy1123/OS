#include "mmu.h"
#include "pmap.h"
#include "printf.h"
#include "env.h"
#include "error.h"


/* 这些变量由 mips_detect_memory() 设置 */
u_long maxpa;            /* 最大物理地址 */
u_long npage;            /* 内存页数 */
u_long basemem;          /* 基本内存大小（以字节为单位） */
u_long extmem;           /* 扩展内存大小（以字节为单位） */

Pde *boot_pgdir;

struct Page *pages;
static u_long freemem;

static struct Page_list page_free_list; /* 物理页面的空闲列表 */


/* 概述：
    初始化 basemem 和 npage。
    将 basemem 设置为 64MB，并计算相应的 npage 值。*/
void mips_detect_memory()
{
    /* 步骤 1：初始化 basemem。
     * （在实际计算机上，CMOS 告诉我们有多少千字节的内存）。 */
    basemem = 64 * 1024 * 1024;

    /* 步骤 2：计算相应的 npage 值。 */
    npage = basemem / BY2PG;

    printf("Physical memory: %dK available, ", (int)(maxpa / 1024));
    printf("base = %dK, extended = %dK\n", (int)(basemem / 1024),
           (int)(extmem / 1024));
}

/* 概述：
    分配 `n` 字节的物理内存，具有 `align` 对齐，如果 `clear` 设置，则清除已分配的内存。
    此分配器仅在设置虚拟内存系统时使用。

   后置条件：
    如果内存不足，应 panic，否则返回已分配内存的地址。*/
static void *alloc(u_int n, u_int align, int clear)
{
    extern char end[];
    u_long alloced_mem;

    /* 如果这是第一次初始化 `freemem`。第一个虚拟地址是链接器未分配给任何内核代码或全局变量的地址。 */
    if (freemem == 0) {
        freemem = (u_long)end;
    }

    /* 步骤 1：将 `freemem` 向上取整以正确对齐 */
    freemem = ROUND(freemem, align);

    /* 步骤 2：将当前 `freemem` 值保存为已分配块 */
    alloced_mem = freemem;

    /* 步骤 3：增加 `freemem` 以记录分配 */
    freemem += n;

    /* 步骤 4：如果参数 `clear` 设置，则清除已分配块 */
    if (clear) {
        bzero((void *)alloced_mem, n);
    }

    // 内存不足，PANIC !!
    if (PADDR(freemem) >= maxpa) {
        panic("out of memory\n");
        return (void *)-E_NO_MEM;
    }

    /* 步骤 5：返回已分配块 */
    return (void *)alloced_mem;
}

/* 概述：
    获取给定页目录 `pgdir` 中虚拟地址 `va` 的页表条目。
    如果页表不存在且参数 `create` 设置为 1，则创建它。*/
static Pte *boot_pgdir_walk(Pde *pgdir, u_long va, int create)
{
    Pde *pgdir_entryp;
    Pte *pgtable, *pgtable_entry;

    /* 步骤 1：获取相应的页目录条目和页表 */
    /* 提示：使用 KADDR 和 PTE_ADDR 从页目录条目值中获取页表。 */
    pgdir_entryp = &pgdir[PDX(va)];

    if (!(*pgdir_entryp & PTE_V)) {
        if (!create) {
            return NULL;
        }

        struct Page *ppage;
        if (page_alloc(&ppage) < 0) {
            return NULL;
        }
        ppage->pp_ref++;
        *pgdir_entryp = page2pa(ppage) | PTE_V;
    }

    pgtable = (Pte *)KADDR(PTE_ADDR(*pgdir_entryp));

    /* 步骤 2：如果相应的页表不存在且参数 `create` 设置，则创建一个。并为此新页表设置正确的权限位。 */
    pgtable_entry = &pgtable[PTX(va)];

    /* 步骤 3：获取 `va` 的页表条目，并返回它。 */
    return pgtable_entry;
}

/* 概述：
    将虚拟地址空间 [va, va+size) 映射到页目录根 pgdir 中的物理地址空间 [pa, pa+size)。
    使用权限位 `perm|PTE_V` 进行条目映射。

  前置条件：
    大小是 BY2PG 的倍数。*/
void boot_map_segment(Pde *pgdir, u_long va, u_long size, u_long pa, int perm)
{
    int i;
    Pte *pgtable_entry;

    /* 步骤 1：检查 `size` 是否为 BY2PG 的倍数。 */
    assert(size % BY2PG == 0);

    /* 步骤 2：将虚拟地址空间映射到物理地址。 */
    /* 提示：使用 `boot_pgdir_walk` 获取虚拟地址 `va` 的页表条目。 */
    for (i = 0; i < size; i += BY2PG) {
        pgtable_entry = boot_pgdir_walk(pgdir, va + i, 1);
        *pgtable_entry = (pa + i) | perm | PTE_V;
    }
}

/* 概述：
    设置两级页表。

   提示：
    你可以在 include/mmu.h 中获取有关 `UPAGES` 和 `UENVS` 的更多详细信息。*/
void mips_vm_init()
{
    extern char end[];
    extern int mCONTEXT;
    extern struct Env *envs;

    Pde *pgdir;
    u_int n;

    /* 步骤 1：为页目录（第一级页表）分配一页。 */
    pgdir = alloc(BY2PG, BY2PG, 1);
    printf("to memory %x for struct page directory.\n", freemem);
    mCONTEXT = (int)pgdir;

    boot_pgdir = pgdir;

    /* 步骤 2：为全局数组 `pages` 分配适当大小的物理内存，用于物理内存管理。然后，将虚拟地址 `UPAGES` 映射到之前分配的物理地址 `pages`。为了考虑对齐，你应该在映射之前向上取整内存大小。 */
    pages = (struct Page *)alloc(npage * sizeof(struct Page), BY2PG, 1);
    printf("to memory %x for struct Pages.\n", freemem);
    n = ROUND(npage * sizeof(struct Page), BY2PG);
    boot_map_segment(pgdir, UPAGES, n, PADDR(pages), PTE_R);

    /* 步骤 3，为全局数组 `envs` 分配适当大小的物理内存，用于进程管理。然后将物理地址映射到 `UENVS`。 */
    envs = (struct Env *)alloc(NENV * sizeof(struct Env), BY2PG, 1);
    n = ROUND(NENV * sizeof(struct Env), BY2PG);
    boot_map_segment(pgdir, UENVS, n, PADDR(envs), PTE_R);

    printf("pmap.c:\t mips vm init success\n");
}

/* 概述：
    初始化页面结构和内存空闲列表。
    `pages` 数组对每个物理页面都有一个 `struct Page` 条目。页面是引用计数的，空闲页面保存在链表中。
  提示：
    使用 `LIST_INSERT_HEAD` 将某些内容插入链表。*/
void page_init(void)
{
    /* 步骤 1：初始化 page_free_list。 */
    /* 提示：使用 include/queue.h 中定义的宏 `LIST_INIT`。 */
    LIST_INIT(&page_free_list);

    /* 步骤 2：将 `freemem` 向上取整为 BY2PG 的倍数。 */
    freemem = ROUND(freemem, BY2PG);

    /* 步骤 3：将 `freemem` 以下的所有内存标记为已使用（将 `pp_ref` 字段设置为 1）。 */
    int i;
    for (i = 0; i < freemem / BY2PG; i++) {
        pages[i].pp_ref = 1;
    }

    /* 步骤 4：将其他内存标记为空闲。 */
    for (i = freemem / BY2PG; i < npage; i++) {
        pages[i].pp_ref = 0;
        LIST_INSERT_HEAD(&page_free_list, &pages[i], pp_link);
    }
}

/* 概述：
    从空闲内存中分配一个物理页面，并清除此页面。

  后置条件：
    如果分配新页面失败（内存不足，没有空闲页面），返回 -E_NO_MEM。
    否则，将已分配页面的地址设置为 *pp，并返回 0。

  注意：
    不会增加页面的引用计数 - 如果有必要，调用者必须执行这些操作（显式或通过 page_insert）。

  提示：
    使用 include/queue.h 中定义的 LIST_FIRST 和 LIST_REMOVE。*/
int page_alloc(struct Page **pp)
{
    struct Page *ppage_temp;

    /* 步骤 1：从空闲内存中获取一个页面。如果失败，返回错误代码。 */
    if (LIST_EMPTY(&page_free_list)) {
        return -E_NO_MEM;
    }

    *pp = LIST_FIRST(&page_free_list);
    LIST_REMOVE(*pp, pp_link);

    /* 步骤 2：初始化此页面。
     * 提示：使用 `bzero`。 */
    bzero(page2kva(*pp), BY2PG);
    (*pp)->pp_ref = 1;

    return 0;
}

/* 概述：
    释放一个页面，如果它的 `pp_ref` 达到 0，则将其标记为空闲。
  提示：
    释放页面时，只需将其插入 page_free_list。*/
void page_free(struct Page *pp)
{
    /* 步骤 1：如果仍有虚拟地址引用此页面，则不做任何操作。 */
    if (pp->pp_ref > 0) {
        return;
    }

    /* 步骤 2：如果 `pp_ref` 达到 0，则将此页面标记为空闲并返回。 */
    if (--pp->pp_ref == 0) {
        LIST_INSERT_HEAD(&page_free_list, pp, pp_link);
    }

    /* 如果 `pp_ref` 的值小于 0，则之前一定发生了一些错误，所以 PANIC !!! */
    if (pp->pp_ref < 0) {
        panic("cgh:pp->pp_ref is less than zero\n");
    }
}

/* 概述：
    给定 `pgdir`，一个指向页目录的指针，pgdir_walk 返回虚拟地址 'va' 的页表条目（具有权限 PTE_R|PTE_V）。

  前置条件：
    `pgdir` 应该是两级页表结构。

  后置条件：
    如果内存不足，返回 -E_NO_MEM。
    否则，我们成功获取页表条目，将页表条目的值存储到 *ppte，并返回 0，表示成功。

  提示：
    我们使用两级指针存储页表条目，并返回状态代码以指示此函数是否成功执行。
    此函数与 `boot_pgdir_walk` 函数有相似之处。*/
int pgdir_walk(Pde *pgdir, u_long va, int create, Pte **ppte)
{
    Pde *pgdir_entryp;
    Pte *pgtable;
    struct Page *ppage;

    /* 步骤 1：获取相应的页目录条目和页表。 */
    pgdir_entryp = &pgdir[PDX(va)];
    if (!(*pgdir_entryp & PTE_V)) {
        if (!create) {
            return -E_NO_MEM;
        }
        if (page_alloc(&ppage) < 0) {
            return -E_NO_MEM;
        }
        ppage->pp_ref++;
        *pgdir_entryp = page2pa(ppage) | PTE_V;
    }

    pgtable = (Pte *)KADDR(PTE_ADDR(*pgdir_entryp));

    /* 步骤 2：如果相应的页表不存在（有效）且参数 `create` 设置，则创建一个。并为此新页表设置正确的权限位。
     * 创建新页表时，可能会内存不足。 */
    *ppte = &pgtable[PTX(va)];

    /* 步骤 3：将页表条目设置为 `*ppte` 作为返回值。 */
    return 0;
}

/* 概述：
    将物理页面 'pp' 映射到虚拟地址 'va'。
    页表条目的权限（低 12 位）应设置为 'perm|PTE_V'。

  后置条件：
    成功时返回 0
    如果无法分配页表，则返回 -E_NO_MEM

  提示：
    如果 `va` 处已有页面映射，则调用 page_remove() 释放此映射。
    如果插入成功，则应增加 `pp_ref`。*/
int page_insert(Pde *pgdir, struct Page *pp, u_long va, u_int perm)
{
    u_int PERM;
    Pte *pgtable_entry;
    PERM = perm | PTE_V;

    /* 步骤 1：获取相应的页表条目。 */
    if (pgdir_walk(pgdir, va, 1, &pgtable_entry) < 0) {
        return -E_NO_MEM;
    }

    if (pgtable_entry != 0 && (*pgtable_entry & PTE_V) != 0) {
        if (pa2page(*pgtable_entry) != pp) {
            page_remove(pgdir, va);
        } else {
            tlb_invalidate(pgdir, va);
            *pgtable_entry = (page2pa(pp) | PERM);
            return 0;
        }
    }

    /* 步骤 2：更新 TLB。 */
    /* 提示：使用 tlb_invalidate 函数 */
    tlb_invalidate(pgdir, va);

    /* 步骤 3：执行检查，重新获取页表条目以验证插入。 */
    /* 步骤 3.1 检查页面是否可以插入，如果不能返回 -E_NO_MEM */
    if (pgdir_walk(pgdir, va, 1, &pgtable_entry) < 0) {
        return -E_NO_MEM;
    }

    /* 步骤 3.2 插入页面并增加 pp_ref */
    *pgtable_entry = (page2pa(pp) | PERM);
    pp->pp_ref++;

    return 0;
}

/* 概述：
    查找虚拟地址 `va` 映射到的页面。

  后置条件：
    返回指向相应页面的指针，并将其页表条目存储到 *ppte。
    如果 `va` 未映射到任何页面，则返回 NULL。*/
struct Page *page_lookup(Pde *pgdir, u_long va, Pte **ppte)
{
    struct Page *ppage;
    Pte *pte;

    /* 步骤 1：获取页表条目。 */
    if (pgdir_walk(pgdir, va, 0, &pte) < 0) {
        return NULL;
    }

    /* 提示：检查页表条目是否不存在或无效。 */
    if (pte == 0 || (*pte & PTE_V) == 0) {
        return NULL;
    }

    /* 步骤 2：获取相应的 Page 结构体。 */
    /* 提示：使用 include/pmap.h 中定义的 `pa2page` 函数。 */
    ppage = pa2page(*pte);
    if (ppte) {
        *ppte = pte;
    }

    return ppage;
}

// 概述：
//  减少 Page `*pp` 的 `pp_ref` 值，如果 `pp_ref` 达到 0，则释放此页面。
void page_decref(struct Page *pp) {
    if (--pp->pp_ref == 0) {
        page_free(pp);
    }
}

// 概述：
//  取消映射虚拟地址 `va` 处的物理页面。
void page_remove(Pde *pgdir, u_long va)
{
    Pte *pagetable_entry;
    struct Page *ppage;

    /* 步骤 1：获取页表条目，并检查页表条目是否有效。 */
    ppage = page_lookup(pgdir, va, &pagetable_entry);

    if (ppage == 0) {
        return;
    }

    /* 步骤 2：减少 `pp_ref` 并决定是否需要释放此页面。 */
    /* 提示：当没有虚拟地址映射到此页面时，释放它。 */
    if (--ppage->pp_ref == 0) {
        page_free(ppage);
    }

    /* 步骤 3：更新 TLB。 */
    *pagetable_entry = 0;
    tlb_invalidate(pgdir, va);
}

// 概述：
//  更新 TLB。
void tlb_invalidate(Pde *pgdir, u_long va)
{
    if (curenv) {
        tlb_out(PTE_ADDR(va) | GET_ENV_ASID(curenv->env_id));
    } else {
        tlb_out(PTE_ADDR(va));
    }
}

void physical_memory_manage_check(void)
{
    struct Page *pp, *pp0, *pp1, *pp2;
    struct Page_list fl;
    int *temp;

    // 应该能够分配三个页面
    pp0 = pp1 = pp2 = 0;
    assert(page_alloc(&pp0) == 0);
    assert(page_alloc(&pp1) == 0);
    assert(page_alloc(&pp2) == 0);

    assert(pp0);
    assert(pp1 && pp1 != pp0);
    assert(pp2 && pp2 != pp1 && pp2 != pp0);

    // 暂时窃取剩余的空闲页面
    fl = page_free_list;
    // 现在这个 page_free_list 必须为空!!!!
    LIST_INIT(&page_free_list);
    // 应该没有空闲内存
    assert(page_alloc(&pp) == -E_NO_MEM);

    temp = (int*)page2kva(pp0);
    // 将 1000 写入 pp0
    *temp = 1000;
    // 释放 pp0
    page_free(pp0);
    printf("The number in address temp is %d\n", *temp);

    // 重新分配
    assert(page_alloc(&pp0) == 0);
    assert(pp0);

    // pp0 不应该改变
    assert(temp == (int*)page2kva(pp0));
    // pp0 应该为零
    assert(*temp == 0);

    page_free_list = fl;
    page_free(pp0);
    page_free(pp1);
    page_free(pp2);
    struct Page_list test_free;
    struct Page *test_pages;
    test_pages = (struct Page *)alloc(10 * sizeof(struct Page), BY2PG, 1);
    LIST_INIT(&test_free);
    //LIST_FIRST(&test_free) = &test_pages[0];
    int i, j = 0;
    struct Page *p, *q;
    //测试尾部插入
    for (i = 0; i < 10; i++) {
        test_pages[i].pp_ref = i;
        //test_pages[i].pp_link=NULL;
        //printf("0x%x  0x%x\n",&test_pages[i], test_pages[i].pp_link.le_next);
        LIST_INSERT_TAIL(&test_free, &test_pages[i], pp_link);
        //printf("0x%x  0x%x\n",&test_pages[i], test_pages[i].pp_link.le_next);
    }
    p = LIST_FIRST(&test_free);
    int answer1[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    assert(p != NULL);
    while (p != NULL) {
        //printf("%d %d\n",p->pp_ref,answer1[j]);
        assert(p->pp_ref == answer1[j++]);
        //printf("ptr: 0x%x v: %d\n",(p->pp_link).le_next,((p->pp_link).le_next)->pp_ref);
        p = LIST_NEXT(p, pp_link);
    }
    // 插入后测试
    int answer2[] = {0, 1, 2, 3, 4, 20, 5, 6, 7, 8, 9};
    q = (struct Page *)alloc(sizeof(struct Page), BY2PG, 1);
    q->pp_ref = 20;

    //printf("---%d\n",test_pages[4].pp_ref);
    LIST_INSERT_AFTER(&test_pages[4], q, pp_link);
    //printf("---%d\n",LIST_NEXT(&test_pages[4],pp_link)->pp_ref);
    p = LIST_FIRST(&test_free);
    j = 0;
    //printf("into test\n");
    while (p != NULL) {
        //printf("%d %d\n",p->pp_ref,answer2[j]);
        assert(p->pp_ref == answer2[j++]);
        p = LIST_NEXT(p, pp_link);
    }

    printf("physical_memory_manage_check() succeeded\n");
}

void page_check(void)
{
    struct Page *pp, *pp0, *pp1, *pp2;
    struct Page_list fl;

    // 应该能够分配三个页面
    pp0 = pp1 = pp2 = 0;
    assert(page_alloc(&pp0) == 0);
    assert(page_alloc(&pp1) == 0);
    assert(page_alloc(&pp2) == 0);

    assert(pp0);
    assert(pp1 && pp1 != pp0);
    assert(pp2 && pp2 != pp1 && pp2 != pp0);

    // 暂时窃取剩余的空闲页面
    fl = page_free_list;
    // 现在这个 page_free_list 必须为空!!!!
    LIST_INIT(&page_free_list);

    // 应该没有空闲内存
    assert(page_alloc(&pp) == -E_NO_MEM);

    // 没有空闲内存，因此我们不能分配页表
    assert(page_insert(boot_pgdir, pp1, 0x0, 0) < 0);

    // 释放 pp0 并重试：pp0 应该用于页表
    page_free(pp0);
    assert(page_insert(boot_pgdir, pp1, 0x0, 0) == 0);
    assert(PTE_ADDR(boot_pgdir[0]) == page2pa(pp0));

    printf("va2pa(boot_pgdir, 0x0) is %x\n", va2pa(boot_pgdir, 0x0));
    printf("page2pa(pp1) is %x\n", page2pa(pp1));

    assert(va2pa(boot_pgdir, 0x0) == page2pa(pp1));
    assert(pp1->pp_ref == 1);

    // 应该能够在 BY2PG 处映射 pp2，因为 pp0 已经分配用于页表
    assert(page_insert(boot_pgdir, pp2, BY2PG, 0) == 0);
    assert(va2pa(boot_pgdir, BY2PG) == page2pa(pp2));
    assert(pp2->pp_ref == 1);

    // 应该没有空闲内存
    assert(page_alloc(&pp) == -E_NO_MEM);

    printf("start page_insert\n");
    // 应该能够在 BY2PG 处映射 pp2，因为它已经在那里
    assert(page_insert(boot_pgdir, pp2, BY2PG, 0) == 0);
    assert(va2pa(boot_pgdir, BY2PG) == page2pa(pp2));
    assert(pp2->pp_ref == 1);

    // pp2 不应该在空闲列表中
    // 如果在 page_insert 中处理引用计数时粗心大意，可能会发生这种情况
    assert(page_alloc(&pp) == -E_NO_MEM);

    // 不应该能够在 PDMAP 处映射，因为需要为页表提供空闲页面
    assert(page_insert(boot_pgdir, pp0, PDMAP, 0) < 0);

    // 在 BY2PG 处插入 pp1（替换 pp2）
    assert(page_insert(boot_pgdir, pp1, BY2PG, 0) == 0);

    // 应该在 0 和 BY2PG 处都有 pp1，pp2 不在任何地方，...
    assert(va2pa(boot_pgdir, 0x0) == page2pa(pp1));
    assert(va2pa(boot_pgdir, BY2PG) == page2pa(pp1));
    // ... 并且引用计数应反映这一点
    assert(pp1->pp_ref == 2);
    printf("pp2->pp_ref %d\n", pp2->pp_ref);
    assert(pp2->pp_ref == 0);
    printf("end page_insert\n");

    // pp2 应该由 page_alloc 返回
    assert(page_alloc(&pp) == 0 && pp == pp2);

    // 在 0 处取消映射 pp1 应该保持 pp1 在 BY2PG
    page_remove(boot_pgdir, 0x0);
    assert(va2pa(boot_pgdir, 0x0) == ~0);
    assert(va2pa(boot_pgdir, BY2PG) == page2pa(pp1));
    assert(pp1->pp_ref == 1);
    assert(pp2->pp_ref == 0);

    // 在 BY2PG 处取消映射 pp1 应该释放它
    page_remove(boot_pgdir, BY2PG);
    assert(va2pa(boot_pgdir, 0x0) == ~0);
    assert(va2pa(boot_pgdir, BY2PG) == ~0);
    assert(pp1->pp_ref == 0);
    assert(pp2->pp_ref == 0);

    // 因此，它应该由 page_alloc 返回
    assert(page_alloc(&pp) == 0 && pp == pp1);

    // 应该没有空闲内存
    assert(page_alloc(&pp) == -E_NO_MEM);

    // 强行取回 pp0
    assert(PTE_ADDR(boot_pgdir[0]) == page2pa(pp0));
    boot_pgdir[0] = 0;
    assert(pp0->pp_ref == 1);
    pp0->pp_ref = 0;

    // 归还空闲列表
    page_free_list = fl;

    // 释放我们占用的页面
    page_free(pp0);
    page_free(pp1);
    page_free(pp2);

    printf("page_check() succeeded!\n");
}

void pageout(int va, int context)
{
    u_long r;
    struct Page *p = NULL;

    if (context < 0x80000000) {
        panic("tlb refill and alloc error!");
    }

    if ((va > 0x7f400000) && (va < 0x7f800000)) {
        panic(">>>>>>>>>>>>>>>>>>>>>>it's env's zone");
    }

    if (va < 0x10000) {
        panic("^^^^^^TOO LOW^^^^^^^^^");
    }

    if ((r = page_alloc(&p)) < 0) {
        panic("page alloc error!");
    }

    p->pp_ref++;

    page_insert((Pde *)context, p, VA2PFN(va), PTE_R);
    printf("pageout:\t@@@___0x%x___@@@  ins a page \n", va);
}
