#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "fs.h"

int pread(int fd, void* addr, int n, int off){

	return pread_os(fd, addr, n, off);

}

int pread_w(void){
	int fd;
	void* addr;
	int n;
	int off;
	
	if(argint(0,&fd) < 0) {
		return -1;
	}

	if(argptr(1, (char**)&addr, sizeof(addr)) < 0) {
		return -1;
	}

	if(argint(2,&n) < 0) {
		return -1;
	}

	if(argint(3,&off) < 0) {
		return -1;
	}

	return pread(fd, (void*)addr, n, off);

}


