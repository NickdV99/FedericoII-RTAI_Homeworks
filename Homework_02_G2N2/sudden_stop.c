//---------------------BUDDY TASK FOR THE SIMULATION OF THE SUDDEN STOP ----------------------//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <rtai_lxrt.h>
#include <rtai_shm.h>
#include <sys/io.h>
#include "parameters.h"

static RT_TASK *main_Task;

int * shm_sudden_stop;

int main(void)
{

	if (!(main_Task = rt_task_init_schmod(nam2num("SDSTOP"), 0, 0, 0, SCHED_FIFO, 0xF))) {
		printf("CANNOT INIT SUDDEN STOP MAIN TASK\n");
		exit(1);
	}


	shm_sudden_stop = rtai_malloc(SHM_STOP, sizeof(int));
	
	for(int i = 0; i < NUM_OF_WHEELS; i++){
	
		shm_sudden_stop[i] = 1;

	}

	rt_shm_free(SHM_STOP);	

	rt_task_delete(main_Task);
 	printf("Sudden stop \n");
	return 0;
}
