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
#define TSHARED_KEY  056306     // identyfikator pamięci współdzielonej dla tramwaju
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

// spowolnienie wykonywania programu
unsigned int speeddown = 1;

// identyfikatory kolejek i pamięci współdzielonej
int msqid;
int shmid;
int shmidt;
int mnpid;

// przerwa pomiędzy losowaniami kolejnych tramwaju
int randinterval = 35;

/**
 * Struktura informacji o tramwaju.
 * Większość informacji kopiowana jest ze struktury DIR_INFO.
 */
struct TRAM_INFO
{
	int x, y;           // x, y
	int width, height;  // szerokość, wysokość
	int active;         // czy pieszy jest aktywny
	int stopy;          // punkt stopu
};

// piesi
struct TRAM_INFO *tram;
int              *light_state;

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
	msg.Msqt = 0x0D;

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
	msg.Msqt = 0x1D;
	if( msgsnd(msqid, &msg, MSGSTR_SIZE, 0) < 0 )
		return 1;

	// czekaj na wiadomość od serwera
	if( msgrcv(msqid, &msg, MSGSTR_SIZE, 0x0D, 0) < 0 )
	{
		printf( COLOR_RED("Nie można połączyć się z kolejką komunikatów.\n") );
		return 1;
	}

	// maksymalna ilość pieszych i interwał pomiędzy kolejnymi obliczeniami
	if( msg.Msqt == 0x20 )
		speeddown = ((unsigned int)(unsigned char)msg.Message[4]) | ((unsigned int)(unsigned char)msg.Message[5] << 0x08) | 
			((unsigned int)(unsigned char)msg.Message[6] << 0x10) | ((unsigned int)(unsigned char)msg.Message[7] << 0x16);
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
	msg.Msqt = 0xFD;
	msgsnd( msqid, &msg, MSGSTR_SIZE, IPC_NOWAIT );
}

/**
 * Tworzenie tramwaju.
 * 
 * @param void
 * @return void
 */
void create_tram( void )
{
	static int interval = 0;
	int x = 0, randnum;

	if( interval++ < randinterval )
		return;
	interval = 0;

	// losowanie - czy tramwaj ma się pojawić czy nie
	randnum = rand() % 2;

	if( randnum )
	{
		if( tram->active )
			return;
		
		tram->y = 480;
		tram->active = 1;
	}
}

/**
 * Ruch tramwaju.
 * 
 * @param arg Argumenty przekazywane do funkcji wątku
 * @return NULL
 */
void *move_tram( void *arg )
{
	while( can_run )
	{
		// poruszaj tramwajem
		if( tram->active )
		{
			// wyłącz tramwaj z użytku - zrobił swoje
			if( tram->y < -tram->height)
				tram->active = 0;
				
			if( tram->y == tram->stopy && light_state[13] != 2 )
				;
			else
				tram->y--;
		}

		// tworzenie pieszego
		create_tram();
		usleep( speeddown );
	}
	return NULL;
}

/**
 * Przywracanie ustawień początkowych dla tramwaju.
 * 
 * @param void
 * @return void
 */
