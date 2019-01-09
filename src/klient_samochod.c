/**
 *  Copyright 2016-2017 Kamil Biały <sobiemir@aculo.pl>
 *                      sobiemir
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <sys/msg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/shm.h>
#include <pthread.h>
#include <stdlib.h>

#define MESSAGE_KEY  020626     // identyfikator kolejki komunikatów
#define SHARED_KEY   060226     // identyfikator pamięci współdzielonej
#define CSHARED_KEY  022606     // identyfikator pamięci współdzielonej dla samochodów
#define MESSAGE_SIZE 256        // rozmiar bufora wiadomości w kolejce komunikatów

#define MSGSTR_SIZE (256 * sizeof(char) + sizeof(int))

// kolory w konsoli...
#define CLEAR_LINE         "\033[2K\r"
#define COLOR_GREEN(_T_)   "\033[0;32;32m" _T_ "\033[0m"
#define COLOR_CYAN(_T_)    "\033[0;36m"    _T_ "\033[0m"
#define COLOR_RED(_T_)     "\033[0;32;31m" _T_ "\033[0m"
#define COLOR_YELLOW(_T_)  "\033[0;33m"    _T_ "\033[0m"
#define COLOR_MAGNETA(_T_) "\033[0;35m"    _T_ "\033[0m"
#define COLOR_BLUE(_T_)    "\033[0;32;34m" _T_ "\033[0m"

// maksymalna ilość samochodów - pobierana z serwera
unsigned int maxcars = 0;

// spowolnienie wykonywania programu
unsigned int speeddown = 1;

// identyfikatory kolejek i pamięci współdzielonej
int msqid;
int shmid;
int shmidc;
int mnpid;

// przerwa pomiędzy losowaniami kolejnych samochodów
int randinterval = 35;

// ilość samochodów
unsigned int numcars = 0;

/**
 * Struktura informacji o samochodzie.
 * Większość informacji kopiowana jest ze struktury DIR_INFO.
 * Każdy samochód posiada interwał dla migacza.
 */
struct CAR_INFO
{
	int x, y, dir, blinker;     // x, y, kierunek i migacz
	int width, height;          // wysokość i szerokość
	int next, xm, ym;           // kierunek w który samochód będzie skręcał
	int xmax, ymax;             // linia skrętu X i Y
	int color;                  // indeks koloru samochodu
	int active;                 // czy samochód jest aktywny
	int before, after;          // indeks samochodu przed i pod
	int interval;               // licznik dla kierunkowskazu
	pid_t pid;                  // identyfikator procesu
};

// samochody
struct CAR_INFO *cars;
int             *light_state;

/**
 * Struktura wysyłanych wiadomości do klienta lub serwera.
 * Dokładny opis struktury znajduje się w pliku z serwerem.
 */
struct MSG_MESSAGE
{
	long Type;                  // typ wiadomości
	int  Msqt;                  // typ wewnętrzny wiadomości
	char Message[MESSAGE_SIZE]; // wiadomość do wyświetlenia
};

// identyfikator wątku
pthread_t movethr;
int can_run = 1;

