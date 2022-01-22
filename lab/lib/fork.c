// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;
	//cprintf("enter pgfault with va: %08x\n", (int)addr);
	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.

	addr = (void*) ROUNDDOWN((int)addr, PGSIZE);
	unsigned int pn = (unsigned int)addr >> 12;
	int perm = uvpt[pn] & 0xfff;
	if((err & FEC_WR) == 0) panic("not a write page fault.\n");
	if((perm & PTE_COW) == 0) panic("not a COW page.\n");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	int cur_env_id = sys_getenvid();
	r = sys_page_alloc(cur_env_id, (void*)PFTEMP, PTE_P | PTE_U | PTE_W);
	if(r < 0) panic("%e\n", r);
	memmove((void*)PFTEMP, addr, PGSIZE);
	r = sys_page_map(cur_env_id, (void*)PFTEMP, cur_env_id, addr, PTE_P | PTE_U | PTE_W);
	if(r < 0) panic("%e\n", r);
	r = sys_page_unmap(cur_env_id, (void*)PFTEMP);
	if(r < 0) panic("%e\n", r);
	return;
	//panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	int perm;
	int src_perm = uvpt[pn] & PTE_SYSCALL;
	if(src_perm & PTE_SHARE){
		r = sys_page_map(thisenv->env_id, (void*)(pn * PGSIZE), envid, (void*)(pn * PGSIZE), src_perm);
		//cprintf("1 shared page.\n");
		if(r < 0) panic("%e\n", r);
	}else if((src_perm & PTE_W) || (src_perm & PTE_COW)){
		perm = (src_perm | PTE_COW) & (~PTE_W);
		r = sys_page_map(thisenv->env_id, (void*)(pn * PGSIZE), envid, (void*)(pn * PGSIZE), perm);
		if(r < 0) panic("%e\n", r);
		r = sys_page_map(thisenv->env_id, (void*)(pn * PGSIZE), thisenv->env_id, (void*)(pn * PGSIZE), perm);
		if(r < 0) panic("%e\n", r);
	}else{
		r = sys_page_map(thisenv->env_id, (void*)(pn * PGSIZE), envid, (void*)(pn * PGSIZE), src_perm);
		if(r < 0) panic("%e\n", r);
	}
	//panic("duppage not implemented");
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	//panic("fork not implemented");
	set_pgfault_handler(pgfault);
	int r;
	int child_id;
	r = sys_exofork();
	if(r < 0) panic("%e\n", r);
	else if(r > 0){//parent env
		child_id = r;
		extern unsigned char end[];
		int start_pn = (unsigned int)UTEXT >> 12;
		int end_pn = (unsigned int)end >> 12;
		duppage(child_id, (unsigned int)(USTACKTOP - PGSIZE) >> 12);
		//cprintf("finished copying user normal stack\n");
		/*for(int i = start_pn; i <= end_pn; i++){
			r = duppage(child_id, i);
			if(r < 0) panic("%e\n", r);
			//cprintf("copied one page\n");
		}*/
		for(unsigned int pn = 0; pn < PGNUM(USTACKTOP - PGSIZE); pn++){
			while(!(uvpd[pn >> 10] & PTE_P)) pn += 1024;
			if(uvpt[pn] & PTE_P) {
				r = duppage(child_id, pn);
				if(r < 0) panic("%e\n", r);
			}
		}
		//cprintf("finished copying UTEXT to end\n");
		r = sys_page_alloc(child_id, (void*)(UXSTACKTOP - PGSIZE), PTE_U | PTE_P | PTE_W);
		if(r < 0) panic("%e\n", r);
		extern void _pgfault_upcall(void);
		r = sys_env_set_pgfault_upcall(child_id, _pgfault_upcall);
		if(r < 0) panic("%e\n", r);
		r = sys_env_set_status(child_id, ENV_RUNNABLE);
		if(r < 0) panic("%e\n", r);
		return child_id;
	}else{//child env
		int env_id = sys_getenvid() & (NENV - 1);
		thisenv = (struct Env*)envs + env_id;
		return 0;
	}
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
