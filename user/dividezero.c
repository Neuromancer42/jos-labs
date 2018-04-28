// test user-level general handler -- divide-by-zero kills the problem gracefully

#include <inc/lib.h>

void
handler(struct UTrapframe *utf)
{
	cprintf("divide-by-zero happens at %p\n", utf->utf_eip);
	sys_env_destroy(sys_getenvid());
}

int zero;

void
umain(int argc, char **argv)
{
	set_exception_handler(T_DIVIDE, handler);
  zero = 0;
	cprintf("1/0 is %08x!\n", 1/zero);
}