// struktura kierunków
struct DIR_INFO
{
	int x, y, blinker, xm, ym, width, height, next, xmax, ymax, stopx, stopy, lastcar;
}
direction[12] =
{
	// kierunek 0 (jedzie w lewo, skręca w lewo)
	{ -15, 246, 1, 1, 0, 15, 10, 8, 326, 0, 192, 0, -1 },
	
	// kierunek 1 (jedzie w lewo, skręca w prawo)
	{ -15, 286, 2, 1, 0, 15, 10, 5, 265, 0, 192, 0, -1 },
	
	// kierunek 2 (jedzie w lewo, na prosto)
	{ -15, 266, 0, 1, 0, 15, 10, 2, 0, 0, 192, 0, -1 },
	
	// kierunek 3 (jedzie w dół, skręca w lewo)
	{ 305, -15, 1, 0, 1, 10, 15, 2, 0, 246, 0, 130, -1 },
	
	// kierunek 4 (jedzie w dół, skręca w prawo)
	{ 265, -15, 2, 0, 1, 10, 15, 11, 0, 185, 0, 130, -1 },
	
	// kierunek 5 (jedzie w dół, na prosto)
	{ 285, -15, 0, 0, 1, 10, 15, 5, 0, 0, 0, 130, -1 },
	
	// kierunek 6 (jedzie do góry, skręca w lewo)
	{ 326, 495, 1, 0, -1, 10, 15, 11, 0, 225, 0, 334, -1 },
	
	// kierunek 7 (jedzie do góry, skręca w prawo)
	{ 366, 495, 2, 0, -1, 10, 15, 2, 0, 286, 0, 334, -1 },
	
	// kierunek 8 (jedzie do góry, na prosto)
	{ 346, 495, 0, 0, -1, 10, 15, 8, 0, 0, 0, 334, -1 },
	
	// kierunek 9 (jedzie w prawo, skręca w lewo)
	{ 655, 225, 1, -1, 0, 15, 10, 5, 305, 0, 416, 0, -1 },
	
	// kierunek 10 (jedzie w prawo, skręca w prawo)
	{ 655, 185, 2, -1, 0, 15, 10, 8, 366, 0, 416, 0, -1 },
	
	// kierunek 11 (jedzie w prawo, na prosto)
	{ 655, 205, 0, -1, 0, 15, 10, 11, 0, 0, 416, 0, -1 }
};

/**
 * Zamiana ciągu znaków na typ liczbowy.
 * 
 * @param str Ciąg znaków do zamiany.
 * @param npos Ostatnia przetwarzana w str pozycja.
 * 
 * @return Liczba pobrana z ciągu znaków.
 */
unsigned int string_to_uint( const char *str, char **npos )
{
	unsigned int num = 0;
	size_t dig = *str;

	while( dig >= '0' && dig <= '9' )
		num = num * 10 + (dig - '0'), dig = *(++str);

	// zapisz nową pozycję
	if( npos != NULL )
		*npos = (char*)str;

	return num;
}

/**
 * Wysłanie wiadomości prosto do serwera.
 * 
 * @param message Wiadomość do wysłania.
 * @param flags Dodatkowe flagi dla funkcji msgsnd.
 * 
 * @return Wartość zwracana przez msgsnd.
 */
int send_server_message( const char *message, int flags )
{
	struct MSG_MESSAGE msg;

	msg.Type = 0x0AL;
	msg.Msqt = 0x0B;

	strcpy( msg.Message, message );
	return msgsnd( msqid, &msg, MSGSTR_SIZE, flags );
}

/**
 * Powitanie z serwerem.
 * 
 * @param void
 * @return 1 - błąd, 0 - brak błędu
 */
int server_handshake( void )
{
	struct MSG_MESSAGE msg;

	printf( COLOR_BLUE("Próba nawiązania połączenia z serwerem...") "\n" );

	// handshake, poczekaj...
	msg.Type = 0x0AL;
	msg.Msqt = 0x1B;
	if( msgsnd(msqid, &msg, MSGSTR_SIZE, 0) < 0 )
		return 1;

	// czekaj na wiadomość od serwera
	if( msgrcv(msqid, &msg, MSGSTR_SIZE, 0x0B, 0) < 0 )
	{
		printf( COLOR_RED("Nie można połączyć się z kolejką komunikatów.\n") );
		return 1;
	}

	// maksymalna ilość samochodów i interwał pomiędzy kolejnymi obliczeniami
	if( msg.Msqt == 0x20 )
	{
		maxcars = ((unsigned int)(unsigned char)msg.Message[0]) | ((unsigned int)(unsigned char)msg.Message[1] << 0x08) | 
			((unsigned int)(unsigned char)msg.Message[2] << 0x10) | ((unsigned int)(unsigned char)msg.Message[3] << 0x16);
		speeddown = ((unsigned int)(unsigned char)msg.Message[4]) | ((unsigned int)(unsigned char)msg.Message[5] << 0x08) | 
			((unsigned int)(unsigned char)msg.Message[6] << 0x10) | ((unsigned int)(unsigned char)msg.Message[7] << 0x16);
	}
	else
	{
		printf( COLOR_RED("Odebrano nieprawidłowy pakiet danych od serwera.\n") );
		return 1;
	}

	// nawiązano połączenie z serwerem
	printf( COLOR_GREEN("Nawiązano połączenie z serwerem") ".\n" );

	// sygnał powitalny
	send_server_message( COLOR_GREEN("Połączenie zostało ustanowione."), 0 );
	return 0;
}

