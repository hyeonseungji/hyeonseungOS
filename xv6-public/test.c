#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[])
{
	/*printf(1,"my ppid is %d \n",getppid());*/
	printf(1,"my pid is %d\n",getpid());
	
	for(int i = 0; i < 5; i++)
		yield();
	set_cpu_share(30);
	printf(1,"yield end!\n");
	exit();
}
