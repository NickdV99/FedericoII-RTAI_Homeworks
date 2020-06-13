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
	2. Definizione funzioni di esecuzione del task Riconoscitore HRT e funzione potenza
	3. Inizializzazione modulo kernel e tasks
	4. Eliminazione tasks e rimozione modulo kernel
*/


//---------------------------------------1-------------------------------------------
//Task riconoscitore
static RT_TASK task_ric;

//Puntatore a memoria condivisa
static struttura_condivisa * str_cond;

//Tempo di start e minimo semiperiodo
static RTIME start;
static RTIME min_semperiod;

//Funzione potenza
int potenza(int, int);


//---------------------------------------2-------------------------------------------
int potenza(int base, int esponente) {

	int i = 0;
	int potenza = 1;
	for (i=0; i<esponente; i++) {
		potenza = potenza*base;
	}
	return potenza;

}

static void riconoscitore(long par) {	//Funzione del task riconoscitore di sequenza

	int stato_seq = 0;
	int loops = LOOPS;
	int n;

	while(loops--) {

		n = (potenza(2,2)*codice_condiviso[0] + potenza(2,1)*codice_condiviso[1] + potenza(2,0)*codice_condiviso[2]);
		
		switch(stato_seq) {

			case 0:
				str_cond->OK = 0;
				if (n==sequenza[0]) {
					stato_seq = 1;
				}
				break;
			
			case 1:
				if (n==sequenza[1]) {
					stato_seq = 2;
				}
				else if (n==sequenza[0]) {
					stato_seq = 1;
				}
				else stato_seq = 0;
				break;

			case 2:
				if (n==sequenza[2]) {
					stato_seq = 3;
				}
				else if (n==sequenza[0]) {
					stato_seq = 1;
				}
				else stato_seq = 0;
				break;

			case 3:
				if (n==sequenza[3]) {
					stato_seq = 4;
				}
				else if (n==sequenza[0]) {
					stato_seq = 1;
				}
				else stato_seq = 0;
				break;
		
		}
	
		/*Decommento se voglio avere la conferma della correttezza del task
		printk("[%d][%d][%d]\n", codice_condiviso[0], codice_condiviso[1], codice_condiviso[2]);*/

		//Se lo stato è 4, vuol dire che è stata trovata una sequenza
		if (stato_seq==4) {
			str_cond->OK = 1;
			(str_cond->count)++;
			stato_seq = 0;
		}
		
		rt_task_wait_period();
	}

	printk("Ending task_ricevitore...\n");

}


//---------------------------------------3-------------------------------------------
int init_module(void) {
	
	min_semperiod = nano2count(MIN_SEMPERIOD);

	//Inizializzazione struttura condivisa e codice condiviso
	str_cond = (struttura_condivisa *) rtai_kmalloc(ID_STRUTTURA, sizeof(struttura_condivisa));
	if (!str_cond) printk("Errore allocazione struttura_condivisa!!!\n");
	codice_condiviso = (int *) rtai_kmalloc(ID_CODICE, sizeof(int)*N_TASK_GEN);
	if(!codice_condiviso) printk("Errore allocazione codice_condiviso!!!\n");
	
	str_cond->OK = 0;
	str_cond->count = 0;

	//Inizializzo il task riconoscitore
	rt_task_init_cpuid(&task_ric, riconoscitore, 0, STACK_SIZE, 0, 0, 0, ID_CPU);
	
	//Rendo periodico il task e lo sfaso di 5ms per evitare le discontinuità
	start = rt_get_time() + N_TASK_GEN*min_semperiod;
	rt_task_make_periodic(&task_ric, start + min_semperiod/2, min_semperiod);

	//Schedulo i task con Rate Monotonic
	rt_spv_RMS(0);

	return 0;

}


//---------------------------------------4-------------------------------------------
void cleanup_module(void) {
	
	printk("Delete task and shm...\n");
	rt_task_delete(&task_ric);
	rtai_kfree(ID_STRUTTURA);
	rtai_kfree(ID_CODICE);
	printk("End.\n");
	
}

	
//-----------------------------------------------------------------------------------
		
		
		



	
		
		
		
		
		

	


