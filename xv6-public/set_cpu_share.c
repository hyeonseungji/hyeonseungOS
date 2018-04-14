#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"


int set_cpu_share(int a){

	myproc()->share = a;
	return a;

}

int sys_set_cpu_share(void){
	int input;
	if(argint(0,&input) < 0)
	 return -1;
	return set_cpu_share(input);
}
/*
int
argint(int n, int *ip)
{
  return fetchint((myproc()->tf->esp) + 4 + 4*n, ip);
}
*/