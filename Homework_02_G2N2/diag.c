//---------------------TASK DIAGNOSTIC ----------------------

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <rtai_lxrt.h>
#include <rtai_mbx.h>
#include <sys/io.h>
#include <signal.h>
#include "parameters.h"

static RT_TASK *main_Task;

static int keep_on_running = 1; 
static void endme(int dummy) {keep_on_running = 0;}


MBX* mbx_acq_diag_SS;
MBX* mbx_SS_diag;

int main(void)
{
	printf("The DIAGNOSTIC STARTED!\n");
	signal(SIGINT, endme);

	if (!(main_Task = rt_task_init_schmod(nam2num("DIAG"), 5, 0, 0, SCHED_FIFO, 0xF))) {
		printf("CANNOT INIT DIAGNOSTIC MAIN TASK\n");
		exit(1);
	}

	int msg_to_ss = DIAG_MESSAGE;
	diag_info answ; 
	
	mbx_acq_diag_SS = rt_typed_named_mbx_init(MBX_ACQ_DIAG_SS, sizeof(int), PRIO_Q);
	mbx_SS_diag = rt_typed_named_mbx_init(MBX_SS_DIAG, sizeof(diag_info), PRIO_Q);

	int counter = 0;

	while ( counter < 10 && keep_on_running ){
		
		rt_mbx_send(mbx_acq_diag_SS, &msg_to_ss, sizeof(int));

		printf("REQUEST %d SENT \n", counter);

		if( !( rt_mbx_receive(mbx_SS_diag, &answ, sizeof(diag_info)) ) ){
			
			printf("ANSWER FROM SPORADIC SERVER RECIVED \n");
			
			if( answ.response != 0 ){

				printf("THE SS CANNOT SERVE MY REQUEST, I'M GOING TO SLEEP UNTIL THE NEXT R. TIME : %llu \n", answ.response);				
				rt_sleep_until(answ.response);

			}
		
			else{
			
				counter ++;

				for( int i = 0; i < NUM_OF_WHEELS; i++){

					printf("\n\n************ WHEEL  [%d] ***************\n", i); 
					printf("WCET - ACQUIRE :%llu \n", answ.info_wheels[i].wcet_acquire); 
					printf("INFO - ACQUIRE ( error ) :%d \n", answ.info_wheels[i].acquire); 
					printf("WCET - FILTER :%llu \n", answ.info_wheels[i].wcet_filter); 
					printf("INFO - FILTER ( average ):%d \n", answ.info_wheels[i].filter); 
					printf("WCET - CONTROLLER :%llu \n", answ.info_wheels[i].wcet_controller); 
					printf("INFO - CONTROLLER :%d \n", answ.info_wheels[i].controller); 
					printf("WCET - ACTUATOR :%llu \n", answ.info_wheels[i].wcet_actuator); 
					printf("INFO - ACTUATOR :%d \n", answ.info_wheels[i].actuator);
	
				}
			
				for ( int i = 0; i < answ.num_el; i ++){
					printf("[ RTIME %d:   %llu \t ||", i, answ.info_ss[i].RT);
					printf("  RAMOUNT %d: %llu ] \n", i, answ.info_ss[i].RA);
				
				}
				
				printf("  REMAINING CAPACITY : %llu \n", answ.remaining_capacity);
				printf("\n\n\n");
				
				rt_sleep(nano2count(2000000));
			
			}
			
		}else printf("MESSAGGIO NON RICEVUTO \n");

	}



	
	//rt_mbx_delete(mbx_SS_diag);

	rt_task_delete(main_Task);
	return 0;
}