/**
 * Funkcja uruchamiana przy wyłączaniu klienta.
 * 
 * @param void
 * @return void
 */
void server_bye( void )
{
	struct MSG_MESSAGE msg;

	// informacja o zakończonym połączeniu
	send_server_message( COLOR_GREEN("Połączenie zostało zakończone."), IPC_NOWAIT );

	// wyślij informacje na pożegnanie
	msg.Type = 0x0AL;
	msg.Msqt = 0xFB;
	msgsnd( msqid, &msg, MSGSTR_SIZE, IPC_NOWAIT );
}

/**
 * "Losowe" tworzenie samochodu.
 * 
 * @param void
 * @return void
 */
void random_create_car( void )
{
	static int interval = 0;
	int x = 0, randnum;

	if( interval++ < randinterval )
		return;
	interval = 0;

	// losowanie - czy samochód ma się pojawić czy nie
	randnum = rand() % 2;

	if( randnum )
		while( x < numcars )
		{
			if( cars[x].active )
			{
				++x;
				continue;
			}

			// losowanie kierunku przemieszczania się pojazdu
			randnum = rand() % 12;

			// uzupełnianie struktury nowymi danymi
			cars[x].active   = 1;
			cars[x].x        = direction[randnum].x;
			cars[x].y        = direction[randnum].y;
			cars[x].dir      = randnum;
			cars[x].blinker  = direction[randnum].blinker;
			cars[x].width    = direction[randnum].width;
			cars[x].height   = direction[randnum].height;
			cars[x].color    = rand() % 3;
			cars[x].xm       = direction[randnum].xm;
			cars[x].ym       = direction[randnum].ym;
			cars[x].next     = direction[randnum].next;
			cars[x].xmax     = direction[randnum].xmax;
			cars[x].ymax     = direction[randnum].ymax;
			cars[x].before   = direction[randnum].lastcar;
			cars[x].after    = -1;
			cars[x].interval = 0;

			// gdy samochód przed jest aktualnym samochodem, wywal go
			if( cars[x].before == x )
				cars[x].before = -1;

			// zapisz indeks samochodu przed dla samochodu po
			if( cars[x].before != -1 )
				cars[cars[x].before].after = x;

			// ostatni samochód jadący w danym kierunku
			direction[randnum].lastcar = x;

			break;
		}
}

/**
 * Ruch samochodu.
 * Funkcja na której działa wątek.
 * Tworzy procesy dla pojedynczych pieszych.
 * 
 * @param arg Argumenty przekazywane do funkcji wątku
 * @return NULL
 */
