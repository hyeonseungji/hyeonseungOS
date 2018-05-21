
#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"


void thread_exit(void *retval){

	/*cprintf("thread_exit : retval is %d\n",(int*)retval);*/
	thread_exit_os(retval);
}

int thread_exit_w(void) {

	void * input;
	if(argptr(0, (char**)&input, sizeof(input)) < 0)
	  return -1;
	thread_exit((void*)input);
	return -1;
}
