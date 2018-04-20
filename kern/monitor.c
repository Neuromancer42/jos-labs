// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display a listing of function call frames", mon_backtrace },
	{ "mappings", "Display and manipulate the physical page mappings", mon_mappings },
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	int *ebp = (int*) read_ebp();
	cprintf("Stack backtrace:\n");
	while (ebp) {
		int eip = ebp[1];
		int* args = ebp + 1;
		cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n", 
			ebp, eip, args[1], args[2], args[3], args[4], args[5]);
		struct Eipdebuginfo eip_info;
		int errno = debuginfo_eip(eip, &eip_info);
		cprintf("      %s:%d: %.*s+%d\n",
			eip_info.eip_file,
			eip_info.eip_line,
			eip_info.eip_fn_namelen, eip_info.eip_fn_name,
			eip - eip_info.eip_fn_addr);
		ebp = (int*)*ebp;
	}
	return 0;
}

void
mon_mappings_help()
{
	cprintf("mappings: Display and manipulate the physical memory mappings\n"
		"          help\n"
		"                -- show this message\n"
		"          show  <lower-addr> <upper-addr>\n"
		"                -- display the mapping of the virtual addresses\n"
		"          set   <address> [<entry>]\n"
		"                -- set the page table entry if it exists\n"
		"                   if no entry set, show the original\n"
		"          dump  physical/virtual <lower-addr> <upper-addr>\n"
		"                -- dump the contents in the virtual memory\n");
}

void
mon_mappings_show(uintptr_t lower, uintptr_t upper)
{
	cprintf("Page mappings:\n"
		"     Virtual    Physical\n", lower, upper);
	size_t i;
	pde_t *pd_ptr = (pde_t *) KADDR(rcr3());
	for(i = lower; i <= upper; i += PGSIZE) {
		cprintf("  %08p", (void *) i);
		pte_t *pte_ptr;
		pte_ptr = pgdir_walk(pd_ptr, (void *) i, false);
		if (pte_ptr)
			cprintf("  %08p\n", PTE_ADDR(*pte_ptr));
		else
			cprintf("    unmapped\n");
	}
}

void
mon_mappings_show_entry(uintptr_t va)
{
	pde_t *pd_ptr = (pde_t *) KADDR(rcr3());
	pte_t *pte_ptr = pgdir_walk(pd_ptr, (void *) va, false);
	if (pte_ptr)
		cprintf("%08p: %08x\n", va, *pte_ptr);
	else
		cprintf("%08p: unmapped\n", va);
}

void
mon_mappings_set_entry(uintptr_t va, pde_t pde)
{
	pde_t *pd_ptr = (pde_t *) KADDR(rcr3());
	pte_t *pte_ptr = pgdir_walk(pd_ptr, (void *) va, false);
	if (pte_ptr == NULL) {
		*pte_ptr = pde;
		cprintf("%08p: %08x -> %08x\n", va, *pte_ptr);
	} else
		cprintf("%08p: unmapped\n", va);
}

void
mon_mappings_dump_page(uintptr_t pa, size_t offset, size_t length)
{
	size_t i;
	for (i = pa + ROUNDDOWN(offset, 16);
	     i < pa + ROUNDUP(offset + length, 16);
	     i++) {
		if (i <  pa + offset
		    || i >=  pa + offset + length)
			cprintf("   ");
		else
			cprintf(" %02x", (uint8_t) *((char*) i));
		if (i % 16 == 15)
			cprintf("\n");
	}
	cprintf("\n");
}

void
mon_mappings_dump(uintptr_t lower, uintptr_t upper)
{
	if (lower >= upper) return;
	size_t i;
	pde_t *pd_ptr = (pde_t *) KADDR(rcr3());
	pte_t *pte_ptr;
	uintptr_t p1 = ROUNDUP(lower, PGSIZE);
	uintptr_t p2 = ROUNDDOWN(upper, PGSIZE);
	if (p1 < p2) {
		pte_ptr = pgdir_walk(pd_ptr, (void *) lower, false);
		if (pte_ptr)
			mon_mappings_dump_page((uintptr_t) KADDR(PTE_ADDR(*pte_ptr)),
					       lower, PGSIZE - lower);
		else
			cprintf(" unmapped!\n\n");
		uintptr_t p;
		for (p = p1 + PGSIZE; p < p2; p += PGSIZE) {
			pte_ptr = pgdir_walk(pd_ptr, (char *) p, false);
			if (pte_ptr)
				mon_mappings_dump_page((uintptr_t) KADDR(PTE_ADDR(*pte_ptr)),
						       0, PGSIZE);
			else
				cprintf(" unmapped!\n\n");
		}
		if (upper > p2) {
			pte_ptr = pgdir_walk(pd_ptr, (char *) p2, false);
			if (pte_ptr)
				mon_mappings_dump_page((uintptr_t) KADDR(PTE_ADDR(*pte_ptr)),
						       0, upper - p2);
			else
				cprintf(" unmmapped!\n\n");
		}
	} else {
		pte_ptr = pgdir_walk(pd_ptr, (char *) lower, false);
		if (pte_ptr)
			mon_mappings_dump_page((uintptr_t) KADDR(PTE_ADDR(*pte_ptr)),
					       lower, upper - lower);
		else
			cprintf(" unmapped!\n\n");
	}
}