void reset_tram()
{
	tram->active = 0;
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

			msg.Type = 0x0DL;
			msg.Msqt = 0xF0;

			msgsnd( msqid, &msg, MSGSTR_SIZE, IPC_NOWAIT );

			iscmd = 1;
		}
		// reset pozycji pieszych
		else if( strcmp(command, "reset") == 0 )
		{
			struct MSG_MESSAGE msg;

			send_server_message( COLOR_YELLOW("Powrót tramwaju do stanu początkowego."), 0 );
			printf( COLOR_YELLOW("Powrót tramwaju do stanu początkowego.") "\n" );

			msg.Type = 0x0DL;
			msg.Msqt = 0xE2;

			msgsnd( msqid, &msg, MSGSTR_SIZE, IPC_NOWAIT );

			iscmd = 1;
		}
		// pomoc
		else if( strcmp(command, "help") == 0 )
		{
			printf( "Lista dostępnych komend:\n" );
			printf( "  - " COLOR_YELLOW("reset") "    : powrót pieszych do stanu początkowego;\n" );
			printf( "  - " COLOR_YELLOW("ival ") COLOR_BLUE("<?>") " : przerwa pomiędzy kolejnymi losowaniami pieszych w 1/60 sek;\n" );
			printf( "  - " COLOR_YELLOW("help") "     : pomoc, wyświetla listę komend;\n" );
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

		// zmiana przerwy pomiędzy losowaniami pieszych
		if( strcmp(command, "ival") == 0 )
		{
			if( *params == '\0' )
				printf( COLOR_YELLOW("Aktualna przerwa pomiędzy losowaniami pieszych: %d.\n"), randinterval );
			else
			{
				struct MSG_MESSAGE msg;
				unsigned int num = string_to_uint( params, &params );

				// inforamcja o zmianie ilości wyświetlanych pieszych
				printf( COLOR_CYAN("Zmieniono przerwę pomiędzy losowaniami pieszych na: %d.") "\n", num );

				// wiadomość do klienta
				msg.Type = 0x0DL;
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
		printf( COLOR_RED("Wymuszone zakończenie działania programu.\n") );
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
		printf( COLOR_RED("Nie można połączyć się z kolejką komunikatów.\n") );
		return 1;
	}

	// informacja o programie
	printf( "=================================================\n"
			"@ " COLOR_GREEN("Symulator skrzyżowania") "\n"
			"@ " COLOR_GREEN("Klient tramwaju") "\n"
			"=================================================\n"
			"@ " COLOR_BLUE("Kamil Biały") "\n"
			"=================================================\n" );

	// nawiązywanie połączenia z serwerem
	if( server_handshake() != 0 )
	{
		printf( COLOR_RED("Nie można nawiązać połączenia z serwerem.\n") );
		return 1;
	}

	// pobierz identyfikator pamięci współdzielonej
	if( (shmid = shmget(SHARED_KEY, 16 * sizeof *light_state, 0)) < 0 )
	{
		printf( COLOR_RED("Pamięć współdzielona nie istnieje.\n") );
		return 1;
	}
	if( (light_state = shmat(shmid, NULL, 0)) == (int*)-1 )
	{
		printf( COLOR_RED("Błąd podczas pobierania adresu pamięci współdzielonej.\n") );
		return 1;
	}

	// utwórz segment pamięci współdzielonej dla pieszych
	if( (shmidt = shmget(TSHARED_KEY, sizeof *tram, 0)) < 0 )
	{
		printf( COLOR_RED("Pamięć współdzielona dla pieszych nie istnieje!\n") );
		return 1;
	}
	if( (tram = shmat(shmidt, NULL, 0)) == (struct TRAM_INFO*)-1 )
	{
		printf( COLOR_RED("Błąd podczas pobierania adresu pamięci współdzielonej.\n") );
		return 1;
	}

	// inicjalizacja tramwaju
	tram->x      = 214;
	tram->y      = 480;
	tram->width  = 10;
	tram->height = 30;
	tram->active = 0;
	tram->stopy  = 333;

	// uruchom menedżer przechwytywanych komend
	cmdproc = fork();
	if( cmdproc == 0 )
		return command_manager();

	// utwórz wątek dla tramwaju
	if( pthread_create(&movethr, NULL, &move_tram, NULL) != 0 )
	{
		send_server_message( COLOR_RED("Wystąpił błąd podczas tworzenia wątku."), IPC_NOWAIT );
		printf( COLOR_RED("Wystąpił błąd podczas tworzenia wątku.") "\n" );
	}

	// pętla główna dla wiadomości
	while( running )
	{
		// pobierz wiadomość z kolejki
		if( msgrcv(msqid, &msg, MSGSTR_SIZE, 0x0D, 0) < 0 )
		{
			printf( CLEAR_LINE );
			printf( COLOR_RED("Wystąpił bład podczas pobierania wiadomości.\n") );
			running = 0;
			sendbye = 1;
		}

		switch( msg.Msqt )
		{
		// powrót do pieszych do początkowej pozycji
		case 0xE2:
			reset_tram();
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
			printf( COLOR_CYAN("Przechwycono sygnał zamknięcia.\n") );
			running = 0;
			sendbye = 1;
		break;
		// sygnał zamknięcia serwera
		case 0xFF:
			printf( CLEAR_LINE );
			printf( COLOR_CYAN("Przechwycono sygnał zamknięcia serwera.\n") );
			running = 0;
			sendbye = 0;
		break;
		}
	}

	can_run = 0;

	// informacja o zakończeniu połączenia z serwerem
	if( sendbye )
		server_bye();

	return 0;
}
