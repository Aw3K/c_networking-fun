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
int 	shmid, semid;
struct records {
	char name[20];
	char msg[128];
	int used;
} *shm_records;

union semun  {
	int val;
	struct semid_ds *buf;
	ushort *array;
} arg;

void mysyserr(char *mymsg){
	printf("ERROR: %s (errno: %i, %s)\n", mymsg, errno, strerror(errno));
	exit(EXIT_FAILURE);
}

int main(int argc, char * argv[]) {
	if (argc != 3){
		printf("Użycie: %s <źródło_klucza> <ilość_slotów>\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	int max, used;
	size_t bufsize = 128;
	struct semid_ds semid_ds;
	struct sembuf sbuf;
	
	if ((shmkey = ftok(argv[1], 1)) == -1)
		mysyserr("Błąd funkcji main:ftok()");
	
	if ((shmid = shmget(shmkey, 0, 0)) == -1 )
		mysyserr("Błąd funkcji main:shmget()");
	
	shm_records = (struct records *) shmat(shmid, (void *)0, 0);
	if(shm_records == (struct records *)-1)
		mysyserr("Błąd funkcji main:shmat()");
	
	if((semid = semget(shmkey, 0, 0)) == -1)
		mysyserr("Błąd funkcji main:semget()");
	
	arg.buf = &semid_ds;
	if(semctl(semid, 0, IPC_STAT, arg) == -1)
		mysyserr("Błąd funkcji main:max:semctl()");
	
	max = arg.buf->sem_nsems-1;
	if((used = semctl(semid, max, GETVAL, arg)) == -1)
		mysyserr("Błąd funkcji main:used:semctl()");
	
	sbuf.sem_flg = IPC_NOWAIT;
	for(int i = 0; i<max; i++) {
		errno = 0;
		sbuf.sem_num = i;
		sbuf.sem_op = -1;
		if(semop(semid, &sbuf, 1) == -1 && errno != EAGAIN)
			mysyserr("Błąd funkcji main:semop()");
		if (errno == 0 && shm_records[i].used == 0) break;
		else if (errno == 0 && shm_records[i].used == 1) {
			sbuf.sem_op = 1;
			semop(semid, &sbuf, 1);
		}
		if(max == used || i == max-1) {
			printf("Nie ma już wolnych slotów w księdze!\n");
			exit(EXIT_FAILURE);
		}
	}
	
	printf("Klient ksiegi skarg i wnioskow wita!\n[Wolnych %d wpisow (na %d)]\nNapisz co ci doskwiera:\n", max-used, max);
	int n = read(fileno(stdin),shm_records[sbuf.sem_num].msg,bufsize);
	if(n == -1) {
		sbuf.sem_op = 1;
		semop(semid, &sbuf, 1);
		mysyserr("Błąd funkcji main:read()");
	} else {
		shm_records[sbuf.sem_num].msg[n-1]='\0';
		strcpy(shm_records[sbuf.sem_num].name, argv[2]);
		shm_records[sbuf.sem_num].used = 1;
		sbuf.sem_op = 1;
		semop(semid, &sbuf, 1);
		sbuf.sem_num = max;
		semop(semid, &sbuf, 1);
		printf("Dziekuje za dokonanie wpisu do ksiegi\n");
	}
	shmdt(shm_records);
	return 0;
}