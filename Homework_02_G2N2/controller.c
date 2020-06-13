//------------------- CONTROLLER.C ---------------------- 

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
#include <signal.h>
#include <syslog.h>
#include "parameters.h"
#define CPUMAP 0x1



static RT_TASK *main_Task;
static RT_TASK *read_Task[NUM_OF_WHEELS];
static RT_TASK *filter_Task[NUM_OF_WHEELS];
static RT_TASK *control_Task[NUM_OF_WHEELS];
static RT_TASK *write_Task[NUM_OF_WHEELS];
static int keep_on_running = 1;

static pthread_t read_thread[NUM_OF_WHEELS];
static pthread_t filter_thread[NUM_OF_WHEELS];
static pthread_t control_thread[NUM_OF_WHEELS];
static pthread_t write_thread[NUM_OF_WHEELS];
static RTIME sampl_interv;

static void endme(int dummy) {keep_on_running = 0;}

int* sensor;
int* actuator;
int* reference;
info_tasks_wheel* shm_info;		//SHM used to save the wcet of the tasks and other informations
int* wheel_block;		//SHM used for the comunication between controllers

static int buffer[NUM_OF_WHEELS][BUF_SIZE];
static int head[NUM_OF_WHEELS];
static int tail[NUM_OF_WHEELS];

SEM* space_avail[NUM_OF_WHEELS];
SEM* meas_avail[NUM_OF_WHEELS];
SEM* mutex_info;
SEM* mutex_controller;

MBX* mbx_acq_diag_SS;			//MAILBOX WHERE ACQUIRE TASK AND DIAGNOSTIC TASK SEND MESSAGE TO THE SPORADIC SERVERE ( ACQUIRE SENDS A MESSAGE IF A SUDDEN STOP OCCURRED, DIAG SENDS A MESSAGE IF A DIAGNOSTIC REQUEST OCCURRED )
MBX* mbx_contr_airbag;			//MAILBOX USED BY THE AIRBAG TASK TO NOTIFY THE ACTIVATION OF THE AIRBAG TO THE CONTROLLER TASKS



static void * acquire_loop(void * par) {

	int i = (int) par;
	int count = 1;
	int prev_value = sensor[i];
	int gap = 0;
	int msg_send = AIRBAG_MESSAGE;

	if (!(read_Task[i] = rt_task_init_schmod(nam2num("READER")+i, 1+i, 0, 0, SCHED_FIFO, CPUMAP))) {
		printf("CANNOT INIT SENSOR TASK\n");
		exit(1);
	}

	RTIME wcet_acquire = 0;					//Declaration of wcet and two array ( start and end ) used as buffer to save temporarily the outputs of rt_get_exec_time 
	RTIME start[3];
	RTIME end[3];
	
	RTIME expected = rt_get_time() + NUM_OF_WHEELS*sampl_interv;  	
	rt_task_make_periodic(read_Task[i], expected, sampl_interv);
	rt_make_hard_real_time();
	


	while (keep_on_running)
	{

		if ( count == 1 ) 

			rt_get_exectime(read_Task[i], start);  // wcet



		// DATA ACQUISITION FROM PLANT
		rt_sem_wait(space_avail[i]);

		//Start of Critical Section 

			buffer[i][head[i]] = sensor[i];
			gap = prev_value - buffer[i][head[i]];
			prev_value = buffer[i][head[i]];
			head[i] = (head[i]+1) % BUF_SIZE;

		//End of Critical Section

		rt_sem_signal(meas_avail[i]);
		
		if( gap >= 50 ){

			if( i == 0 )
		
				rt_mbx_send(mbx_acq_diag_SS, &msg_send, sizeof(int));
			
		}




		if( count % 5 == 0 ){
		
			count =	0;	

			// computation of the execution time 
			rt_get_exectime (read_Task[i], end); // wcet

			if( wcet_acquire < (unsigned long long)count2nano(end[0] - start[0])/5 ){	//Once we got the execution time of the current istance, we must verify if this value is greater or lower than the current wcet 
				
				wcet_acquire = (unsigned long long)count2nano(end[0] - start[0])/5;
		
			}
		
			// writing info's into the shm 
			rt_sem_wait(mutex_info);
			
			//Start of Critical Section
			shm_info[i].wcet_acquire = wcet_acquire ;
			shm_info[i].acquire = gap ;
			//End of Critical Section
	
			rt_sem_signal(mutex_info);
			
		}

		count ++;		
			
		rt_task_wait_period();
	}
	rt_task_delete(read_Task[i]);
	return 0;
}







