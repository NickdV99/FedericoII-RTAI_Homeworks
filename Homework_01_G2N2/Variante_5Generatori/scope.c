#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <rtai_shm.h>
#include <fcntl.h>
#include <signal.h>
#include "header.h"

//Per una terminazione pulita
static int end;
static void endme(int dummy) { end = 1; }

int main() {

	struttura_condivisa * str_cond; 
	unsigned int loops = LOOPS;

	signal(SIGINT, endme);
	str_cond = rtai_malloc(ID_STRUTTURA, sizeof(struttura_condivisa));


	while (!end) {
		
		//Leggo ininterrottamente la struttura condivisa
		printf("___[OK: %d - count: %d]___\n", str_cond->OK, str_cond->count);
		usleep(BUDDY_PERIOD);

	}

	rtai_free(NOME_STRUTTURA, &str_cond);
	
	return 0;

}
		