void *move_vechicle( void *arg )
{
	int x, w, before, stopx, stopy, repcond;
	struct CAR_INFO *car;
	int currcars = 0;

	while( can_run )
	{
		if( currcars != numcars )
		{
			if( currcars > numcars )
				while( currcars > numcars )
				{
					kill( cars[currcars-1].pid, SIGKILL );
					currcars--;
				}
			else if( currcars < numcars )
				while( currcars < numcars )
				{
					int childid = fork();

					// proces potomny
					if( childid == 0 )
					{
						int x = currcars;
						car = &cars[currcars];
						while( 1 )
						{
							if( !car->active )
							{
								usleep( speeddown );
								continue;
							}

							// samochód poza granicami widoczności
							if( car->x > 660 || car->y > 500 || car->x < -20 || car->y < -20 )
							{
								car->active = 0;

								// jeżeli za samochodem jest samochód, wywal informacje o samochodzie przed
								if( car->after != -1 )
									cars[car->after].before = -1;

								// ustaw ostatni samochód dla danego kierunku
								if( direction[car->dir].lastcar == x )
									direction[car->dir].lastcar = -1;

								continue;
							}

							// zmiana kierunku samochodów
							if( ((car->xm == 1 && car->xmax && car->x >= car->xmax) || (car->xm == -1 && car->xmax && car->x <= car->xmax)) ||
								((car->ym == 1 && car->ymax && car->y >= car->ymax) || (car->ym == -1 && car->ymax && car->y <= car->ymax)) )
							{
								int dir = car->next;

								if( direction[car->dir].lastcar == x )
									direction[car->dir].lastcar = -1;

								car->xm     = direction[dir].xm;
								car->ym     = direction[dir].ym;
								car->width  = direction[dir].width;
								car->height = direction[dir].height;
								car->dir    = dir;
							}

							// kierunek
							switch( car->dir )
							{
								case 0: case 1: case 2: w = 1; break;
								case 3: case 4: case 5: w = 2; break;
								case 6: case 7: case 8: w = 3; break;
								default: w = 4; break;
							}

							before  = car->before;
							stopx   = direction[car->dir].stopx;
							stopy   = direction[car->dir].stopy;
							repcond = before != -1 && car->dir == cars[before].dir;
							
							if( repcond && w == 1 && stopx > car->x && car->x + car->width + 2 >= cars[before].x - 5 )
								;
							// góra
							else if( repcond && w == 2 && stopy > car->y && car->y + car->height + 2 >= cars[before].y - 5 )
								;
							// dół
							else if( repcond && w == 3 && stopy < car->y && car->y - 5 <= cars[before].y + cars[before].height )
								;
							// prawa strona
							else if( repcond && w == 4 && stopx < car->x && car->x - 5 <= cars[before].x + cars[before].width )
								;
							// pion i poziom - blokada pojazdu na czerwonych światłach
							else if( stopx && car->x == stopx && light_state[car->dir] != 2 )
								;
							else if( stopy && car->y == stopy && light_state[car->dir] != 2 )
								;
							else
							{
								// po przekątnej z lewej strony
								if( car->dir == 0 && car->x > 270 )
								{
									car->x += 1;
									car->y -= 1;
								}
								// po przekątnej z góry
								else if( car->dir == 3 && car->y > 190 )
								{
									car->x += 1;
									car->y += 1;
								}
								// po przekątnej z dołu
								else if( car->dir == 6 && car->y < 290 )
								{
									car->x -= 1;
									car->y -= 1;
								}
								// po przekątnej z prawej strony
								else if( car->dir == 9 && car->x < 370 )
								{
									car->x -= 1;
									car->y += 1;
								}
								// poruszanie zwykłe (z góra/dół, dół/góra, lewo/prawo, prawo/lewo)
								else
								{
									car->x += 1 * car->xm;
									car->y += 1 * car->ym;
								}
							}
							usleep( speeddown );
						}

						exit;
					}
					else
						cars[currcars].pid = childid;
					currcars++;
				}
		}

		// tworzenie samochodu
		random_create_car();
		usleep( speeddown );
	}

	return NULL;
}

/**
 * Przywracanie ustawień początkowych dla samochodów.
 * 
 * @param void
 * @return void
 */
void reset_vechicles()
{
	int x;
	for( x = 0; x < maxcars; ++x )
		cars[x].active = 0;
	for( x = 0; x < 12; ++x )
		direction[x].lastcar = -1;
}

/**
 * Funkcja zarządzająca komendami.
 * 
 * @param void
 * @return 0
 */
