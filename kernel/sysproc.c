#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "slab.h"
#include "buddy.h"
#include "vm.h"

extern pagetable_t kernel_pagetable;
void* uva2kva(pagetable_t pagetable, uint64 uva)
{
  pte_t *pte;

  pte = walk(pagetable, uva, 0);
  if(pte == 0)
    return 0;

  if((*pte & PTE_V) == 0)
    return 0;

  uint64 pa = PTE2PA(*pte);

  return (void*)(pa + KERNBASE + (uva & (PGSIZE - 1)));
}

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_kmem_init(void)
{
  return 0;
}

uint64
sys_kmem_cache_create(void)
{
   char name[32];
    int size;
    uint64 ctor_uva, dtor_uva;
    struct proc* p = myproc();
    if(argstr(0, name, sizeof(name)) < 0) return -1;
    argint(1, &size);
    argaddr(2, &ctor_uva);
    argaddr(3, &dtor_uva);
    void* ctor = 0, *dtor = 0;
 
    if (ctor_uva != 0)
    {
      ctor = uva2kva(p->pagetable,ctor_uva);
      ctor = (void*)walkaddr(p->pagetable, (uint64)ctor);
    }
    if (dtor_uva != 0)
    {
       dtor = uva2kva(p->pagetable,dtor_uva);
       dtor = (void*)walkaddr(p->pagetable, (uint64)dtor);
    }

    kmem_cache_t *cache = kmem_cache_create(name, size, (void(*)(void*))ctor, (void(*)(void*))dtor);

    if(cache == 0)
        return -1;

    return (uint64)cache;
}

uint64
sys_kmem_cache_alloc(void)
{
   uint64 cache_addr;
	struct proc *p = myproc();
    argaddr(0, &cache_addr);

    kmem_cache_t *cache = (kmem_cache_t*)cache_addr;

    void *obj = kmem_cache_alloc(cache);

	  uint64 kva = PGROUNDDOWN((uint64)obj);
    uint64 pa  = kva - KERNBASE;
    uint64 uva = pa;

    pte_t *pte = walk(p->pagetable, uva, 0);

    if(pte == 0 || (*pte & PTE_V) == 0)
    {
        mappages(
            p->pagetable,
            uva,
            BLOCK_SIZE,
            pa,
            PTE_R | PTE_W | PTE_U
        );
    }

    return uva + ((uint64)obj & (BLOCK_SIZE - 1));
}

uint64
sys_kmem_cache_free(void)
{
      uint64 cache_addr;
    uint64 obj_uva;

    argaddr(0, &cache_addr);
    argaddr(1, &obj_uva);

    kmem_cache_t *cache = (kmem_cache_t*)cache_addr;

    void *obj_kva = (void *)(obj_uva + KERNBASE);

    kmem_cache_free(cache, obj_kva);

    return 0;
}
uint64
sys_kmem_cache_shrink(void)
{
  uint64 cachep;

  argaddr(0, &cachep);

  if (cachep == 0) return 0;

  return (uint64)kmem_cache_shrink((kmem_cache_t*)cachep);
}

uint64
sys_kmalloc(void)
{
  int size;
	struct proc *p = myproc();
    argint(0, &size);

    void *obj = kmalloc(size);
	 uint64 kva = PGROUNDDOWN((uint64)obj);
    uint64 pa  = kva - KERNBASE;
    uint64 uva = pa;

    pte_t *pte = walk(p->pagetable, uva, 0);

    if(pte == 0 || (*pte & PTE_V) == 0)
    {
        mappages(
            p->pagetable,
            uva,
            BLOCK_SIZE,
            pa,
            PTE_R | PTE_W | PTE_U
        );
    }

    return uva + ((uint64)obj & (BLOCK_SIZE - 1));
}
uint64
sys_kkfree(void)
{
    uint64 obj_uva;

    argaddr(1, &obj_uva);

    void *obj_kva = (void *)(obj_uva + KERNBASE);

    kkfree(obj_kva);

    return 0;
}
uint64
sys_kmem_cache_destroy(void)
{
  uint64 cachep;

  argaddr(0, &cachep);

  if(cachep == 0)
    return 0;

  kmem_cache_t *cache = (kmem_cache_t*)cachep;

  if(cache->ctor)
  {
      uint64 kva = PGROUNDDOWN((uint64)cache->ctor);
      uvmunmap(kernel_pagetable, kva, 1, 0);
  }

  if(cache->dtor)
  {
      uint64 kva = PGROUNDDOWN((uint64)cache->dtor);
      uvmunmap(kernel_pagetable, kva, 1, 0);
  }

  kmem_cache_destroy(cache);

  return 1;
}

uint64
sys_kmem_cache_info(void)
{
  uint64 cachep;

  argaddr(0, &cachep);

  if (cachep == 0) return 0;

  kmem_cache_info((kmem_cache_t*)cachep);

  return 1;
}

uint64
sys_kmem_cache_error(void)
{
  uint64 cachep;

  argaddr(0, &cachep);

  if (cachep <= 0) return 0;

  return  (uint64)kmem_cache_error((kmem_cache_t*)cachep);
}