static void * filter_loop(void * par) {

	int i = (int) par;
	int count = 1;
	int cnt = BUF_SIZE;
	unsigned int sum = 0;
	unsigned int avg = 0;

	if (!(filter_Task[i] = rt_task_init_schmod(nam2num("FILTER")+i, 5+i, 0, 0, SCHED_FIFO, CPUMAP))) {
		printf("CANNOT INIT FILTER TASK\n");
		exit(1);
	}
	
	RTIME wcet_fil = 0;					//Declaration of wcet and two array ( start and end ) used as buffer to save temporarily the outputs of rt_get_exec_time 
	RTIME start[3];
	RTIME end[3];

	RTIME expected = rt_get_time() + NUM_OF_WHEELS* sampl_interv;
	rt_task_make_periodic(filter_Task[i], expected, sampl_interv);
	rt_make_hard_real_time();

	

	while (keep_on_running)
	{

		if ( count == 1 )
	
			rt_get_exectime (filter_Task[i], start); 	


	
	
		// FILTERING (average)
		rt_sem_wait(meas_avail[i]);

			sum += buffer[i][tail[i]];
			tail[i] = (tail[i]+1) % BUF_SIZE;

		rt_sem_signal(space_avail[i]);
		
		cnt--;

		if (cnt == 0) {
			cnt = BUF_SIZE;
			avg = sum/BUF_SIZE;
			sum = 0;
			// sends the average measure to the controller
			rt_send(control_Task[i], avg);		
		}



		if( count % 5 == 0 ){
		
			count =	0;	

			// computation of the execution time 		
			rt_get_exectime (filter_Task[i], end);	

			if( wcet_fil < ( unsigned long long ) count2nano( end[0] - start[0] )/5 )
			
				wcet_fil = ( unsigned long long ) count2nano( end[0] - start[0] )/5; 
	
			// writing info's into the shm 		
			
			rt_sem_wait(mutex_info);
			
			//Start of Critical Section
			shm_info[i].wcet_filter = wcet_fil ;
			shm_info[i].filter = avg;
			//End of Critical Section
	
			rt_sem_signal(mutex_info);

		}

		count ++;

		rt_task_wait_period();
	}
	rt_task_delete(filter_Task[i]);
	return 0;
}







static void * control_loop(void * par) {

	int i = (int) par;			// the variable "i" indicates the number of the wheel controlled by this task
	unsigned int prev_sensor = 0;             //to store previous sensor readings to detect skids
	unsigned int plant_state = 0;           //speed received from the plant
	int error = 0;                          //error to use to calculate the control action
	unsigned int control_action = 0;        //control action to be sent to the actuator
	unsigned int ANTI_SKID_ON = 1;		//to activate the ANTI SKID
	unsigned int CONTROL_PERIOD_MULTIPLIER = 1;	//to configure the control period
	unsigned int support = 0 ;				// support variable used to save the message returned from rt_mbx_recive_if
	unsigned int AirBag_activated = 0;		// notify the activation of the AirBag
	unsigned int k=0;
	unsigned int block = 0;			//if 1, it means that one or more of the other wheels is blocked

	RTIME wcet_con = 0;					//Declaration of wcet and two array ( start and end ) used as buffer to save temporarily the outputs of rt_get_exec_time 
	RTIME start[3];
	RTIME end[3];

	if (!(control_Task[i] = rt_task_init_schmod(nam2num("CNTRL")+i, 9+i, 0, 0, SCHED_FIFO, CPUMAP))) {
		printf("CANNOT INIT CONTROL TASK\n");
		exit(1);
	}

	RTIME expected = rt_get_time() +  NUM_OF_WHEELS*sampl_interv;
	rt_task_make_periodic(control_Task[i], expected, CONTROL_PERIOD_MULTIPLIER*BUF_SIZE*sampl_interv);
	rt_make_hard_real_time();
      
	while (keep_on_running)
	{         

		rt_get_exectime(control_Task[i], start); 	




		if( !(rt_mbx_receive_if(mbx_contr_airbag, &support, sizeof(int))) )

			AirBag_activated = 1;
		

		// receiving the average plant state from the filter
		rt_receive(filter_Task[i], &plant_state);

		if( plant_state == 0 && AirBag_activated == 1 ){

			control_action = 3;

		}

		else{

			rt_sem_wait(mutex_controller);

			//Start of Critical Section 
		
	        	if( prev_sensor == sensor[i] ) {

				wheel_block[i] = 1;
			
			}
	
			else{

			wheel_block[i] = 0; 
			block = 0;
	
			}
				
			while ( k < NUM_OF_WHEELS && block == 0){		//Check if one of the other wheels is blocked 

				if(wheel_block[k] == 1)
				
					block = 1;
	
				k++;

			}
			//End of Critical Section
	
			rt_sem_signal(mutex_controller);

			if( !AirBag_activated ){			//If the airbag is active, the car can only braking ( control action 4 ) or slow down ( control action 2, in case of wheel blocked )

				// computation of the control law
				error = (*reference) - plant_state;
				if (error > 0) control_action = 1;
				else if (error < 0) control_action = 2;
       	        		else control_action = 3; 

			} else control_action = 2;		


			if (ANTI_SKID_ON) {

				if (((*reference) == 0) && (plant_state != 0) && (block != 1)) 

					control_action = 4; //brake only when no skid is detected.

			} else if ((*reference) == 0) control_action = 4;

			k = 0;

		}
			
			

		// sending the control action to the actuator
		rt_send(write_Task[i], control_action);

		prev_sensor = sensor[i];		




		// computation of the execution time 
		rt_get_exectime (control_Task[i], end);
		
		if( wcet_con < ( unsigned long long ) count2nano( end[0] - start[0] ) )
			
			wcet_con =(unsigned long long)count2nano( end[0] - start[0] );  
	
		//writing info's into the shm
		
		rt_sem_wait(mutex_info);
			
		//Start of Critical Section	
		shm_info[i].wcet_controller = wcet_con ;
		shm_info[i].controller = AirBag_activated;
		//End of Critical Section
	
		rt_sem_signal(mutex_info);
			
			
             	rt_task_wait_period();
	}
	
	rt_task_delete(control_Task[i]);

	return 0;
}







