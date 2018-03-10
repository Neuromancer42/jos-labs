/* See COPYRIGHT for copyright information. */

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/monitor.h>
#include <kern/console.h>
#include <kern/pmap.h>
#include <kern/kclock.h>


void
i386_init(void)
{
	extern char edata[], end[];

	// Before doing anything else, complete the ELF loading process.
	// Clear the uninitialized global data (BSS) section of our program.
	// This ensures that all static/global variables start out zero.
	memset(edata, 0, end - edata);

	// Initialize the console.
	// Can't call cprintf until after we do this!
	cons_init();

	cprintf("6828 decimal is %o octal!\n", 6828);

	// Lab 2 memory management initialization functions
	mem_init();

	cprintf("test color:"
		" \e\004red"
		" \e\002green"
		" \e\001blue"
		"\n\e\007");
	cprintf("\e\173\n"
		"    _   __                                                           __ __ ___ \n"
		"   / | / /__  __  ___________  ____ ___  ____ _____  ________  _____/ // /|__ \\\n"
		"  /  |/ / _ \\/ / / / ___/ __ \\/ __ `__ \\/ __ `/ __ \\/ ___/ _ \\/ ___/ // /___/ /\n"
		" / /|  /  __/ /_/ / /  / /_/ / / / / / / /_/ / / / / /__/  __/ /  /__  __/ __/ \n"
		"/_/ |_/\\___/\\__,_/_/   \\____/_/ /_/ /_/\\__,_/_/ /_/\\___/\\___/_/     /_/ /____/ \n"
		"                                                                               \n"
		"\e\007\n");
		// Drop into the kernel monitor.
	while (1)
		monitor(NULL);
}


/*
 * Variable panicstr contains argument to first call to panic; used as flag
 * to indicate that the kernel has already called panic.
 */
const char *panicstr;

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: mesg", and then enters the kernel monitor.
 */
void
_panic(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	if (panicstr)
		goto dead;
	panicstr = fmt;

	// Be extra sure that the machine is in as reasonable state
	asm volatile("cli; cld");

	va_start(ap, fmt);
	cprintf("kernel panic at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);

dead:
	/* break into the kernel monitor */
	while (1)
		monitor(NULL);
}

/* like panic, but don't */
void
_warn(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	va_start(ap, fmt);
	cprintf("kernel warning at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);
}
