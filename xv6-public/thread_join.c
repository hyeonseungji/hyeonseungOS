#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"


int thread_join(thread_t thread, void ** retval){
	/*cprintf("thread_on join is %d\n",thread);*/
	return thread_join_os(thread, retval);

}

int thread_join_w(void) {
	int * input;
	void* input2;

	if(argptr(0,(char**)&input,sizeof(input)) < 0)
	  return -1;
	if(argptr(1,(char**)&input2,sizeof(input2)) < 0)
	  return -1;
	return thread_join((thread_t)input, (void **)input2);
}
