#include <linux/module.h>
#include <linux/moduleparam.h>
#include <asm/io.h>
#include <asm/rtai.h>
#include <rtai_sched.h>
#include <rtai_shm.h>
#include "header.h"

MODULE_LICENSE("GPL");

/*Passi da eseguire:
	1. Inizializzazione parametri e variabili
	2. Definizione funzioni di esecuzione dei 3 task generatori HRT
	3. Inizializzazione modulo kernel e tasks
	4. Eliminazione tasks e rimozione modulo kernel
*/


//---------------------------------------1-------------------------------------------
static int semiperiodi[3] = {10, 20, 40}; //Semiperiodi di default (in millisecondi)
static int arr_argc1 = 0;
module_param_array(semiperiodi, int, &arr_argc1, 0000);
MODULE_PARM_DESC(semiperiodi, "Array dei semiperiodi di default");

static int fasi[3] = {2, 1, 0};	//Sfasamenti di default	(multipli di 10ms)
static int arr_argc2 = 0;
module_param_array(fasi, int, &arr_argc2, 0000);
MODULE_PARM_DESC(fasi, "Array degli sfasamenti di default");

//Definizione task
static RT_TASK task_gen[N_TASK_GEN];

//Tempo di start e minimo semiperiodo
static RTIME start;
static RTIME min_semperiod;

//---------------------------------------2-------------------------------------------
static void generatore(long numtask) {	//Funzione dei task generatori
	
	int loops = LOOPS;

	while(loops--) {
		
		//Commutazione
		if (codice_condiviso[N_TASK_GEN-numtask-1]==1) codice_condiviso[N_TASK_GEN-numtask-1] = 0;
		else codice_condiviso[N_TASK_GEN-numtask-1] = 1;
		rt_task_wait_period();
	}

	printk("Ending task_generatore %d\n", (int) numtask);

}

//---------------------------------------3-------------------------------------------
int init_module(void) {
	
	int i;
	min_semperiod = nano2count(MIN_SEMPERIOD);

	//Inizializzazione codice condiviso
	codice_condiviso = (int*) rtai_kmalloc(ID_CODICE, sizeof(int)*N_TASK_GEN);
	if (!codice_condiviso) printk("Errore allocazione codice_condiviso!!!\n");
	
	for(i=0; i<N_TASK_GEN; i++)
		codice_condiviso[i] = 1;
	
	//Inizializzo i task generatori
	for (i=0; i<N_TASK_GEN; i++) {	
		rt_task_init_cpuid(&task_gen[i], generatore, i, STACK_SIZE, i, 0, 0, ID_CPU);	
	}
	
	//Rendo periodici i task
	start = rt_get_time() + N_TASK_GEN*min_semperiod;
	for (i=0; i<N_TASK_GEN; i++) {
		rt_task_make_periodic(&task_gen[i], start + fasi[i]*min_semperiod, (semiperiodi[i]/10)*min_semperiod);
	}

	//Schedulo i task con Rate Monotonic
	rt_spv_RMS(0);

	return 0;

}


//---------------------------------------4-------------------------------------------
void cleanup_module(void) {
	
	int i;
	printk("Delete tasks and shm...\n");
	for (i=0; i<N_TASK_GEN; i++) {
		rt_task_delete(&task_gen[i]);
	}
	rtai_kfree(ID_CODICE);
	printk("End.\n");
	
}

	
//-----------------------------------------------------------------------------------
		
		
		



	
		
		
		
		
		

	


