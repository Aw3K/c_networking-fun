#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/shm.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<netdb.h>
#include<string.h>
#include<sys/stat.h>
#include<errno.h>
#include<signal.h>

int shmkey, shmid, sockfd;
char playerName[40];
char enemyIP[INET_ADDRSTRLEN];
char *PORT = "15565"; //ulubiona liczba w 2 postaciach potrzebne, bo rózne argumenty funkcji
u_short PORTu = 15565;
struct addrinfo hints, *res, *tmp; // przechowują wynik getaddrinfo i umozliwiają komunikację 
struct sockaddr_in server_addr; // do bind adresu
pid_t worker;

int winCon[8][3] = {
	{0,1,2},
	{3,4,5},
	{6,7,8},
	{0,3,6},
	{1,4,7},
	{2,5,8},
	{0,4,8},
	{2,4,6}
}; //warunki zwycięstwa

struct sharedDataStruct {
	char board[10];
	char enemyName[40];
	int myTurn;
	int connected;
	pid_t pidWorker;
	int scores[2];
} *sharedData; // struktura pamięci współdzielonej

struct dataUDPStruct {
	int system;
	int spot;
	char playerName[40];
} dataUDP, Msg; // struktura do przesyłania danych poprzez UDP

void showBoard(){ // wyświetla plansze
	for (int i = 0; i<9; i+=3){
		printf("%c | %c | %c\n", sharedData->board[i],sharedData->board[i+1],sharedData->board[i+2]);
	}
}

void checkIfWon() { // sprawdza czy wygraliśmy lub remis i resetuje plansze
	for(int i = 0; i<8; i++){
		if((sharedData->board[winCon[i][0]] == sharedData->board[winCon[i][1]]) && (sharedData->board[winCon[i][1]] == sharedData->board[winCon[i][2]])) {
			if(sharedData->board[winCon[i][0]] == 'X') {
				sharedData->scores[0]++;
				printf("[Wygrana! Kolejna rozgrywka, poczekaj na swoja kolej]\n");
				sharedData->myTurn = 0; // wygrałeś - nie zaczynasz
				strcpy(sharedData->board, "123456789");
			}
			else {
				sharedData->scores[1]++;
				printf("[Pregrana! Zagraj jeszcze raz]\n");
				sharedData->myTurn = 1; // przegrałeś - zaczynasz pierwszy, chyba uczciwe
				strcpy(sharedData->board, "123456789");
			}
			break;
		}
	}
	int remis = 1;
	for(int i = 0; i<9; i++) {
		if((sharedData->board[i] - '0') == i+1) {
			remis = 0;
		}
	}
	if (remis) {
		printf("[Remis! Kolejna rozgrywka]\n");
		strcpy(sharedData->board, "123456789");
	}
}

void UDPMsg(int sys, int spot) { // funkcja do wysyłania wiadomości systemowej i zwykłej
	Msg.system = sys;
	Msg.spot = spot;
	strcpy(Msg.playerName, playerName);
	for (tmp = res; tmp != NULL; tmp = tmp->ai_next) // wysyła wiadomosc na adresy zwrócone przez getaddrinfo
	{
		if (sendto(sockfd, &Msg, sizeof(Msg), 0, tmp->ai_addr, tmp->ai_addrlen) != -1)
		{
			break;
		}
	}
}

void resetShared(){ // wartości domyślne pamięci wspóldzielonej
	sharedData->myTurn = 0;
	sharedData->connected = 0;
	strcpy(sharedData->board, "123456789");
	sharedData->scores[0] = 0;
	sharedData->scores[1] = 0;
}

void clearApp(){ // czyszczenie apki przed wyłaczeniem 
	printf("[CZYSZCZENIE]\n[zabicie: %s]\n", kill(sharedData->pidWorker, 9) == 0 ?"OK":strerror(errno));
	printf("[odłączenie: %s]\n[usunięcie: %s]\n", shmdt(sharedData) == 0 ?"OK":strerror(errno), shmctl(shmid, IPC_RMID, 0) == 0	?"OK":strerror(errno));
	close(sockfd);
}

