#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>

//funkcja błędów z errno
void mysyserr(char *mymsg){
	printf("ERROR: %s (errno: %i, %s)\n", mymsg, errno, strerror(errno));
}
//przeszukuje pokolei pliki w folderze i ustawia maksymalne szerokości pól do formatowania wyjścia w tablicy lengths
void checkFormatting(int lengths[], int *max){
	DIR *dp;
	struct dirent *x;
	struct stat info;
	char *n1 = (char*)malloc(sizeof(char)*32), *n2 = (char*)malloc(sizeof(char)*32);
	int sum = 0;

	if ((dp = opendir(".")) == NULL) lengths[0] = -1;
	else {
		while((x = readdir(dp)) != NULL) {
			sum++;
			if (errno != 0) mysyserr("Błąd funkcji checkFormatting:readdir()");
			else {
				errno = 0;
				if((lstat(x->d_name, &info)) == -1) mysyserr("Błąd funkcji checkFormatting:lstat()");
				else {
					sprintf(n1, "%lu", info.st_nlink); //zmiana z intu na char*
					sprintf(n2, "%lu", info.st_size);
					if(strlen(n1) > lengths[0]) lengths[0] = strlen(n1); //ilości linków do pliku
					if(strlen((getpwuid(info.st_uid))->pw_name) > lengths[1]) lengths[1] = strlen((getpwuid(info.st_uid))->pw_name); //nazwa własciciela pliku
					if(strlen((getgrgid(info.st_gid))->gr_name) > lengths[2]) lengths[2] = strlen((getgrgid(info.st_gid))->gr_name); //grupa właściciela pliku
					if(strlen(n2) > lengths[3]) lengths[3] = strlen(n2); //wielkość pliku
				}
			}
		}
	}
	*max = sum;
	closedir(dp);
}

//zwraca wskaźnik - 11 elementową tablice chara z uprawieniami poprzez podanie st_mode (struct stat)
char* permissions(mode_t x) {
	char *out = (char*)malloc(sizeof(char)*11);
	switch (x & S_IFMT) {
		case S_IFDIR:  out[0] = 'd'; break;
		case S_IFLNK:  out[0] = 'l'; break;
		default: out[0] = '-'; break;
	}
	out[1] = ((x & S_IRUSR) ? 'r' : '-');
    out[2] = ((x & S_IWUSR) ? 'w' : '-');
    out[3] = ((x & S_IXUSR) ? 'x' : '-');
    out[4] = ((x & S_IRGRP) ? 'r' : '-');
    out[5] = ((x & S_IWGRP) ? 'w' : '-');
    out[6] = ((x & S_IXGRP) ? 'x' : '-');
    out[7] = ((x & S_IROTH) ? 'r' : '-');
    out[8] = ((x & S_IWOTH) ? 'w' : '-');
    out[9] = ((x & S_IXOTH) ? 'x' : '-');
	out[10] = '\0';
	return out;
}
//zwraca wskaźnik - ścieżka do pliku na który wskazuje link symboliczny
char* pwdLink(char *name, int size) {
	char *linkname = (char*)malloc(size + 1);
	if ((readlink(name, linkname, size + 1)) == -1) {
		mysyserr("Błąd funkcji pwdLink:readlink()");
		return "NULL";
	}
	else {
		linkname[size] = '\0';
		return linkname;
	}
}
//zwraca wskaźnik - sforamtowany tekst daty z polską nazwą miesiąca lub NULL w przypadku błędu
char* timeFormatW(time_t *x) {
	char *months[12] = {"Stycznia", "Lutego", "Marca", "Kwietnia", "Maja", "Czerwca", "Lipca", "Sierpnia", "Września", "Października", "Listopada", "Grudnia"};
	char tmp[64], *dateout = (char*)malloc(sizeof(char)*64);
	struct tm *ty = localtime(x);
	errno = 0; tmp[0] = 0; dateout[0] = 0;
	
	strftime(tmp, 64, "%d ", ty);
	strcat(dateout, tmp);
	strcat(dateout, months[ty->tm_mon]);
	strftime(tmp, 64, " %Y roku o %H:%M:%S", ty);
	strcat(dateout, tmp);
	
	if (errno != 0) {
		mysyserr("Błąd funkcji timeFormatW()");
		return "NULL";
	} else return dateout;
}
//zwraca wskaźnik, polski tym pliku
char* switchType(char x){
	switch (x){
		case 'l': return "link symboliczny"; break;
		case '-': return "zwykły plik";	     break;
		case 'd': return "katalog";          break;
	}
	return "NULL";
}
//zwraca wskaźnik, poprawny opis rozmiaru pliku
char* sizeFormat(long int x) {
	if (x == 1) return "bajt";
	else if (x >= 2 && x <= 4) return "bajty";
	else return "bajtów";
}
//funckja porównująca używana w qsort, liczy wystąpienia '.' w wyrazach i przesuwa wskaźnik do porównania - ignoruje '.' i wielkości liter
int compare(const void *s1, const void *s2){
	char *x = *((char**)s1), *y = *((char**)s2);
	int xp = 0, yp = 0;
	while(x[xp] == '.') {
		if (x[xp+1] == '\0') {
			xp = 0;
			break;
		}
		xp++;
	}
	while(y[yp] == '.') {
		if (y[yp+1] == '\0') {
			yp = 0;
			break;
		}
		yp++;
	}
	return strcasecmp(x+xp, y+yp);
}