static void * actuator_loop(void * par) {

	int i = (int) par;
	
	if (!(write_Task[i] = rt_task_init_schmod(nam2num("WRITE")+i, 13+i, 0, 0, SCHED_FIFO, CPUMAP))) {
		printf("CANNOT INIT ACTUATOR TASK\n");
		exit(1);
	}

	RTIME wcet_act = 0;					//Declaration of wcet and two array ( start and end ) used as buffer to save temporarily the outputs of rt_get_exec_time 
	RTIME start[3];
	RTIME end[3];

	unsigned int control_action = 0;
	int cntr = 0;

	RTIME expected = rt_get_time() +  NUM_OF_WHEELS*sampl_interv;
	rt_task_make_periodic(write_Task[i], expected, BUF_SIZE*sampl_interv);
	rt_make_hard_real_time();

	while (keep_on_running)

	{		
		rt_get_exectime (write_Task[i], start); 		




		// receiving the control action from the controller
		rt_receive(control_Task[i], &control_action);
		
		switch (control_action) {
			case 1: cntr = 1; break;
			case 2:	cntr = -1; break;
			case 3:	cntr = 0; break;
                        case 4: cntr = -2; break;
			default: cntr = 0;
		}
		
		(actuator[i]) = cntr;




		// computation of the execution time 		
		rt_get_exectime (write_Task[i], end);	

		if( wcet_act < ( unsigned long long ) count2nano( end[0] - start[0] ) )
			
			wcet_act = ( unsigned long long ) count2nano( end[0] - start[0] );   
	
		// writing info's into the shm 		
		
		rt_sem_wait(mutex_info);
			
		//Start of Critical Section
		shm_info[i].wcet_actuator = wcet_act ;
		shm_info[i].actuator = cntr;
		//End of Critical Section
	
		rt_sem_signal(mutex_info);
					

		rt_task_wait_period();
	}
	
	rt_task_delete(write_Task[i]);
	return 0;
}








