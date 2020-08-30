#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "semphr.h"

/*Passi da eseguire:
	1. Definizione macro e variabili globali
	2. Definizione funzioni di esecuzione dei 4 task e funzione potenza
	3. Definizione Tasks
	4. Main
		4.1 Esecuzione 
		4.2 Pulizia variabili temporanee e stop scheduling
*/


/*-----------------------------------1-----------------------------------*/
/* Macro */
#define mainLOOPS 1000000				/* Numero di cicli dei generatori */
#define mainMIN_SEMPERIOD 10			/* Minimo semiperiodo, in ms */
#define mainQUARTER_PERIOD 5			/* Mezzo semiperiodo, in ms */
#define mainBUDDY_PERIOD 10				/* Periodo del task Scope, in ms */
#define mainN_TASK_GEN 3				/* Numero di task generatori */	
/* Priorità massima tra i generatori */
#define mainMAX_GEN_PRIORITY  tskIDLE_PRIORITY + mainN_TASK_GEN

/* Struttura dati da mostrare in output */
struct {

	uint8_t uOK;		//Bit sequenza riconosciuta: 0 o 1
	uint16_t usCount;	//Contatore di sequenze riconosciute

} Struttura_Output;

/* Struttura dati contenente le informazioni di un generatore */
typedef struct {

	uint8_t uNumTask;	/* Numero del task */
	uint8_t uFase;		/* Sfasamento (multiplo di 10 ms) */
	uint8_t uPeriodo;	/* Periodo del task */

} Struttura_Generatore;

/* Sequenza da riconoscere */
uint8_t uSequenza[] = { 0, 3, 6, 5 };

/* Mutex per l'accesso al codice generato */
SemaphoreHandle_t xMutex_Codice;

/* Codice a 3 bit di codifica delle onde quadre (inizializzato a [1][1][1]).
   Task generatore n.N_TASK_GEN (periodo più lungo) -> bit 0 del codice */
uint8_t uCodice[] = { 1, 1, 1 };

/* Minimo semiperiodo e sua metà */
TickType_t xMin_semperiod = pdMS_TO_TICKS(mainMIN_SEMPERIOD);
TickType_t xQuarterPeriod = pdMS_TO_TICKS(mainQUARTER_PERIOD);


/*-----------------------------------2-----------------------------------*/
/* Funzione potenza (nel nostro caso bastano 8 bit) */
uint8_t uPotenza(uint8_t uBase, uint8_t uEsponente) {

	uint8_t uCounter = 0;
	uint8_t uRisultato = 1;
	for (uCounter = 0; uCounter < uEsponente; uCounter++) {
		uRisultato = uRisultato * uBase;
	}
	if (uEsponente == 0) return 1;
	else return uRisultato;

}


/* Funzione dei task generatori. Per rispettare il rate monotonic
   e gli sfasamenti dell'H1, la priorità è assegnata in base al periodo 
   mentre, per le fasi, non avendo a disposizione lo start time come in RTAI, 
   i task vengono sfasati sfruttando la vTaskDelay() */
static void prvGeneratore(void * pvStruttura_par) {	

	Struttura_Generatore SG = *((Struttura_Generatore*)pvStruttura_par);

	/* Attendo per lo sfasamento iniziale */
	vTaskDelay(SG.uFase * xMin_semperiod);

	/* Inizializzazioni varie */
	TickType_t xNextWakeTime = xTaskGetTickCount();
	const TickType_t xBlockTime = pdMS_TO_TICKS(SG.uPeriodo);
	uint32_t ulLoops = mainLOOPS;

	while (ulLoops--) {

		/* Acquisizione mutex */
		xSemaphoreTake(xMutex_Codice, portMAX_DELAY);

		/* Commutazione */
		if (uCodice[mainN_TASK_GEN - SG.uNumTask] == 1) 
			uCodice[mainN_TASK_GEN - SG.uNumTask] = 0;
		else uCodice[mainN_TASK_GEN - SG.uNumTask] = 1;

		/* Rilascio mutex */
		xSemaphoreGive(xMutex_Codice);

		/* Simulazione task periodico */
		vTaskDelayUntil(&xNextWakeTime, xBlockTime);

	}

	printf("Ending task generatore %d\n", SG.uNumTask);

}


/* Funzione del task riconoscitore di sequenza. Esso partirà 5ms
   dopo l'inizio del generatore con periodo più lungo, in accordo
   con l'H1 */