int main(int argc, char *argv[]) {
	char *perms; //wskaźnik na permisje
	struct stat info; //struktura przechowująca informacje o pliku (lstat)
	if (argc == 1) { // 1 tryb
		int lengths[] = {0,0,0,0}, max = 0, curline = 0;
		checkFormatting(lengths, &max); // sprawdzanie formatowania

		DIR *dp;// otwieranie katalogu
		if ((dp = opendir(".")) == NULL || lengths[0] == -1) { 
			mysyserr("Błąd funkcji main:opendir()");
			return -1;
		}
		
		struct dirent *x;
		char **files = (char**)malloc(sizeof(char*)*max), *tmp = (char*)malloc(sizeof(char)*PATH_MAX);
		
		while((x = readdir(dp)) != NULL) {
			if (errno != 0) mysyserr("Błąd funkcji main:readdir()");
			else {
				errno = 0;
				files[curline] = (char*)malloc(sizeof(char)*strlen(x->d_name));
				strcpy(files[curline], x->d_name); // listowanie plików do tablicy wskaźników
				curline++;
			}
		}
		closedir(dp);
		
		struct timespec current;
		char *dateout = (char*)malloc(sizeof(char)*16);
		qsort(files, max, sizeof(char*), compare); //sortowanie listy plików
		for(int i = 0; i<max; i++) {
			tmp[0] = 0;
			if((lstat(files[i], &info)) == -1) mysyserr("Błąd funkcji main:lstat()");
			else {
				perms = permissions(info.st_mode); // permisje pliku
				clock_gettime(CLOCK_REALTIME, &current); // aktualny czas
				if ((current.tv_sec - (info.st_mtim).tv_sec) > 15500000) strftime(dateout, 16, "%b %d %5Y", localtime(&(info.st_mtim).tv_sec)); 
				else strftime(dateout, 16, "%b %d %H:%M", localtime(&(info.st_mtim).tv_sec)); //porównywanie aktualnego czasu i zwracanie odpowiedniego formatu do dateout (starsze niż 6 miesiecy - pokazuje rok)
				strcat(tmp, files[i]);
				if(perms[0] == 'l') {
					strcat(tmp, " -> "); // jeżeli link sybomliczny, dodaje informacje gdzie wskazuje na końcu linii
					strcat(tmp, pwdLink(files[i], info.st_size));
				}
				printf("%10s %*lu %*s %*s %*lu %s %s\n", perms, lengths[0], info.st_nlink, lengths[1], (getpwuid(info.st_uid))->pw_name, lengths[2], (getgrgid(info.st_gid))->gr_name, lengths[3], info.st_size, dateout, tmp);
			}	// wyświetla linię z informacjami o pliku
		}
	} else if (argc == 2) { // 2 tryb
		size_t bufsize = 256;
		char *dir = (char*)malloc(sizeof(char)*PATH_MAX), *buf = (char *)malloc(sizeof(char)*bufsize+1);
		char *comTime[3] = {"Ostatnio używany:        ", "Ostatnio modyfikowany:   ", "Ostatnio zmieniany stan: "};
		int n;
		FILE *f;
		
		if((lstat(argv[1], &info)) == -1) mysyserr("Błąd funkcji main:lstat()"); // odczytywanie informacji o pliku
		else {
			perms = permissions(info.st_mode); // permisje pliku
			printf("Informacje o %s:\nTyp pliku:   %s\n", argv[1], switchType(perms[0]));
			
			getcwd(dir, PATH_MAX); // zwraca katalog roboczy (current working directory) - używam by wypisać ścieżkę pliku
			printf("Scieżka:     %s/%s\n", dir, argv[1]);
			
			if(perms[0] == 'l')
				printf("Wskazuje na: %s\n", pwdLink(argv[1], info.st_size)); // wyświetla  na co wskazuję - link symboliczny
			
			printf("Rozmiar:     %lu %s\nUprawnienia: %s\n", info.st_size, sizeFormat(info.st_size), perms+1); // rozmiar pliku wraz z polskim formatowaniem
			
			time_t times[3] = {(info.st_atim).tv_sec, (info.st_mtim).tv_sec, (info.st_ctim).tv_sec}; // tablica przechowująca informacje o 3 czasach: uzycia, modyfikacji i stanu z struktury stat
			for(int i = 0; i<3; i++)
				printf("%s%s\n", comTime[i], timeFormatW(&(times[i]))); // wypisywanie sformatowanego czasu
			
			if (perms[0] == '-' && perms[3] == '-' && perms[6] == '-' && perms[9] == '-') { // gdy zwykły plik bez uprawień odczytu wypisz 2 pierwsze linijki
				if((f = fopen(argv[1], "r")) == NULL) // zwraca stream do pliku o podanej nazwie i trybie (odczyt)
					mysyserr("Błąd funkcji main:fopen()");
				else {
					if (f == NULL) mysyserr("Błąd funkcji main:fdopen()");
					else {
						printf("Początek zawartości:\n");
						for(int i = 0; i<2; i++) { // 2 linijki
							buf[0] = 0; // zeruje bufor i błędy
							errno = 0;
							if ((n = getline(&buf, &bufsize, f)) == -1 && errno != 0) { // wczytuje linie do buf o max szerokości buffsize używa streamu FILE f
								mysyserr("Błąd funkcji main:getline()");
								break;
							} else {
								if(buf[n-1] != '\n') { // jeżeli przed ostatni znak wyjściowego ciagu nie jest nową linią to to ustaw znam nowej linni i końca
									buf[n+1] = '\0';   // zabieg ten ma rozwiązać problemy błędu gdy w pliku wskazanym jest tylko jedna linia bez końca znaku
									buf[n] = '\n';
								} else buf[n] = '\0'; // w przeciwnym wypadku sam znak końca 
								printf("%s", buf); // wypisuje linie
							}
						}
					}
				}
			}
		}
	} else printf("Usage: %s <filename>\n", argv[0]); // zły tryb
	return 0;
}