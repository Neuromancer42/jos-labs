// User-level page fault handler support.
// Rather than register the C page fault handler directly with the
// kernel as the page fault handler, we register the assembly language
// wrapper in pfentry.S, which in turns calls the registered C
// function.

#include <inc/lib.h>


// Assembly language pgfault entrypoint defined in lib/pfentry.S.
extern void _pgfault_upcall(void);

// Pointer to currently installed C-language pgfault handler.
void (*_exception_handler[32])(struct UTrapframe *utf);
int _pass_times = 0;

//
// Set the page fault handler function.
// If there isn't one yet, _pgfault_handler will be 0.
// The first time we register a handler, we need to
// allocate an exception stack (one page of memory with its top
// at UXSTACKTOP), and tell the kernel to call the assembly-language
// _pgfault_upcall routine when a page fault occurs.
//
void
set_pgfault_handler(void (*handler)(struct UTrapframe *utf))
{
	int r;

	if (_pass_times == 0) {
		_pass_times = 1;
		// First time through!
		// LAB 4: Your code here.
		int err;
		err = sys_page_alloc(0, (void *) UXSTACKTOP - PGSIZE,
				     PTE_W | PTE_U | PTE_P);
		if (err < 0)
			panic("set_pgfault_handler: when allocating a page, %e", err);

		err = sys_env_set_pgfault_upcall(0, _pgfault_upcall);

		if (err < 0)
			panic("set_pgfault_hanlder: when setting pagefault upcall, %e", err);
	}

	// Save handler pointer for assembly to call.
	_exception_handler[T_PGFLT] = handler;
}

void
set_exception_handler(uint32_t trapno, void (*handler)(struct UTrapframe *utf))
{
	int r;
	if (_pass_times == 0) {
		_pass_times = 1;
		int err;
		err = sys_page_alloc(0, (void *) UXSTACKTOP - PGSIZE,
				     PTE_W | PTE_U | PTE_P);
		if (err < 0)
			panic("set_exception_handler: when allocating a page, %e", err);

		err = sys_env_set_exception_upcall(0, trapno, _pgfault_upcall);
		if (err < 0)
			panic("set_exception_hanlder: when setting exception upcall, %e", err);
	}

	_exception_handler[trapno] = handler;
}