static void prvRiconoscitore(void * pvParameter) {

	/* Inizializzazioni varie */
	uint8_t uStato_seq = 0;
	uint32_t ulLoops = mainLOOPS;
	uint8_t uValue;
	TickType_t xNextWakeTime = xTaskGetTickCount();
	const TickType_t xBlockTime = pdMS_TO_TICKS(mainMIN_SEMPERIOD);
	
	/* Attendo per lo sfasamento iniziale */
	vTaskDelay(xQuarterPeriod); 

	while (ulLoops--) {

		/* Lettura del codice generato */
		xSemaphoreTake(xMutex_Codice, portMAX_DELAY);
		uValue = (uPotenza(2, 2) * uCodice[0] + uPotenza(2, 1) * uCodice[1] + uPotenza(2, 0) * uCodice[2]);
		xSemaphoreGive(xMutex_Codice);

		/* Riconoscimento valore della sequenza */
		switch (uStato_seq) {

			case 0:
				Struttura_Output.uOK = 0;
				if (uValue == uSequenza[0]) {
					uStato_seq = 1;
				}
				break;

			case 1:
				if (uValue == uSequenza[1]) {
					uStato_seq = 2;
				}
				else if (uValue == uSequenza[0]) {
					uStato_seq = 1;
				}
				else uStato_seq = 0;
				break;

			case 2:
				if (uValue == uSequenza[2]) {
					uStato_seq = 3;
				}
				else if (uValue == uSequenza[0]) {
					uStato_seq = 1;
				}
				else uStato_seq = 0;
				break;

			case 3:
				if (uValue == uSequenza[3]) {
					uStato_seq = 4;
				}
				else if (uValue == uSequenza[0]) {
					uStato_seq = 1;
				}
				else uStato_seq = 0;
				break;

		}

		/* Decommento se voglio avere la conferma della correttezza del task 
		xSemaphoreTake(xMutex_Codice, portMAX_DELAY);
		printf("[%d][%d][%d]\n", uCodice[0], uCodice[1], uCodice[2]); 
		xSemaphoreGive(xMutex_Codice);*/

		/* Se lo stato è 4, vuol dire che è stata trovata una sequenza */
		if (uStato_seq == 4) {
			Struttura_Output.uOK = 1;
			(Struttura_Output.usCount)++;
			uStato_seq = 0;
		}

		/* Simulazione task periodico */
		vTaskDelayUntil(&xNextWakeTime, xBlockTime);

	}

	printf("Ending task riconoscitore...\n");

}


/* Funzione task Scope */
static void prvScope(void * pvParameter) {

	uint32_t ulLoops = mainLOOPS;

	while (ulLoops--) {

		TickType_t xNextWakeTime = xTaskGetTickCount();
		const TickType_t xBlockTime = pdMS_TO_TICKS(mainMIN_SEMPERIOD);

		/* Stampa a video Struttura Output */
		printf("[____OK: %d____|____Count: %d____]\n", Struttura_Output.uOK, Struttura_Output.usCount);

		/* Simulazione task periodico */
		vTaskDelayUntil(&xNextWakeTime, xBlockTime);

	}

	printf("Ending task scope...\n");
}


/*-----------------------------------3-----------------------------------*/
void main_blinky(void) {
	
	/* Contatore di utilità */
	uint8_t uCounter;

	/* Inizializzazione Struttura di output */
	Struttura_Output.uOK = 0;
	Struttura_Output.usCount = 0;

	/* Array di puntatori a strutture dati per i generatori */
	Struttura_Generatore * Struct_Gen = (Struttura_Generatore *)pvPortMalloc(mainN_TASK_GEN * sizeof(Struttura_Generatore));

	/* Inizializzazione Mutex per l'accesso al codice generato */
	xMutex_Codice = xSemaphoreCreateMutex();
	
	/* Inizializzazione task generatori */
	TaskHandle_t xGeneratori[mainN_TASK_GEN];
	for (uCounter = 0; uCounter < mainN_TASK_GEN; uCounter++) {
		Struct_Gen[uCounter].uFase = (mainN_TASK_GEN - uCounter - 1);
		Struct_Gen[uCounter].uPeriodo = uPotenza(2, uCounter) * mainMIN_SEMPERIOD;
		Struct_Gen[uCounter].uNumTask = uCounter + 1;
		BaseType_t xGenCreated = xTaskCreate(prvGeneratore, "Generatore", configMINIMAL_STACK_SIZE, (void *)&Struct_Gen[uCounter],
										     tskIDLE_PRIORITY + uCounter + 1, &(xGeneratori[uCounter]));
	}

	/* Inizializzazione task riconoscitore */
	TaskHandle_t xRiconoscitore;
	BaseType_t xRicCreated = xTaskCreate(prvRiconoscitore, "Riconoscitore", configMINIMAL_STACK_SIZE, 0,
		mainMAX_GEN_PRIORITY + 1, &xRiconoscitore);

	/* Inizializzazione Buddy Task per l'output */
	TaskHandle_t xScope;
	BaseType_t xScopeCreated = xTaskCreate(prvScope, "Scope", configMINIMAL_STACK_SIZE, 0, tskIDLE_PRIORITY, &xScope);

	/* Avvio lo scheduler di FreeRTOS*/
	vTaskStartScheduler();

	/* Pulizia variabili dinamiche/temporanee e stop scheduling */
	printf("\nEsecuzione terminata. Pulizia memoria...");
	for (uCounter = 0; uCounter < mainN_TASK_GEN; uCounter++) {
		vTaskDelete(xGeneratori[uCounter]);
	}
	vTaskDelete(xRiconoscitore);
	vSemaphoreDelete(xMutex_Codice);
	vPortFree(Struct_Gen);
	printf("\nEnd...");

}