int command_manager()
{
	char command[256],
		 cmdchr;
	int  cmdpos  = 0,
		 running = 1;

	printf( COLOR_MAGNETA("$ ") );
	while( running )
	{
		char *params;
		int iscmd = 0;

		while( (cmdchr = fgetc(stdin)) != '\n' )
			command[cmdpos++] = cmdchr;

		command[cmdpos] = '\0';

		// zakończenie działania klienta
		if( strcmp(command, "exit") == 0 )
		{
			struct MSG_MESSAGE msg;

			running = 0;

			msg.Type = 0x0BL;
			msg.Msqt = 0xF0;

			msgsnd( msqid, &msg, MSGSTR_SIZE, IPC_NOWAIT );

			iscmd = 1;
		}
		// reset pozycji samochodów
		else if( strcmp(command, "reset") == 0 )
		{
			struct MSG_MESSAGE msg;

			send_server_message( COLOR_YELLOW("Powrót samochodów do stanu początkowego."), 0 );
			printf( COLOR_YELLOW("Powrót samochodów do stanu początkowego.") "\n" );

			msg.Type = 0x0BL;
			msg.Msqt = 0xE2;

			msgsnd( msqid, &msg, MSGSTR_SIZE, IPC_NOWAIT );

			iscmd = 1;
		}
		// maksymalna ilość samochodów
		else if( strcmp(command, "max") == 0 )
			printf( COLOR_YELLOW("Limit wyświetlanych samochodów to: %d.") "\n", maxcars ), iscmd = 1;
		// aktualna ilość samochodów
		else if( strcmp(command, "current") == 0 )
			printf( COLOR_YELLOW("Obecna ilość wyświetlanych samochodów to: %d.") "\n", numcars ), iscmd = 1;
		// pomoc
		else if( strcmp(command, "help") == 0 )
		{
			printf( "Lista dostępnych komend:\n" );
			printf( "  - " COLOR_YELLOW("reset") "    : powrót samochodów do stanu początkowego;\n" );
			printf( "  - " COLOR_YELLOW("max") "      : największa możliwa ilość ustawionych samochodów;\n" );
			printf( "  - " COLOR_YELLOW("current") "  : aktualna ilość ustawionych samochodów;\n" );
			printf( "  - " COLOR_YELLOW("ival ") COLOR_BLUE("<?>") " : przerwa pomiędzy kolejnymi losowaniami samochodów w 1/60 sek;\n" );
			printf( "  - " COLOR_YELLOW("help") "     : pomoc, wyświetla listę komend;\n" );
			printf( "  - " COLOR_YELLOW("set ") COLOR_BLUE("<x>") "  : zmiana ilości ustawionych samochodów, po komendzie set "
													  "należy podać nową ilość wyświetlanych samochodów;\n" );
			printf( "  - " COLOR_YELLOW("exit") "     : wyjście z programu;\n" );
			printf( "Zmienne oznaczone jako x, y lub z należy zamienić odpowiednimi znakami lub cyframi. W przypadku gdy w miejscu zmiennej "
				"znajduje się znak ? oznacza to, że argument ten nie jest obowiązkowy.\n" );
			iscmd = 1;
		}

		// szukaj parametrów
		params = command;
		while( *params != ' ' && *params != '\0' )
			params++;
		if( *params == ' ' )
			*params++ = '\0';

		// zmiana ilości wyświetlanych pojazdów
		if( strcmp(command, "set") == 0 && *params != '\0' )
		{
			struct MSG_MESSAGE msg;
			unsigned int num = string_to_uint( params, &params );

			// kontroluj ilość wyświetlanych pojazdów
			if( num > maxcars )
			{
				printf( COLOR_MAGNETA("Ilość zamochodów nie może przekraczać: %d.") "\n", maxcars );
				num = maxcars;
			}

			// inforamcja o zmianie ilości wyświetlanych pojazdów
			printf( COLOR_CYAN("Zmieniono ilość wyświetlanych pojazdów na: %d.") "\n", num );

			// wiadomość do klienta
			msg.Type = 0x0BL;
			msg.Msqt = 0xE1;

			// zamień ilość wyświetlanych samochodów
			numcars = num;

			msg.Message[0] = numcars & 0xFF;
			msg.Message[1] = (numcars << 0x08) & 0xFF;

			msgsnd( msqid, &msg, MSGSTR_SIZE, IPC_NOWAIT );

			iscmd = 1;
		}
		// zmiana przerwy pomiędzy losowaniami pojazdów
		else if( strcmp(command, "ival") == 0 )
		{
			if( *params == '\0' )
				printf( COLOR_YELLOW("Aktualna przerwa pomiędzy losowaniami samochodów: %d.\n"), randinterval );
			else
			{
				struct MSG_MESSAGE msg;
				unsigned int num = string_to_uint( params, &params );

				// inforamcja o zmianie ilości wyświetlanych pojazdów
				printf( COLOR_CYAN("Zmieniono przerwę pomiędzy losowaniami pojazdów na: %d.") "\n", num );

				// wiadomość do klienta
				msg.Type = 0x0BL;
				msg.Msqt = 0xE3;

				// zamień ilość wyświetlanych samochodów
				randinterval = num;

				msg.Message[0] = randinterval & 0xFF;
				msg.Message[1] = (randinterval >> 0x08) & 0xFF;
				msg.Message[2] = (randinterval >> 0x10) & 0xFF;
				msg.Message[3] = (randinterval >> 0x18) & 0xFF;

				msgsnd( msqid, &msg, MSGSTR_SIZE, IPC_NOWAIT );
			}

			iscmd = 1;
		}

		if( !iscmd )
			printf( COLOR_RED("Nieprawidłowa komenda, wpisz help aby wyświetlić listę komend.") "\n" );

		if( running )
			printf( COLOR_MAGNETA("$ ") );
		cmdpos = 0;
	}
	return 0;
}