int main(void)
{
	printf("The controller is STARTED!\n");
 	signal(SIGINT, endme);

	int i = 0;

	if (!(main_Task = rt_task_init_schmod(nam2num("MAINTSK"), 0, 0, 0, SCHED_FIFO, 0xF))) {
		printf("CANNOT INIT MAIN TASK\n");
		exit(1);
	}
 

	// Init heads and tails values 
	for( i = 0; i < NUM_OF_WHEELS; i++){

		head[i] = 0;
		tail[i] = 0;	

	}

	//Attach to data shared with the controller
	sensor = rtai_malloc(SEN_SHM, NUM_OF_WHEELS * sizeof(int));
	actuator = rtai_malloc(ACT_SHM, NUM_OF_WHEELS * sizeof(int));
	reference = rtai_malloc(REFSENS, sizeof(int));
	shm_info = rtai_malloc(INF_SHM, NUM_OF_WHEELS * sizeof(info_tasks_wheel));
	wheel_block = rtai_malloc(SHM_CON, NUM_OF_WHEELS * sizeof(int));		//This SHM store a value ( 0 or 1 ) for each wheel, this value indicates if that wheel is blocked or not ( 1 if it's blocked, 0 if not )

	//Init the block value of each wheel at 0
	for ( i = 0; i < NUM_OF_WHEELS; i++){

		wheel_block[i] = 0;		
				
	}


	(*reference) = 110;


	// SEMAPHORES
	for( i = 0; i < NUM_OF_WHEELS; i++){
	
		space_avail[i] = rt_typed_named_sem_init(SPACE_SEM + i, BUF_SIZE, CNT_SEM | PRIO_Q);
		meas_avail[i] = rt_typed_named_sem_init(MEAS_SEM + i, 0, CNT_SEM | PRIO_Q);
		
	}

	mutex_info = rt_typed_named_sem_init(MUTEX_INFO,  1, BIN_SEM | FIFO_Q);
	mutex_controller = rt_typed_named_sem_init(MUTEX_CONT,  1, BIN_SEM | PRIO_Q);


	// MAILBOX 
	mbx_acq_diag_SS = rt_typed_named_mbx_init(MBX_ACQ_DIAG_SS, sizeof(int), PRIO_Q);
	mbx_contr_airbag = rt_typed_named_mbx_init(MBX_CONTR_AB, NUM_OF_WHEELS * sizeof(int), FIFO_Q);
	
	sampl_interv = nano2count(CNTRL_TIME);
	
	// THREADS 
	for ( i = 0; i < NUM_OF_WHEELS; i++ ){
	
		pthread_create(&read_thread[i], NULL, acquire_loop, (void*)i );
		pthread_create(&filter_thread[i], NULL, filter_loop, (void*)i);
		pthread_create(&control_thread[i], NULL, control_loop, (void*)i);
		pthread_create(&write_thread[i], NULL, actuator_loop, (void*)i);
	
	}


	while (keep_on_running) {

		for( i = 0; i < NUM_OF_WHEELS; i++){
	
			printf("Control %d: %d \t",i,(actuator[i]));
			
		}
			
		printf("\n");		
		//The comments below are lines of code used to test the correct use of the shared memory "info"
		/*printf ("***************** \n");
		printf("WCET - ACQUIRE : %llu \n", shm_info[i].wcet_acquire); 
		printf("INFO - ACQUIRE : %d \n", shm_info[i].acquire); 
		printf("WCET - FILTER : %llu \n", shm_info[i].wcet_filter); 
		printf("INFO - FILTER : %d \n", shm_info[i].filter); 
		printf("WCET - CONTROLLER : %llu \n", shm_info[i].wcet_controller); 
		printf("INFO - CONTROLLER : %d \n", shm_info[i].controller); 
		printf("WCET - ACTUATOR : %llu \n", shm_info[i].wcet_actuator); 
		printf("INFO - ACTUATOR : %d \n", shm_info[i].actuator);*/
		rt_sleep(10000000);

	}
	

	rt_shm_free(SEN_SHM);
	rt_shm_free(ACT_SHM);
	rt_shm_free(REFSENS);
	rt_shm_free(INF_SHM);
	rt_shm_free(SHM_CON);
	
	for ( i = 0; i < NUM_OF_WHEELS; i++){
	
		rt_named_sem_delete(meas_avail[i]);
		rt_named_sem_delete(space_avail[i]);

	}
	
	rt_named_sem_delete(mutex_info);
	rt_sem_delete(mutex_controller);

	rt_named_mbx_delete(mbx_acq_diag_SS);
	rt_named_mbx_delete(mbx_contr_airbag);
	
	

	rt_task_delete(main_Task);
 	printf("The controller is STOPPED\n");
	return 0;
}




// Code used to test the output of rt_get_exectime   


		/*openlog("slog", LOG_PID|LOG_CONS, LOG_USER);
		 syslog(LOG_EMERG, "START: %llu ", (unsigned long long)count2nano(start[0]) );
 		closelog();

		openlog("slog", LOG_PID|LOG_CONS, LOG_USER);
		 syslog(LOG_EMERG, "END : %llu ", (unsigned long long)count2nano(end[0]) );
 		closelog();

		openlog("slog", LOG_PID|LOG_CONS, LOG_USER);
		 syslog(LOG_EMERG, "WCET: %llu ", wcet_con );
 		closelog();*/

