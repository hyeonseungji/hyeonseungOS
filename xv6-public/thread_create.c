
#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int thread_create(thread_t* thread, void*(*start_routine)(void*),void * arg){

	/*cprintf("tid is %d\n",*thread);
	cprintf("routine is %p\n",*(int*)start_routine);
	cprintf("arg is %d\n",(int)arg);*/
	return thread_create_os(thread, start_routine, arg);
}

int thread_create_w(void){
	int* input;
	void* input2;
	void* input3;
	if(argptr(0,(char**)&input,sizeof(input)) < 0)
	 return -1;
	cprintf("input is %d\n",*input);
	if(argptr(1,(char**)&input2,sizeof(input2)) < 0)
	 return -1;
	if(argptr(2,(char**)&input3,sizeof(input3)) < 0)
	 return -1;
	return thread_create(input, (void*)input2, (void*)input3);

}
