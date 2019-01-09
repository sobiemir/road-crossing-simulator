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
#define PSHARED_KEY  030633     // identyfikator pamięci współdzielonej dla pieszych
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

// maksymalna ilość pieszych - pobierana z serwera
unsigned int maxpeds = 0;

// spowolnienie wykonywania programu
unsigned int speeddown = 1;

// identyfikatory kolejek i pamięci współdzielonej
int msqid;
int shmid;
int shmidp;
int mnpid;

// przerwa pomiędzy losowaniami kolejnych pieszych
int randinterval = 35;

// ilość pieszych
unsigned int numpeds = 0;

/**
 * Struktura informacji o pieszych.
 * Większość informacji kopiowana jest ze struktury DIR_INFO.
 * Każdy pieszy posiada interwał dla migacza.
 */
struct PED_INFO
{
	int x, y, dir;          // x, y, kierunek
	int color, xm, ym;      // kolor, linia skrętu X i Y
	int active;             // czy pieszy jest aktywny
	int before, after;      // indeks pieszego przed i po
	pid_t pid;
};

// piesi
struct PED_INFO *peds;
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
	int x, y, xm, ym, stopx, stopy, lastped;
}
direction[4] =
{
	// idzie na dół
	{ 237, -15, 0, 1, 0, 145, -1 },
	
	// idzie do góry
	{ 397, 495, 0, -1, 0, 335, -1 },
	
	// idzie w lewo
	{ -15, 318, 1, 0, 200, 0, -1 },
	
	// idzie w prawo
	{ 655, 156, -1, 0, 415, 0, -1 },
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
	msg.Msqt = 0x0C;

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
	msg.Msqt = 0x1C;
	if( msgsnd(msqid, &msg, MSGSTR_SIZE, 0) < 0 )
		return 1;

	// czekaj na wiadomość od serwera
	if( msgrcv(msqid, &msg, MSGSTR_SIZE, 0x0C, 0) < 0 )
	{
		printf( COLOR_RED("Nie można połączyć się z kolejką komunikatów.\n") );
		return 1;
	}

	// maksymalna ilość pieszych i interwał pomiędzy kolejnymi obliczeniami
	if( msg.Msqt == 0x20 )
	{
		maxpeds = ((unsigned int)(unsigned char)msg.Message[0]) | ((unsigned int)(unsigned char)msg.Message[1] << 0x08) | 
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
	msg.Msqt = 0xFC;
	msgsnd( msqid, &msg, MSGSTR_SIZE, IPC_NOWAIT );
}

/**
 * "Losowe" tworzenie pieszego.
 * 
 * @param void
 * @return void
 */
void random_create_ped( void )
{
	static int interval = 0;
	int x = 0, randnum;

	if( interval++ < randinterval )
		return;
	interval = 0;

	// losowanie - czy pieszy ma się pojawić czy nie
	randnum = rand() % 2;

	if( randnum )
		while( x < numpeds )
		{
			if( peds[x].active )
			{
				++x;
				continue;
			}

			// losowanie kierunku przemieszczania się pieszych
			randnum = rand() % 4;

			// uzupełnianie struktury
			peds[x].active   = 1;
			peds[x].x        = direction[randnum].x;
			peds[x].y        = direction[randnum].y;
			peds[x].dir      = randnum;
			peds[x].color    = rand() % 3;
			peds[x].xm       = direction[randnum].xm;
			peds[x].ym       = direction[randnum].ym;
			peds[x].before   = direction[randnum].lastped;
			peds[x].after    = -1;
				
			// gdy pieszy przed jest aktualnym pieszym, wywal go
			if( peds[x].before == x )
				peds[x].before = -1;
			
			// zapisz indeks pieszego przed dla pieszego po
			if( peds[x].before != -1 )
				peds[peds[x].before].after = x;
			
			// ostatni pieszy jadący w danym kierunku
			direction[randnum].lastped = x;

			break;
		}
}

/**
 * Ruch pieszego.
 * Funkcja na której działa wątek.
 * Tworzy procesy dla pojedynczych pieszych.
 * 
 * @param arg Argumenty przekazywane do funkcji wątku
 * @return NULL
 */
void *move_pedestrian( void *arg )
{
	int before, stopx, stopy, x;
	struct PED_INFO *ped;
	int currpeds = 0;

	while( can_run )
	{
		if( currpeds != numpeds )
		{
			if( currpeds > numpeds )
				while( currpeds > numpeds )
				{
					kill( peds[currpeds-1].pid, SIGKILL );
					currpeds--;
				}
			else if( currpeds < numpeds )
				while( currpeds < numpeds )
				{
					int childid = fork();

					// proces potomny
					if( childid == 0 )
					{
						int x = currpeds;
						ped = &peds[x];
						while( 1 )
						{
							// pieszy nie jest aktywny
							if( !ped->active )
							{
								usleep( speeddown );
								continue;
							}
							
							// wyjście poza granice ekranu
							if( ped->x > 660 || ped->y > 500 || ped->x < -20 || ped->y < -20 )
							{
								ped->active = 0;

								// jeżeli za pieszym jest pieszy, wywal informacje o pieszym przed
								if( ped->after != -1 )
									peds[ped->after].before = -1;
								
								// gdy jest jedyny na pasie, usuń go z tablicy
								if( direction[ped->dir].lastped == x )
									direction[ped->dir].lastped = -1;
								
								continue;
							}
							
							stopx  = direction[ped->dir].stopx;
							stopy  = direction[ped->dir].stopy;
							before = ped->before;
							
							// lewo
							if( before != -1 && ped->dir == 2 && stopx > ped->x && ped->x + 5 >= peds[before].x - 2 )
								;
							// prawo
							else if( before != -1 && ped->dir == 3 && stopx < ped->x && ped->x - 2 <= peds[before].x + 5 )
								;
							// góra
							else if( before != -1 && ped->dir == 0 && stopy > ped->y && ped->y + 5 >= peds[before].y - 2 )
								;
							// dół
							else if( before != -1 && ped->dir == 1 && stopy < ped->y && ped->y - 2 <= peds[before].y + 5 )
								;
							// światła
							else if( stopx && ped->x == stopx && light_state[ped->dir + 12] != 2 )
								;
							else if( stopy && ped->y == stopy && light_state[ped->dir + 12] != 2 )
								;
							// ruch
							else
							{
								ped->x += 1 * ped->xm;
								ped->y += 1 * ped->ym;
							}
							usleep( speeddown );
						}

						exit;
					}
					else
						peds[currpeds].pid = childid;
					currpeds++;
				}
		}

		// tworzenie pieszego
		random_create_ped();
		usleep( speeddown );
	}
	return NULL;
}

/**
 * Przywracanie ustawień początkowych dla pieszych.
 * 
 * @param void
 * @return void
 */
void reset_pedestrians( void )
{
	int x;
	for( x = 0; x < maxpeds; ++x )
		peds[x].active = 0;
	for( x = 0; x < 4; ++x )
		direction[x].lastped = -1;
}

/**
 * Funkcja zarządzająca komendami.
 * 
 * @param void
 * @return 0
 */
int command_manager( void )
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

			msg.Type = 0x0CL;
			msg.Msqt = 0xF0;

			msgsnd( msqid, &msg, MSGSTR_SIZE, IPC_NOWAIT );

			iscmd = 1;
		}
		// reset pozycji pieszych
		else if( strcmp(command, "reset") == 0 )
		{
			struct MSG_MESSAGE msg;

			send_server_message( COLOR_YELLOW("Powrót pieszych do stanu początkowego."), 0 );
			printf( COLOR_YELLOW("Powrót pieszych do stanu początkowego.") "\n" );

			msg.Type = 0x0CL;
			msg.Msqt = 0xE2;

			msgsnd( msqid, &msg, MSGSTR_SIZE, IPC_NOWAIT );

			iscmd = 1;
		}
		// maksymalna ilość pieszych
		else if( strcmp(command, "max") == 0 )
			printf( COLOR_YELLOW("Limit wyświetlanych pieszych to: %d.") "\n", maxpeds ), iscmd = 1;
		// aktualna ilość pieszych
		else if( strcmp(command, "current") == 0 )
			printf( COLOR_YELLOW("Obecna ilość wyświetlanych pieszych to: %d.") "\n", numpeds ), iscmd = 1;
		// pomoc
		else if( strcmp(command, "help") == 0 )
		{
			printf( "Lista dostępnych komend:\n" );
			printf( "  - " COLOR_YELLOW("reset") "    : powrót pieszych do stanu początkowego;\n" );
			printf( "  - " COLOR_YELLOW("max") "      : największa możliwa ilość ustawionych pieszych;\n" );
			printf( "  - " COLOR_YELLOW("current") "  : aktualna ilość ustawionych pieszych;\n" );
			printf( "  - " COLOR_YELLOW("ival ") COLOR_BLUE("<?>") " : przerwa pomiędzy kolejnymi losowaniami pieszych w 1/60 sek;\n" );
			printf( "  - " COLOR_YELLOW("help") "     : pomoc, wyświetla listę komend;\n" );
			printf( "  - " COLOR_YELLOW("set ") COLOR_BLUE("<x>") "  : zmiana ilości ustawionych pieszych, po komendzie set "
													  "należy podać nową ilość wyświetlanych pieszych;\n" );
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

		// zmiana ilości wyświetlanych pieszych
		if( strcmp(command, "set") == 0 && *params != '\0' )
		{
			struct MSG_MESSAGE msg;
			unsigned int num = string_to_uint( params, &params );

			// kontroluj ilość wyświetlanych pieszych
			if( num > maxpeds )
			{
				printf( COLOR_MAGNETA("Ilość zamochodów nie może przekraczać: %d.") "\n", maxpeds );
				num = maxpeds;
			}

			// inforamcja o zmianie ilości wyświetlanych pieszych
			printf( COLOR_CYAN("Zmieniono ilość wyświetlanych pieszych na: %d.") "\n", num );

			// wiadomość do klienta
			msg.Type = 0x0CL;
			msg.Msqt = 0xE1;

			// zamień ilość wyświetlanych pieszych
			numpeds = num;

			msg.Message[0] = numpeds & 0xFF;
			msg.Message[1] = (numpeds << 0x08) & 0xFF;

			msgsnd( msqid, &msg, MSGSTR_SIZE, IPC_NOWAIT );

			iscmd = 1;
		}
		// zmiana przerwy pomiędzy losowaniami pieszych
		else if( strcmp(command, "ival") == 0 )
		{
			if( *params == '\0' )
				printf( COLOR_YELLOW("Aktualna przerwa pomiędzy losowaniami pieszych: %d.") "\n", randinterval );
			else
			{
				struct MSG_MESSAGE msg;
				unsigned int num = string_to_uint( params, &params );

				// inforamcja o zmianie ilości wyświetlanych pieszych
				printf( COLOR_CYAN("Zmieniono przerwę pomiędzy losowaniami pieszych na: %d.") "\n", num );

				// wiadomość do klienta
				msg.Type = 0x0CL;
				msg.Msqt = 0xE3;

				// zamień ilość wyświetlanych pieszych
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
	for( x = 0; x < numpeds; ++x )
		kill( peds[x].pid, SIGKILL );
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
			"@ " COLOR_GREEN("Klient pieszych") "\n"
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

	// utwórz segment pamięci współdzielonej dla pieszych
	if( (shmidp = shmget(PSHARED_KEY, maxpeds * sizeof *peds, 0)) < 0 )
	{
		printf( COLOR_RED("Pamięć współdzielona dla pieszych nie istnieje!") "\n" );
		return 1;
	}
	if( (peds = shmat(shmidp, NULL, 0)) == (struct PED_INFO*)-1 )
	{
		printf( COLOR_RED("Błąd podczas pobierania adresu pamięci współdzielonej.") "\n" );
		return 1;
	}

	// uruchom menedżer przechwytywanych komend
	cmdproc = fork();
	if( cmdproc == 0 )
		return command_manager();

	// utwórz wątek dla pieszych
	if( pthread_create(&movethr, NULL, &move_pedestrian, NULL) != 0 )
	{
		send_server_message( COLOR_RED("Wystąpił błąd podczas tworzenia wątku."), IPC_NOWAIT );
		printf( COLOR_RED("Wystąpił błąd podczas tworzenia wątku.") "\n" );
	}

	// pętla główna dla wiadomości
	while( running )
	{
		// pobierz wiadomość z kolejki
		if( msgrcv(msqid, &msg, MSGSTR_SIZE, 0x0C, 0) < 0 )
		{
			printf( CLEAR_LINE );
			printf( COLOR_RED("Wystąpił bład podczas pobierania wiadomości.") "\n" );
			running = 0;
			sendbye = 1;
		}

		switch( msg.Msqt )
		{
		// zmiana ilości wyświetlanych pieszych
		case 0xE1:
		{
			char buff[128];
			int x;
			unsigned int num = (unsigned int)(unsigned char)msg.Message[0] | ((unsigned int)(unsigned char)msg.Message[1] << 8);
			numpeds = num;

			// wyślij informacje o zmianie ilości wyświetlanych pieszych
			sprintf( buff, COLOR_YELLOW("Zmieniono ilość wyświetlanych pieszych na: %d"), numpeds );
			send_server_message( buff, IPC_NOWAIT );

			// ustaw tyle pieszych ile potrzeba
			for( x = numpeds; x < maxpeds; ++x )
			{
				// zmień wartości nieużywanych już pieszych
				peds[x].active = 0;
				if( direction[peds[x].dir].lastped == x )
				{
					int y = 0;
					direction[peds[x].dir].lastped = -1;

					// przypisz nowy indeks ostatniego pieszego
					for( y = 0; y < maxpeds; ++y )
						if( peds[y].active && peds[y].dir == peds[x].dir )
							if( peds[y].after == x || peds[y].after == -1 )
								direction[peds[x].dir].lastped = y;
				}
				// podmień indeksy pieszych przed i za dla pieszych przed i za aktualnym pieszym
				if( peds[x].after != -1 )
					peds[peds[x].after].before = peds[x].before;
				if( peds[x].before != -1 )
					peds[peds[x].before].after = peds[x].after;

				peds[x].after  = -1;
				peds[x].before = -1;
			}
		}
		break;
		// powrót do pieszych do początkowej pozycji
		case 0xE2:
			reset_pedestrians();
		break;
		// zmiana przerwy pomiędzy losowaniami kolejnych pieszych
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
