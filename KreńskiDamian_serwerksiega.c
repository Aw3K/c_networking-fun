#include<stdio.h>
#include<stdlib.h>
#include<sys/shm.h>
#include<sys/sem.h>
#include<signal.h>
#include<string.h>
#include<unistd.h>
#include<sys/stat.h>
#include<errno.h>

key_t 	shmkey;
int		shmid, semid, max;

//struktura slotu w pamięci
struct records {
	char name[20];
	char msg[128];
	int used;
} *shm_records;

//potrzebne do operacji na smeaforach
union semun  {
	int val;
	struct semid_ds *buf;
	ushort *array;
} arg;

void mysyserr(char *mymsg){
	printf("ERROR: %s (errno: %i, %s)\n", mymsg, errno, strerror(errno));
	exit(EXIT_FAILURE);
}

void sigHandle(int signal) {
	switch(signal) {
		case SIGTSTP:{
			char *msg[max];
			struct sembuf sbuf;
			for(int i = 0; i<max; i++) msg[i] = (char*)malloc(sizeof(char)*128);
			int any = 0;
			sbuf.sem_flg = IPC_NOWAIT;
			for(int i = 0; i<max; i++){
				errno = 0;
				sbuf.sem_num = i;
				sbuf.sem_op = -1;
				if(semop(semid, &sbuf, 1) == -1 && errno != EAGAIN)
					mysyserr("Błąd funkcji main:semop()");
				if(errno == 0) {
					if(shm_records[i].used == 1){
						any = 1;
						sprintf(msg[i], "[%s] %s\n", shm_records[i].name, shm_records[i].msg);	
					}
					sbuf.sem_op = 1;
					semop(semid, &sbuf, 1);
				}
				else if (errno == EAGAIN) sprintf(msg[i], "[SLOT%d] Zablokowany.\n", i);
			}
			if(any) {
				printf("\n___________  Księga skarg i wniosków:  ___________\n");
				for(int i = 0; i<max; i++) printf("%s", msg[i]);
			} else printf("\nKsiega skarg i wnioskow jest jeszcze pusta.\n");
			break;
		}
		case SIGINT:{
			printf("\n[Serwer]: dostałem SIGINT => kończe i sprzątam... (odłączenie: %s, usunięcie: %s SEM: usunięcie: %s)\n", 
				shmdt(shm_records) == 0 	?"OK":strerror(errno),
				shmctl(shmid, IPC_RMID, 0) == 0	?"OK":strerror(errno),
				semctl(semid, IPC_RMID, 0) == 0	?"OK":strerror(errno));
			exit(0);
		}
	}
}

int main(int argc, char * argv[]) {
	if (argc != 3){
		printf("Użycie: %s <źródło_klucza> <ilość_slotów>\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	max = atoi(argv[2]);
	ushort semvals[max+1];
	struct shmid_ds buf;
	struct sigaction sig = {0};
	sig.sa_handler = sigHandle;
	if (sigaction(SIGINT,  &sig, NULL) == -1 || sigaction(SIGTSTP, &sig, NULL) == -1)
		mysyserr("Błąd funkcji main:sigaction()");
	printf("[Serwer]: księga skarg i wniosków (WARIANT A)\n");
	printf("[Serwer]: tworze klucz na podstawie pliku %s ", argv[1]);
	if ((shmkey = ftok(argv[1], 1)) == -1)
		mysyserr("Błąd funkcji main:ftok()");
	printf(" OK (klucz: %ld)\n", shmkey);
	
	printf("[Serwer]: tworze segment pamięci wspólnej dla księgi na %d wpisow po %zub...\n", max, sizeof(struct records));
	if( (shmid = shmget(shmkey, sizeof(struct records)*max, 0600 | IPC_CREAT | IPC_EXCL| S_IRUSR | S_IWUSR)) == -1)
		mysyserr("Błąd funkcji main:shmget()");

	if ((shmctl(shmid, IPC_STAT, &buf)) == -1)
		mysyserr("Błąd funkcji main:shmctl()");
	printf("          OK (id: %d, rozmiar: %zub)\n", shmid, buf.shm_segsz);
	printf("[Serwer]: dołączam pamięć wspólną...");
	shm_records = (struct records *)shmat(shmid, 0, 0);
	if(shm_records == (struct records *)-1)
		mysyserr("Błąd funkcji main:shmat()");
	
	for(int i = 0; i<max; i++) shm_records[i].used = 0;
	printf(" OK (adres: %lX)\n", (long int)shm_records);
	
	printf("[Serwer]: tworze %d elementową grupę semaforów dla księgi...\n", max);
	if((semid = semget(shmkey, max+1, IPC_CREAT | IPC_EXCL | 0600)) == -1)
		mysyserr("Błąd funkcji main:semget()");
	printf("          OK (id: %d)\n", semid);
	for(int i = 0;i<max;i++) semvals[i] = 1;
	semvals[max] = 0;
	arg.array = semvals;
	if(semctl(semid, 0, SETALL, arg) == -1)
		mysyserr("Błąd funkcji main:semctl()");
	printf("[Serwer]: naciśnij Crtl^Z by wyświetlić stan księgi\n[Serwer]: naciśnij Crtl^C by zakończyc program\n");
	while(1) sleep(1);
	return 0;
}