//--------------------------- SPORADIC SERVER ------------------ 

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <rtai_lxrt.h>
#include <rtai_shm.h>
#include <rtai_sem.h>
#include <rtai_mbx.h>
#include <rtai_msg.h>
#include <sys/io.h>
#include <syslog.h>
#include <signal.h>
#include "parameters.h"
#define CPUMAP 0x1
#define SS_PERIOD_MULTIPLIER 0.5

static RT_TASK *main_Task;
static RT_TASK *sporadic_serv_Task ;
static RT_TASK *airbag_Task;

static pthread_t airbag_thread;
static pthread_t sporadic_server; 
static int keep_on_running = 1;

//CIRCULAR BUFFER USED TO STORE THE REPLENISHMENT TIMES AND AMOUNTS OF THE SPORADIC SERVER
static ss_replenishment server_replen[5];
static int head = 0;
static int tail = 0;
static int num_elem = 0;
static diag_info info ;

static RTIME start;
static RTIME end; 
static RTIME wcet_capacity_needed = 0;

//PERIOD AND CAPACITY OF THE SPORADIC SERVER
static RTIME period ;
static RTIME capacity ;


static void endme(int dummy) {keep_on_running = 0;}

info_tasks_wheel* shm_info;	
int* reference;

SEM* mutex_info;

MBX* mbx_acq_diag_SS;
MBX* mbx_SS_diag;
MBX* mbx_contr_AB;



static RTIME compute_tot_ra( RTIME t ){
	
	RTIME RA = 0;
	int j = 0;
	int i = tail;

	while ( (j < num_elem) && (t > server_replen[i].RT) ){
		
		RA = RA + server_replen[i].RA;
		i = (i+1)%5;
		j++;
	}
		
	num_elem = num_elem - j;

	tail = i;

	return RA;
		
}



static void Diag(RTIME t, RTIME period){

	RTIME capacity_used ; 

	start = rt_get_time();
	
	info.response = 0;

	//GETTING INFO FROM THE SHM
	
	rt_sem_wait(mutex_info);
	
	//Start of Critical Section

	for ( int i = 0; i < NUM_OF_WHEELS; i++){
	
		info.info_wheels[i].wcet_acquire = shm_info[i].wcet_acquire;
		info.info_wheels[i].wcet_filter = shm_info[i].wcet_filter;
		info.info_wheels[i].wcet_controller = shm_info[i].wcet_controller ;
		info.info_wheels[i].wcet_actuator = shm_info[i].wcet_actuator;
		info.info_wheels[i].acquire = shm_info[i].acquire;
		info.info_wheels[i].filter = shm_info[i].filter;	
		info.info_wheels[i].controller = shm_info[i].controller;
		info.info_wheels[i].actuator = shm_info[i].actuator;
	
	}
	
	//End of Critical Section
		
	rt_sem_signal(mutex_info);	

	end = rt_get_time();

	capacity_used = end - start;
	
	server_replen[head].RT = t + period;		//SET THE NEXT REPLENISHMENT TIME
	server_replen[head].RA = capacity_used;		//SET THE REPLENISHMENT AMOUNT OF THE NEXT REPLENISHMENT TIME 
	
	//MANAGING OF THE CIRCULAR BUFFER
	head = (head + 1) % 5;				
	num_elem ++;
	
	//COMPUTING THE REMAINING CAPACITY
	if( capacity > capacity_used )			//IN CASE THE CAPACITY USED IS GREATER THAN THE LAST WCET AND THAN THE CAPACITY REMAINED WHEN THE REQUEST WAS ACCEPTED  				 
		capacity = capacity - capacity_used;
	
	else 
		capacity = 0;			
									
		
	info.remaining_capacity = capacity;	

	//COPYING THE ARRAY OF REPLENISHMENT TIMES AND AMOUNTS INTO THE MESSAGE THAT WILL BE SENT TO THE DIAGNOSTIC TASK

	int k = tail;
	
	for ( int i = 0; i < num_elem; i ++ ){
	
		info.info_ss[i].RA = server_replen[k].RA;	
		info.info_ss[i].RT = server_replen[k].RT;
		k = (k+1) % 5;
		
	}
	
	info.num_el = num_elem;
	
	rt_mbx_send(mbx_SS_diag, &info, sizeof(diag_info));
	
	//UPDATE THE WCET 			
	if( capacity_used >= wcet_capacity_needed )

		wcet_capacity_needed = capacity_used;


}




static void* airbag( void* par){

	if (!(airbag_Task = rt_task_init_schmod(nam2num("AIRBAG"), 0, 0, 0, SCHED_FIFO, CPUMAP))) {
		printf("CANNOT INIT AIRBAG TASK\n");
		exit(1);
	}

		int msg_from_ss;
		int msg_to_controllers = 1;
		
	
		if((rt_receive(0, &msg_from_ss)) != 0  ){
	
			printf("********** AIRBAG ATTIVATO *********** \n");

			*reference = 0;

			for( int i = 0; i < NUM_OF_WHEELS; i ++ ){

				rt_mbx_send( mbx_contr_AB, &msg_to_controllers, sizeof(int)); 

			}

		}else printf("AIRBAG TASK - ERROR DURING THE RECEPTION OF THE MESSAGE \n");

	
	printf("AIRBAG TASK DELETED \n" );
	rt_task_delete(airbag_Task);
	return 0;




}