void mysyserr(char *mymsg){ //standardowa funckcja błedu errno
	printf("ERROR: %s (errno: %i, %s)\n", mymsg, errno, strerror(errno));
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
	if (argc < 2 || argc > 3) { // parametry aplikacji
		printf("Usage: %s <ADDRESS> <NAME>\n", argv[0]);
		exit(EXIT_FAILURE);
	} else {
		struct sigaction sig = {0};
		sig.sa_handler = clearApp; // mały myk by w przypadku ctr^C apka się "wyczyściła"
		if (sigaction(SIGINT, &sig, NULL) == -1)
			mysyserr("main:sigaction()");
		
		if (argc == 2) strcpy(playerName, "NN"); // ustalanie i kopiowanie nazwy gracza
		else strcpy(playerName, argv[2]);
		
		bzero(&hints, sizeof(hints)); // wypełnianie struktury do getaddrinfo
		hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = 0;
		hints.ai_flags = 0;
		int s;
        if ((s = getaddrinfo(argv[1], PORT, &hints, &res)) != 0) {
            printf("ERROR main:getaddrinfo(): %s\n", gai_strerror(s));
            exit(EXIT_FAILURE);
        }
		struct sockaddr_in *addr = ((struct sockaddr_in *) res->ai_addr);
		if((inet_ntop(AF_INET, &addr->sin_addr, enemyIP, INET_ADDRSTRLEN)) == NULL)			// wyciąganie adresu ip przeciwnika w postaci standardowej z kropkami
			mysyserr("main:inet_ntop()");
		
		if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) // ustawianie socketu pod komunikację
			mysyserr("main:socket()");
		int tmpp = 1;
		if((setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &tmpp, sizeof(tmpp))) == -1) // dodanie flagi "SO_REUSEADDR" do socketu, można dzięki temu operować kilkoma adresami na jednym sockecie, bez tego nie działa
			mysyserr("main:socket()");
		
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		server_addr.sin_port = htons(PORTu);
		if((bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr))) == -1) // wypełnianie struktury i funkcja bind - nasłuchiwanie na ustalonym porcie komunikatów
			mysyserr("main:bind()");
	}
	
	if ((shmkey = ftok(argv[0], atoi(enemyIP))) == -1) // ftok używa tutaj dla unikalności klucz generowany z nazwy pliku i postaci liczbowej IP przeciwnika
		mysyserr("main:ftok()");
	if ((shmid = shmget(shmkey, sizeof(struct sharedDataStruct), 0600 | IPC_CREAT | S_IRUSR | S_IWUSR)) == -1) // standardowe funkcje do tworzenia i podłączania pamięci wspóldzielonej, dokładnie to samo co w 2 projekcie
		mysyserr("main:shmget()");
	sharedData = (struct sharedDataStruct *)shmat(shmid, 0, 0);
	if(sharedData == (struct sharedDataStruct *)-1)
		mysyserr("main:shmat()");
	resetShared();
	
	worker = fork();
	if(worker == 0) { // tworzenie procesu do słuchania - worker
		sharedData->pidWorker = getpid(); // zapisuje pid do czyszczenia
		while(1){
			if ((recvfrom(sockfd, &dataUDP, sizeof(dataUDP), 0, NULL, NULL)) == -1) { // odbieranie komunikatów UDP
				mysyserr("błąd funckji worker:recvfrom()");
			}
			strcpy(sharedData->enemyName, dataUDP.playerName);
			if(dataUDP.system == 0 && sharedData->connected == 0) { // komunikat UDP:0 - po otrzymaniu wiadmo że jesteśmy pierwsi i że ktoś może z nami zagrać
				sharedData->connected = 1;
				sharedData->myTurn = 1;
				UDPMsg(1, 0); // wysyła sygnał (potwierdzenie) do złapania - UDP:1
				strcpy(sharedData->board, "123456789");
				sharedData->scores[0] = 0;
				sharedData->scores[1] = 0;
				printf("[%s (%s) dolaczyl do gry]\n", sharedData->enemyName, enemyIP);
				showBoard();
				printf("[wybierz pole] ");
			} else if (dataUDP.system == 1 && sharedData->connected == 0) { // komunikat UDP:1 - zwrotny do UDP:0 - potwierdzenie otrzymania 1 i ustala kolejność ruchu
				sharedData->myTurn = 0;
				sharedData->connected = 1;
				printf("[dołączono do gry z: %s (%s)]\n", sharedData->enemyName, enemyIP);
			} else if (dataUDP.system == -1) { // komunikat UDP:-1 - informacja o zakończeniu gry, przeciwnik wpisał <koniec>
				printf("\n[%s (%s) zakonczyl gre, mozesz poczekac na kolejnego gracza]\n", sharedData->enemyName, enemyIP);
				sharedData->connected = 0; // by znowu czekać na połączenie gracza
			} else if (dataUDP.system == 2) { // komunikat UDP:2 - ruch przeciwnika, standardowa komunikacja do gry
				sharedData->board[dataUDP.spot] = 'O';
				printf("[%s (%s) wybral pole %d]\n", sharedData->enemyName, enemyIP, dataUDP.spot+1);
				checkIfWon(); // ważna informacja: nie ma tutaj żadnego podziału szczególnego kto jest X a kto O, każdy gracz widzi siebie jako X a przeciwnika jako O - co nie ma żadnego znaczenia, wyniki są poprawne
				showBoard();
				printf("[wybierz pole] ");
				sharedData->myTurn = 1;
			}
			fflush(stdout); // dla pewności czy nic nie zostało na stdout
		}
	} else {
		printf("Rozpoczynam gre z %s. Napisz <koniec> by zakonczyc.\n", enemyIP);
		UDPMsg(0, 0); // wysyła sygnał do złapania - UDP:0
		printf("[Propozycja gry wyslana]\n");
		char input[10];
		while(scanf("%s", input)) { // pętla w programie rodzica do komend i ruchów
			if (!strcmp(input, "<koniec>")) {
				UDPMsg(-1, 0); // wysyła sygnał do złapania - UDP:-1
				break;
			} else if (!strcmp(input, "<wynik>")) {
				printf("Ty %d : %d %s\n", sharedData->scores[0], sharedData->scores[1], sharedData->enemyName); 
			} else if (atoi(input) > 0 && atoi(input) < 10) {
				if (sharedData->connected == 0) printf("[Nie ma drugiego gracza, czekaj]\n");
				else {
					if (sharedData->myTurn == 0) {
						printf("[teraz kolej drugiego gracza, poczekaj na swoja kolej]\n");
					} else {
						if(sharedData->board[atoi(input)-1] == input[0]) { 
							sharedData->board[atoi(input)-1] = 'X';
							checkIfWon();
							UDPMsg(2, atoi(input)-1); // wysyła sygnał do złapania - UDP:2, nasz ruch
							sharedData->myTurn = 0;
						} else {
							printf("[tego pola nie mozesz wybrac, wybierz pole] ");
						}
					}
				}
			} else {
				printf("[Błędne wejscie]\n[wybierz pole] ");
			}
			fflush(stdout);
			input[0] = '\0';
		}
	}
	clearApp(); // sprzątanko
	return 0;
}