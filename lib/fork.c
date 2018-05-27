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

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if (!(err & FEC_WR))
		panic("pgfault: not a write access, error code %x", err);
	if (!(uvpt[PGNUM(addr)] & PTE_COW))
		panic("pgfault: not a copy-on-write page, address %p", addr);

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	r = sys_page_alloc(0, PFTEMP, PTE_P | PTE_U | PTE_W);
	if (r < 0)
		panic("pgfault: error when allocating a new page, %e", r);
	void *base = (void *) ROUNDDOWN((uintptr_t) addr, PGSIZE);
	memcpy((void *) PFTEMP, base, PGSIZE);
	r = sys_page_map(0, (void *) PFTEMP, 0, base, PTE_P | PTE_U | PTE_W);
	if (r < 0)
		panic("pgfault: error when moving the new page, %e", r);
	r = sys_page_unmap(0, (void *) PFTEMP);
	if (r < 0)
		panic("pgfault: error when unmapping the temporary page, %e", r);
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
	if (uvpt[pn] & PTE_SHARE) {
		r = sys_page_map(0, (void *) (pn * PGSIZE),
				 envid, (void *) (pn * PGSIZE),
				 uvpt[pn] & PTE_SYSCALL);
		if (r < 0)
			panic("duppage: env 0 -> env %d va %08p perm %03x, error: %e",
			      envid, pn * PGSIZE, uvpt[pn] & 0xFFF, r);
	} else if (uvpt[pn] & (PTE_W | PTE_COW)) {
		r = sys_page_map(0, (void *) (pn * PGSIZE),
				 envid, (void *) (pn * PGSIZE),
				 PTE_P | PTE_U | PTE_COW);
		if (r < 0)
			panic("duppage: env 0 -> env %d va %08p perm %03x, error: %e",
			      envid, pn * PGSIZE, uvpt[pn] & 0xFFF, r);
		r = sys_page_map(0, (void *) (pn * PGSIZE),
				 0, (void *) (pn * PGSIZE),
				 PTE_P | PTE_U | PTE_COW);
		if (r < 0)
			panic("duppage: env 0 -> env 0 va %08p perm %03x, error: %e",
			      pn * PGSIZE, uvpt[pn] & 0xFFF, r);
	} else {
		r = sys_page_map(0, (void *) (pn * PGSIZE),
				 envid, (void *) (pn * PGSIZE),
				 uvpt[pn] & PTE_SYSCALL);
		if (r < 0)
			panic("duppage: env 0 -> env %d va %08p perm %03x, error: %e",
			      envid, pn * PGSIZE, uvpt[pn] & 0xFFF, r);
	}

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
	// Set up page fault handler
	set_pgfault_handler(pgfault);

	// Create a child
	envid_t envid = sys_exofork();
	if (envid < 0)
		return envid;
	if (envid == 0) {
		thisenv = envs + ENVX(sys_getenvid());
		return 0;
	}

	// It is in parent process now
	size_t i;
	for (i = 0; i < UTOP; i += PGSIZE) {
		// skip user exception stack
		if (i == UXSTACKTOP - PGSIZE)
			continue;

		if ((uvpd[PDX(i)] & PTE_P)
		    && (uvpt[PGNUM(i)] & PTE_P)
		    && (uvpt[PGNUM(i)] & PTE_U))
			duppage(envid, PGNUM(i));
	}

	// Set page-fault handler for child process
	int ret;
	ret = sys_page_alloc(envid, (void *) UXSTACKTOP - PGSIZE,
			     PTE_P | PTE_U | PTE_W);
	if (ret < 0)
		panic("fork: error when allocating user exception stack for env %d, %e", envid, ret);
	ret = sys_env_set_pgfault_upcall(envid, thisenv->env_pgfault_upcall);
	if (ret < 0)
		panic("fork: error when setting pgfault_upcall for env %d, %e", envid, ret);
	ret = sys_env_set_status(envid, ENV_RUNNABLE);
	if (ret < 0)
		panic("fork: error when setting env %d runnable, %e", envid, ret);
	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