static void* sporadic_serv_loop(void* par ){
	
	if (!(sporadic_serv_Task = rt_task_init_schmod(nam2num("SPORSRV"), 1, 0, 0, SCHED_FIFO, CPUMAP))) {
		printf("CANNOT INIT SPORADIC SERVER TASK\n");
		exit(1);
	}

	//printf( "SPORADIC SERVER STARTED \n");
	int rqst ;
	RTIME RA ;
	RTIME expected = rt_get_time() +  period/5;
	rt_task_make_periodic(sporadic_serv_Task, expected, period);
	rt_make_hard_real_time();

	
	RTIME t ;

	while (keep_on_running){

	
		if(!(rt_mbx_receive( mbx_acq_diag_SS, &rqst, sizeof(int)))){		//ACQUIRE TASK AND DIAGNOSTIC TASK SEND MESSAGES TO THIS MAILBOX, THOSE MESSAGES ARE CAPTURED BY THE SPORADIC SERVER WHICH KNOWS WHO SENT THE MESSAGE BY THE VALUE RECIVED

			if ( rqst == AIRBAG_MESSAGE ){
	
				rt_send(main_Task, AIRBAG_MESSAGE);
				
				rt_send(airbag_Task,1); 	//IF THE MESSAGE RECEIVED WAS SENT BY THE ACQUIRE TASK, THE SPORADIC SERVER WILL WAKE UP THE AIRBAG TASK
			
			}
			
			else if( rqst == DIAG_MESSAGE ){	//IF THE MESSAGE RECEIVED WAS SENT BY THE DIAGNOSTIC TASK, THE SPORADIC SERVER WILL TRY TO SERVE THE REQUEST 

				t = rt_get_time();

				//COMPUTING OF THE R. AMOUNT THAT HAS TO BE ADDED TO THE CAPACITY 

				RA = compute_tot_ra(t);

				capacity = capacity + RA;

				
				if( capacity >= wcet_capacity_needed ){	
				
					rt_send(main_Task, DIAG_MESSAGE);			

					Diag(t,period);

				}

				else{ 	
					
					//IF THE SPORADIC SERVER DOESN'T HAVE ENOUGH CAPACITY, HE ASKS TO THE DIAG TASK TO SEND THE REQUEST AGAIN WHEN THE NEXT R. TIME WILL ARRIVE
			
					info.response = server_replen[tail].RT;	
					rt_mbx_send(mbx_SS_diag, &info, sizeof(diag_info));	//THE SPORADIC SERVER SENDS THE MESSAGE (TO THE DIAG TASK) WHICH CONTAINS THE NEXT REPLENISHMENT TIME 
								
				}
					
			}

		}
	
	}
	
	rt_task_delete(sporadic_serv_Task);

	return 0;
}


int main(void)
{

	printf("The Sporadic Server is STARTED!\n");
 	signal(SIGINT, endme);

	if (!(main_Task = rt_task_init_schmod(nam2num("SPOSRV"), 0, 0, 0, SCHED_FIFO, 0xF))) {
		printf("CANNOT INIT SPORADIC SERVER MAIN TASK\n");
		exit(1);
	}

	//INIT PERIOD AND CAPACITY OF THE SPORADIC SERVER 
	period = nano2count(10000000);			

	//capacity = nano2count(1000000);
	
	capacity = 100000;
	
	printf("capacity : %llu \n", capacity);
	
	//INIT THE ARRAY OF REPLENISHMENT TIMES AND AMOUNTS OF THE SPORADIC SERVER 
	
	for (int i=0; i < 5; i++){
	
		server_replen[i].RT = 0;
		server_replen[i].RA = 0;

	}
	

	//MAILBOX
	mbx_acq_diag_SS = rt_typed_named_mbx_init(MBX_ACQ_DIAG_SS, sizeof(int), PRIO_Q);
	mbx_SS_diag = rt_typed_named_mbx_init(MBX_SS_DIAG, sizeof(diag_info), PRIO_Q);
	mbx_contr_AB = rt_typed_named_mbx_init(MBX_CONTR_AB, NUM_OF_WHEELS * sizeof(int), FIFO_Q);


	//SEMAPHORE
	mutex_info = rt_typed_named_sem_init(MUTEX_INFO,  1, BIN_SEM | FIFO_Q);

	//SHM
	shm_info = rtai_malloc(INF_SHM, NUM_OF_WHEELS * sizeof(info_tasks_wheel));
	reference = rtai_malloc(REFSENS, sizeof(int));
	
	//THREAD 
	pthread_create(&sporadic_server, NULL, sporadic_serv_loop, NULL);
	pthread_create(&airbag_thread, NULL, airbag , NULL);
	
	int buf;

	while ( keep_on_running ){



		printf("\nWaiting for a request ...\n");
		
		if(rt_receive(0, &buf) != 0 ){	
		
			if( buf == AIRBAG_MESSAGE )
		
				printf("\nServing an Airbag request\n");	
				
			else if( buf == DIAG_MESSAGE )
		
				printf("\nServing a Diagnostic request \n");
				
		}
			
		
		//rt_sleep(1000000000);
		
	}


	rt_named_sem_delete(mutex_info);	

	rt_shm_free(INF_SHM);
	rt_shm_free(REFSENS);
	
	rt_named_mbx_delete(mbx_SS_diag);
	rt_named_mbx_delete(mbx_acq_diag_SS);
	rt_named_mbx_delete(mbx_contr_AB);

	rt_task_delete(main_Task);
 	
	return 0;
}