int
mon_mappings(int argc, char **argv, struct Trapframe *tf)
{
	if (argc == 1) {
		mon_mappings_help();
		return 0;
	} else {
		if (strcmp(argv[1], "show") == 0) {
			if (argc == 4) {
				uintptr_t lower, upper;
				lower = ROUNDDOWN(strtol(argv[2], NULL, 0), PGSIZE);
				upper = ROUNDDOWN(strtol(argv[3], NULL, 0), PGSIZE);
				mon_mappings_show(lower, upper);
				return 0;
			} else {
				cprintf("Usage: show  <lower-addr> <upper-addr>\n");
				return -1;
			}
		} else if (strcmp(argv[1], "set") == 0) {
			if (argc == 3) {
				uintptr_t va = strtol(argv[2], NULL, 0);
				mon_mappings_show_entry(va);
				return 0;
			} else if (argc == 4) {
				uintptr_t va = strtol(argv[2], NULL, 0);
				pde_t pde = strtol(argv[3], NULL, 0);
				return 0;
			} else {
				cprintf("Usage: set   <address> [<entry>]\n"
					"             -- set the page table entry if it exists\n"
					"                if no entry set, show the original\n");
				return -1;
			}
		} else if (strcmp(argv[1], "dump") == 0) {
			if (argc == 5 && strcmp(argv[2], "physical") == 0) {
				physaddr_t lower, upper;
				lower = (physaddr_t) KADDR(strtol(argv[3], NULL, 0));
				upper = (physaddr_t) KADDR(strtol(argv[4], NULL, 0));
				mon_mappings_dump(lower, upper);
				return 0;
			} else if (argc == 5 && strcmp(argv[2], "virtual") == 0) {
				uintptr_t lower, upper;
				lower = strtol(argv[3], NULL, 0);
				upper = strtol(argv[4], NULL, 0);
				mon_mappings_dump(lower, upper);
				return 0;
			} else {
				cprintf("            dump  physical/virtual <lower-addr> <upper-addr>\n"
					"                  -- dump the contents in the virtual memory\n");
				return -1;
			}
		} else if (strcmp(argv[1], "help") == 0) {
			mon_mappings_help();
			return 0;
		} else {
			cprintf("Unknown command. use \"mappings help\" for more info.\n");
			return -1;
		}
	}
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		if (tf != NULL) {
			buf = readline("debug> ");
			if (strcmp(buf, "continue") == 0
			    || strcmp(buf, "c") == 0) {
                                // debug: continue
				// reset EFlags trap flag
				tf->tf_eflags = tf->tf_eflags & (~ FL_TF);
				break;
			} else if (strcmp(buf, "step") == 0
				   || strcmp(buf, "s") == 0) {
				// debug: single-step
				// set EFLags trapflag
				tf->tf_eflags = tf->tf_eflags | FL_TF;
				break;
			} else if (strcmp(buf, "registers") == 0
				   || strcmp(buf, "r") == 0) {
				// debug: show registers
				cprintf("Registers:\n"
					"edi: %08x  esi: %08x\n"
					"ebp: %08x  esp: %08x\n"
					"ebx: %08x  edx: %08x\n"
					"ecx: %08x  eax: %08x\n"
					"ss: %08x\n"
					"cs: %08x\n"
					"ds: %08x\n"
					"es: %08x\n"
					"eflags: %08x\n"
					"eip: %08x\n",
					tf->tf_regs.reg_edi,
					tf->tf_regs.reg_esi,
					tf->tf_regs.reg_ebp,
					tf->tf_regs.reg_oesp,
					tf->tf_regs.reg_ebx,
					tf->tf_regs.reg_edx,
					tf->tf_regs.reg_ecx,
					tf->tf_regs.reg_eax,
					tf->tf_ss, tf->tf_cs,
					tf->tf_ds, tf->tf_es,
					tf->tf_eflags, tf->tf_eip);
				continue;
			} else {
				cprintf("Supported debug commands:\n"
					"(c)ontinue:  continue current execution\n"
					"(s)tep:      single step to next command\n"
					"(r)egisters: show all regsiters\n");
				continue;
			}
		} else {
			buf = readline("K> ");
			if (buf != NULL)
				if (runcmd(buf, tf) < 0)
					break;
		}
	}
}