/**
 * Kończenie działania procesów i wątku działających w programie.
 * 
 * @param void
 * @return void
 */
void kill_all_entities( void )
{
	int x;
	for( x = 0; x < numcars; ++x )
		kill( cars[x].pid, SIGKILL );
	pthread_kill( movethr, SIGKILL );
}

/**
 * Funkcja wywoływana podczas otrzymania sygnału zakończenia działania programu.
 * Np. CTRL+C w konsoli.
 * 
 * @param dummy
 * @return void
 */
void client_out( int dummy )
{
	if( getpid() == mnpid )
	{
		printf( CLEAR_LINE );
		printf( COLOR_RED("Wymuszone zakończenie działania programu.") "\n" );
		kill_all_entities();
	}
}

/**
 * Funkcja główna programu.
 * 
 * @param argc Ilość argumentów przekazywanych do programu.
 * @param argv Argumenty przekazane do programu.
 * 
 * @return Kod błędu lub 0.
 */
int main( int argc, char **argv )
{
	int running = 1,
		sendbye = 1,
		cmdproc;
	struct MSG_MESSAGE msg;

	mnpid = getpid();

	// przechwytywanie sygnałów zakończenia aplikacji
	signal( SIGINT, client_out );

	// kolejka komunikatów
	if( (msqid = msgget(MESSAGE_KEY, 0)) < 0 )
	{
		printf( COLOR_RED("Nie można połączyć się z kolejką komunikatów.") "\n" );
		return 1;
	}

	// informacja o programie
	printf( "=================================================\n"
			"@ " COLOR_GREEN("Symulator skrzyżowania") "\n"
			"@ " COLOR_GREEN("Klient samochodów") "\n"
			"=================================================\n"
			"@ " COLOR_BLUE("Kamil Biały") "\n"
			"=================================================\n" );

	// nawiązywanie połączenia z serwerem
	if( server_handshake() != 0 )
	{
		printf( COLOR_RED("Nie można nawiązać połączenia z serwerem.") "\n" );
		return 1;
	}

	// pobierz identyfikator pamięci współdzielonej
	if( (shmid = shmget(SHARED_KEY, 16 * sizeof *light_state, 0)) < 0 )
	{
		printf( COLOR_RED("Pamięć współdzielona nie istnieje.") "\n" );
		return 1;
	}
	if( (light_state = shmat(shmid, NULL, 0)) == (int*)-1 )
	{
		printf( COLOR_RED("Błąd podczas pobierania adresu pamięci współdzielonej.") "\n" );
		return 1;
	}

	// utwórz segment pamięci współdzielonej dla samochodów
	if( (shmidc = shmget(CSHARED_KEY, maxcars * sizeof *cars, 0)) < 0 )
	{
		printf( COLOR_RED("Pamięć współdzielona dla samochodów nie istnieje!") "\n" );
		return 1;
	}
	if( (cars = shmat(shmidc, NULL, 0)) == (struct CAR_INFO*)-1 )
	{
		printf( COLOR_RED("Błąd podczas pobierania adresu pamięci współdzielonej.") "\n" );
		return 1;
	}

	// uruchom menedżer przechwytywanych komend
	cmdproc = fork();
	if( cmdproc == 0 )
		return command_manager();

	// utwórz wątek dla samochodów
	if( pthread_create(&movethr, NULL, &move_vechicle, NULL) != 0 )
	{
		send_server_message( COLOR_RED("Wystąpił błąd podczas tworzenia wątku."), IPC_NOWAIT );
		printf( COLOR_RED("Wystąpił błąd podczas tworzenia wątku.") "\n" );
	}

	// pętla główna dla wiadomości
	while( running )
	{
		// pobierz wiadomość z kolejki
		if( msgrcv(msqid, &msg, MSGSTR_SIZE, 0x0B, 0) < 0 )
		{
			printf( CLEAR_LINE );
			printf( COLOR_RED("Wystąpił bład podczas pobierania wiadomości.") "\n" );
			running = 0;
			sendbye = 1;
		}

		switch( msg.Msqt )
		{
		// zmiana ilości wyświetlanych samochodów
		case 0xE1:
		{
			char buff[128];
			int x;
			unsigned int num = (unsigned int)(unsigned char)msg.Message[0] | ((unsigned int)(unsigned char)msg.Message[1] << 8);
			numcars = num;

			// wyślij informacje o zmianie ilości wyświetlanych samochodów
			sprintf( buff, COLOR_YELLOW("Zmieniono ilość wyświetlanych samochodów na: %d"), numcars );
			send_server_message( buff, IPC_NOWAIT );

			// ustaw tyle samochodów ile potrzeba
			for( x = numcars; x < maxcars; ++x )
			{
				// zmień wartości nieużywanych już samochodów
				cars[x].active = 0;
				if( direction[cars[x].dir].lastcar == x )
				{
					int y = 0;
					direction[cars[x].dir].lastcar = -1;

					// przypisz nowy indeks ostatniego samochodu
					for( y = 0; y < maxcars; ++y )
						if( cars[y].active && cars[y].dir == cars[x].dir )
							if( cars[y].after == x || cars[y].after == -1 )
								direction[cars[x].dir].lastcar = y;
				}
				// podmień indeksy samochodów przed i za dla samochodów przed i za aktualnym samochodem
				if( cars[x].after != -1 )
					cars[cars[x].after].before = cars[x].before;
				if( cars[x].before != -1 )
					cars[cars[x].before].after = cars[x].after;

				cars[x].after  = -1;
				cars[x].before = -1;
			}
		}
		break;
		// powrót do samochodów do początkowej pozycji
		case 0xE2:
			reset_vechicles();
		break;
		// zmiana przerwy pomiędzy losowaniami kolejnych samochodów
		case 0xE3:
		{
			unsigned int num = ((unsigned int)(unsigned char)msg.Message[0]) | ((unsigned int)(unsigned char)msg.Message[1] << 0x08) | 
				((unsigned int)(unsigned char)msg.Message[2] << 0x10) | ((unsigned int)(unsigned char)msg.Message[3] << 0x16);
			randinterval = num;
		}
		break;
		// sygnał zamknięcia klienta
		case 0xF0:
			printf( CLEAR_LINE );
			printf( COLOR_CYAN("Przechwycono sygnał zamknięcia.") "\n" );
			running = 0;
			sendbye = 1;
		break;
		// sygnał zamknięcia serwera
		case 0xFF:
			printf( CLEAR_LINE );
			printf( COLOR_CYAN("Przechwycono sygnał zamknięcia serwera.") "\n" );
			running = 0;
			sendbye = 0;
		break;
		}
	}

	can_run = 0;

	// informacja o zakończeniu połączenia z serwerem
	if( sendbye )
		server_bye();

	kill_all_entities();

	return 0;
}
