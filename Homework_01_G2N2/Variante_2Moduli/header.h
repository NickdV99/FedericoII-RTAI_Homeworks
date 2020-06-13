#define MIN_SEMPERIOD 10000000
#define BUDDY_PERIOD 10000
#define LOOPS 1000000
#define ID_STRUTTURA 12345
#define N_TASK_GEN 3
#define STACK_SIZE 2048
#define ID_CPU 0
#define ID_CODICE 365

//Struttura dati condivisa tra il task ricevitore e il task buddy scope
typedef struct {
	
	int OK;		//Bit sequenza riconosciuta: 0 o 1
	int count;	//Contatore di sequenze riconosciute

} struttura_condivisa;

//Memoria condivisa dove memorizzare il codice condiviso tra i moduli Generatore e Riconoscitore 
int * codice_condiviso;

//Sequenza da riconoscere
int sequenza[] = {0, 3, 6, 5};
