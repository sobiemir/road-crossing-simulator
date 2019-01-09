#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xos.h>
#include <X11/Xutil.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/shm.h>

#define SPEED_DOWN   20000      // spowolnienie wyświetlania elementów
#define MESSAGE_KEY  020626     // identyfikator kolejki komunikatów
#define SHARED_KEY   060226     // identyfikator pamięci współdzielonej
#define CSHARED_KEY  022606     // identyfikator pamięci współdzielonej dla samochodów
#define PSHARED_KEY  030633     // identyfikator pamięci współdzielonej dla pieszych
#define TSHARED_KEY  056306     // identyfikator pamięci współdzielonej dla tramwaju
#define MESSAGE_SIZE 256        // rozmiar bufora wiadomości w kolejce komunikatów

#define MSGSTR_SIZE (256 * sizeof(char) + sizeof(int))
#define MAX_CARS     60
#define MAX_PEDS     80

// kolory w konsoli...
#define CLEAR_LINE         "\033[2K\r"
#define COLOR_GREEN(_T_)   "\033[0;32;32m" _T_ "\033[0m"
#define COLOR_CYAN(_T_)    "\033[0;36m"    _T_ "\033[0m"
#define COLOR_RED(_T_)     "\033[0;32;31m" _T_ "\033[0m"
#define COLOR_YELLOW(_T_)  "\033[0;33m"    _T_ "\033[0m"
#define COLOR_MAGNETA(_T_) "\033[0;35m"    _T_ "\033[0m"
#define COLOR_BLUE(_T_)    "\033[0;32;34m" _T_ "\033[0m"

#define LIGHT_RUNS        6     // ilość przebiegów (cross_matrix)
#define LIGHT_RUN_TIME    300   // czas przebiegu - świecenie zielonego światła
#define LIGHT_YELLOW_TIME 100   // czas świecenia żółtego światła

/**
 * Indeksy light_state w porządku zdefiniowanym w cross_matrix.
 * Pozwala na konwersję indeksu z tablicy cross_matrix na indeks w light_state.
 */
int light_matrix[16] =
{
	4, 5,  3,  10,
	0, 12, 13, 11,
	2, 14, 15, 9,
	1, 6,  8,  7
};

/**
 * Przebiegi świateł.
 * Światła zmieniają się co LIGHT_RUN_TIME + LIGHT_YELLOW_TIME.
 * Każde pole oznacza światło, zawiera wartości 1 lub 0 (świeci się lub nie).
 * 
 * Oznaczenia:
 * g - światła samochodu
 * t,l,c,r,b - góra, lewo, środek, prawo, dół
 * p - światła dla pieszych
 * 
 *  -----------------------
 * | gtl | gtc | gtr | grt |
 * | --------------------- |
 * | glt | pl  | pr  | grc |
 * | --------------------- |
 * | glc | pt  | pb  | grb |
 * | --------------------- |
 * | glb | gbl | gbc | gbr |
 *  -----------------------
 */
int cross_matrix[LIGHT_RUNS][16] =
{
	// obrót 1
	{ 1, 1, 0, 1,
	  0, 0, 0, 0,
	  0, 0, 0, 0,
	  1, 0, 1, 1 },
	
	{ 0, 1, 0, 0,
	  0, 1, 1, 0,
	  0, 0, 0, 0,
	  0, 0, 1, 0 },
	
	{ 1, 0, 1, 1,
	  0, 0, 0, 0,
	  0, 0, 0, 0,
	  1, 1, 0, 1 },
	  
	// obrót 2
	{ 1, 0, 0, 1,
	  0, 0, 0, 1,
	  1, 0, 0, 0,
	  1, 0, 0, 1 },
	 
	{ 0, 0, 0, 0,
	  0, 0, 0, 1,
	  1, 1, 1, 0,
	  0, 0, 0, 0 },
	  
	{ 1, 0, 0, 1,
	  1, 0, 0, 0,
	  0, 0, 0, 1,
	  1, 0, 0, 1 }
};

/**
 * Stan wszystkich rysowanych świateł:
 * 
 * 0: czerwone;
 * 1: żółte;
 * 2: zielone;
 */
int *light_state;
/*
int light_state[] =
{
	0, 0, 0,    // lewo [lewo, prawo, prosto]
	0, 2, 2,    // dół [lewo, prawo, prosto]
	0, 2, 2,    // góra [lewo, prawo, prosto]
	0, 0, 0,    // prawo [lewo, prawo, prosto]
	0, 0, 0, 0  // piesi
};
*/

/**
 * Struktura wiadomości.
 * Wysyłana do kolejki komunikatów wyświetlana jest przez odpowiedni proces na serwerze.
 * 
 * Typy wiadomości:
 * 0x0A - do serwera
 * 0x0B - do klienta samochodów
 * 0x0C - do klienta pieszych
 * 0x0D - do klienta tramwaju
 * 
 * Zmienna [Msqt] oznacza typ wiadomości w danym programie.
 * 
 * Typy dla serwera:
 * 0x01 - potwierdzenie połączenia (odbiór:tylko klienci)
 * 0x0A - wyświetlenie komunikatu od serwera (odbiór:tylko serwer)
 * 0x0B - wyświetlenie komunikatu od klienta samochodów (odbiór:tylko serwer)
 * 0x0C - wyświetlenie komunikatu od klienta pieszych (odbiór:tylko serwer)
 * 0x0D - wyświetlenie komunikatu od klienta tramwaju (odbiór:tylko serwer)
 * 0x1B - handshake klienta samochodów (odbiór:tylko serwer)
 * 0x1C - handshake klienta pieszych (odbiór:tylko serwer)
 * 0x1D - handshake klienta tramwaju (odbiór:tylko serwer)
 * ....
 * 0x20 - przesłanie maksymalnej wartości obiektów i prędkości odświeżania (odbiór:tylko klient)
 * ....
 * 0xE1 - ustaw ilość obiektów (odbiór:tylko klient)
 * 0xE2 - resetuj pozycje obiektów (odbiór:tylko klient)
 * 0xE3 - zmiana interwału pomiędzy losowaniami obiektów (odbiór:tylko klient)
 * ....
 * 0xF0 - zakończenie aplikacji (każdy sobie))
 * 0xFB - zakończenie klienta samochodów (odbiór:tylko serwer)
 * 0xFC - zakończenie klienta pieszych (odbiór:tylko serwer)
 * 0xFD - zakończenie klineta tramwaju (odbiór:tylko serwer)
 * 0xFF - zakończenie działania serwera (odbiór:tylko klient)
 */
struct MSG_MESSAGE
{
	long Type;                  // typ wiadomości
	int  Msqt;                  // typ wewnętrzny wiadomości
	char Message[MESSAGE_SIZE]; // wiadomość do wyświetlenia
};

Display *display;   // połączenie z serwerem
GC       graphics;  // struktura odpowiedzialna za grafikę
Window   window;    // struktura okna symulatora

// kolor konturów drogi
XColor     road_xcolor;
const char road_color[] = "#000000";

// kolor zebry (przejście dla pieszych)
XColor     zebra_xcolor;
const char zebra_color[] = "#7a7a7a";

// kolor świateł
XColor     light_xcolor[3];
const char light_color[][8] = { "#D9042B", "#F2D027", "#2F942A" }; // czerwony, żółty, zielony

// kolory samochodów
XColor   car_xcolor[3];
char     car_color[3][8] = { "#36673A", "#33A195", "#A60F2B" };

// kolory pieszych
XColor   ped_xcolor[3];
char     ped_color[3][8] = { "#402718", "#101C8A", "#F27405" };

// kolory migaczy (włączony, wyłączony)
XColor   blinker_xcolor[2];
char     blinker_color[2][8] = { "#F29544", "#424242" };

// identyfikator wiadomości i pamięci współdzielonej
int msqid;
int shmid, shmidc, shmidp, shmidt;
int mnpid;

int blinkerinterval = 30;

// struktura informacji o samochodzie
struct CAR_INFO
{
	int x, y, dir, blinker;     // x, y, kierunek i migacz
	int width, height;          // wysokość i szerokość
	int next, xm, ym;           // kierunek w który samochód będzie skręcał
	int xmax, ymax;             // linia skrętu X i Y
	int color;                  // indeks koloru samochodu
	int active;                 // czy samochód jest aktywny
	int before, after;          // indeks samochodu przed i pod
	int interval;
	pid_t pid;                  // identyfikator procesu
}
*cars;

struct PED_INFO
{
	int x, y, dir;          // x, y, kierunek
	int color, xm, ym;      // kolor, linia skrętu X i Y
	int active;             // czy pieszy jest aktywny
	int before, after;      // indeks pieszego przed i po
	pid_t pid;
}
*peds;

struct TRAM_INFO
{
	int x, y;           // x, y
	int width, height;  // szerokość, wysokość
	int active;         // czy pieszy jest aktywny
	int stopy;          // punkt stopu
}
*tram;

/**
 * Wysyłanie wiadomości serwerowej do wyświetlenia przez odpowiedni proces.
 * 
 * @param message Wiadomość do wyświetlenia.
 * @return int Kod błędu zwracany przez funkcję msgsnd.
 */
int send_server_message( const char *message, int flags )
{
	struct MSG_MESSAGE msg;

	msg.Type = 0x0AL;
	msg.Msqt = 0x0A;

	strcpy( msg.Message, message );
	return msgsnd( msqid, &msg, MSGSTR_SIZE, flags );
}

/**
 * Czyści kolejkę klienta o podanym identyfikatorze.
 * 
 * @param client Identyfikator klienta (0x0A, 0x0B, 0x0C, 0x0D)
 * @return void
 */
void clear_client_queue( long client )
{
	struct MSG_MESSAGE msg;
	while( msgrcv(msqid, &msg, MSGSTR_SIZE, client, IPC_NOWAIT) != -1 )
		break;
}

/**
 * Wysyła sygnały zamknięcia aplikacji do klientów.
 * 
 * @param void
 * @return void
 */
void send_close_signal( void )
{
	struct MSG_MESSAGE msg1 = { 0x0BL, 0xFF };
	struct MSG_MESSAGE msg2 = { 0x0CL, 0xFF };
	struct MSG_MESSAGE msg3 = { 0x0DL, 0xFF };

	// nie czekaj, klientów może nie być...
	msgsnd( msqid, &msg1, MSGSTR_SIZE, IPC_NOWAIT );
	msgsnd( msqid, &msg2, MSGSTR_SIZE, IPC_NOWAIT );
	msgsnd( msqid, &msg3, MSGSTR_SIZE, IPC_NOWAIT );
}

/**
 * Inicjalizacja kolorów.
 * Parsuje i przydziela wszystkie kolory zdefiniowane w zmiennych *****_color.
 * 
 * @param void
 * @return void
 */
void init_colors( void )
{
	printf( "Inicjalizacja kolorów.\n" );

	// domyślna mapa kolorów
	Colormap colormap = DefaultColormap( display, 0 );

	// kolor rysowanych linii drogowych
	XParseColor( display, colormap, road_color, &road_xcolor );
	XAllocColor( display, colormap, &road_xcolor );

	// kolor przejścia dla pieszych
	XParseColor( display, colormap, zebra_color, &zebra_xcolor );
	XAllocColor( display, colormap, &zebra_xcolor );

	// światła drogowe
	XParseColor( display, colormap, light_color[0], &light_xcolor[0] );
	XAllocColor( display, colormap, &light_xcolor[0] );
	XParseColor( display, colormap, light_color[1], &light_xcolor[1] );
	XAllocColor( display, colormap, &light_xcolor[1] );
	XParseColor( display, colormap, light_color[2], &light_xcolor[2] );
	XAllocColor( display, colormap, &light_xcolor[2] );

	// tworzenie kolorów dla samochodów 
	XParseColor( display, colormap, car_color[0], &car_xcolor[0] );
	XAllocColor( display, colormap, &car_xcolor[0] );
	XParseColor( display, colormap, car_color[1], &car_xcolor[1] );
	XAllocColor( display, colormap, &car_xcolor[1] );
	XParseColor( display, colormap, car_color[2], &car_xcolor[2] );
	XAllocColor( display, colormap, &car_xcolor[2] );
	
	// kolor migacza
	XParseColor( display, colormap, blinker_color[0], &blinker_xcolor[0] );
	XAllocColor( display, colormap, &blinker_xcolor[0] );
	XParseColor( display, colormap, blinker_color[1], &blinker_xcolor[1] );
	XAllocColor( display, colormap, &blinker_xcolor[1] );

	// tworzenie kolorów dla pieszych
	XParseColor( display, colormap, ped_color[0], &ped_xcolor[0] );
	XAllocColor( display, colormap, &ped_xcolor[0] );
	XParseColor( display, colormap, ped_color[1], &ped_xcolor[1] );
	XAllocColor( display, colormap, &ped_xcolor[1] );
	XParseColor( display, colormap, ped_color[2], &ped_xcolor[2] );
	XAllocColor( display, colormap, &ped_xcolor[2] );
}

/**
 * Przełączanie świateł.
 * Zmienia światła dla samochodów, pieszych i tramwaju.
 * 
 * @param void
 * @return void
 */
void light_switcher( void )
{
	static int counter = 0;
	static int rowrun  = 0;
	static int status  = 0;

	int x = 0;
	
	// przepełnienie, wróć na początek
	if( rowrun >= LIGHT_RUNS )
		rowrun = 0;
		
	// zmiana na zielone lub czerwone
	if( counter < LIGHT_RUN_TIME )
	{
		if( status != 1 )
		{
			send_server_message( COLOR_CYAN("Zmiana koloru świateł."), IPC_NOWAIT );

			for( x = 0; x < 16; ++x )
				light_state[light_matrix[x]] = cross_matrix[rowrun][x] ? 2 : 0;

			status = 1;
		}
	}
	// zmiana na żółte
	else if( counter < LIGHT_RUN_TIME + LIGHT_YELLOW_TIME )
	{
		if( status != 2 )
		{
			int nextrun = rowrun == LIGHT_RUNS - 1 ? 0 : rowrun + 1;
			
			send_server_message( COLOR_MAGNETA("Przygotowanie do zmiany koloru świateł."), IPC_NOWAIT );

			for( x = 0; x < 16; ++x )
			{
				if( cross_matrix[rowrun][x] != cross_matrix[nextrun][x] )
					light_state[light_matrix[x]] = 1;
			}

			status = 2;
		}
	}
	// resetowanie licznika
	else
	{
		counter = 0;
		rowrun++;
	}
	counter++;
}

/**
 * Rysowanie skrzyżowania.
 * Rysuje drogę, zebrę, światła i tory dla tramwaju.
 * 
 * @param void
 * @return void
 */
void draw_crossing( void )
{
	// pozycje świateł (x, y)
	static int light_pos[16][2] =
	{
		{250, 245}, {250, 285}, {250, 265},             // lewo
		{325, 300}, {365, 300}, {345, 300},             // dół
		{305, 170}, {265, 170}, {285, 170},             // góra
		{380, 225}, {380, 185}, {380, 205},             // prawo
		{235, 238}, {395, 238}, {318, 155}, {318, 315}  // światła pieszych
	};
	int idx = 0;

	XSetForeground( display, graphics, zebra_xcolor.pixel );
	
	// rysuj tory
	XDrawLine( display, window, graphics, 214, 0, 214, 480 );
	XDrawLine( display, window, graphics, 224, 0, 224, 480 );
	
	XSetForeground( display, graphics, road_xcolor.pixel );

	// droga pozioma
	XDrawLine( display, window, graphics, 0, 180, 260, 180 );
	XDrawLine( display, window, graphics, 0, 300, 260, 300 );
	XDrawLine( display, window, graphics, 380, 180, 640, 180 );
	XDrawLine( display, window, graphics, 380, 300, 640, 300 );

	// droga pionowa
	XDrawLine( display, window, graphics, 260, 0, 260, 180 );
	XDrawLine( display, window, graphics, 380, 0, 380, 180 );
	XDrawLine( display, window, graphics, 260, 300, 260, 480 );
	XDrawLine( display, window, graphics, 380, 300, 380, 480 );

	// linia podwójna ciągla pozioma
	XDrawLine( display, window, graphics, 0, 239, 260, 239 );
	XDrawLine( display, window, graphics, 0, 241, 260, 241 );
	XDrawLine( display, window, graphics, 380, 239, 640, 239 );
	XDrawLine( display, window, graphics, 380, 241, 640, 241 );

	// linia podwójna ciągla pionowa
	XDrawLine( display, window, graphics, 319, 0, 319, 180 );
	XDrawLine( display, window, graphics, 321, 0, 321, 180 );
	XDrawLine( display, window, graphics, 319, 300, 319, 480 );
	XDrawLine( display, window, graphics, 321, 300, 321, 480 );

	// linie przerywane pionowe
	while( idx < 36 )
	{
		XDrawLine( display, window, graphics, 361, idx * 5 + 1, 361, (idx+1) * 5 - 2 );
		XDrawLine( display, window, graphics, 341, idx * 5 + 1, 341, (idx+1) * 5 - 2 );
		XDrawLine( display, window, graphics, 299, idx * 5 + 1, 299, (idx+1) * 5 - 2 );
		XDrawLine( display, window, graphics, 279, idx * 5 + 1, 279, (idx+1) * 5 - 2 );
	
		XDrawLine( display, window, graphics, 361, 300 + (idx * 5 + 1), 361, 300 + ((idx+1) * 5 - 2) );
		XDrawLine( display, window, graphics, 341, 300 + (idx * 5 + 1), 341, 300 + ((idx+1) * 5 - 2) );
		XDrawLine( display, window, graphics, 299, 300 + (idx * 5 + 1), 299, 300 + ((idx+1) * 5 - 2) );
		XDrawLine( display, window, graphics, 279, 300 + (idx * 5 + 1), 279, 300 + ((idx+1) * 5 - 2) );
		
		idx++;
	}

	// linie przerywane poziome
	idx = 0;
	while( idx < 52 )
	{
		XDrawLine( display, window, graphics, idx * 5 + 1, 199, (idx+1) * 5 - 2, 199 );
		XDrawLine( display, window, graphics, idx * 5 + 1, 219, (idx+1) * 5 - 2, 219 );
		XDrawLine( display, window, graphics, idx * 5 + 1, 261, (idx+1) * 5 - 2, 261 );
		XDrawLine( display, window, graphics, idx * 5 + 1, 281, (idx+1) * 5 - 2, 281 );
	
		XDrawLine( display, window, graphics, 380 + (idx * 5 + 1), 199, 380 + ((idx+1) * 5 - 2), 199 );
		XDrawLine( display, window, graphics, 380 + (idx * 5 + 1), 219, 380 + ((idx+1) * 5 - 2), 219 );
		XDrawLine( display, window, graphics, 380 + (idx * 5 + 1), 261, 380 + ((idx+1) * 5 - 2), 261 );
		XDrawLine( display, window, graphics, 380 + (idx * 5 + 1), 281, 380 + ((idx+1) * 5 - 2), 281 );
	
		idx++;
	}
	
	XSetForeground( display, graphics, zebra_xcolor.pixel );

	// zebra pionowa
	idx = 0;
	while( idx < 11 )
	{
		XDrawLine( display, window, graphics, 232, 184 + idx * 5, 247, 184 + idx * 5 );
		XDrawLine( display, window, graphics, 232, 246 + idx * 5, 247, 246 + idx * 5 );
		XDrawLine( display, window, graphics, 392, 184 + idx * 5, 407, 184 + idx * 5 );
		XDrawLine( display, window, graphics, 392, 246 + idx * 5, 407, 246 + idx * 5 );
		
		idx++;
	}
	
	// zebra pozioma
	idx = 0;
	while( idx < 11 )
	{
		XDrawLine( display, window, graphics, 264 + idx * 5, 152, 264 + idx * 5, 167 );
		XDrawLine( display, window, graphics, 326 + idx * 5, 152, 326 + idx * 5, 167 );
		XDrawLine( display, window, graphics, 264 + idx * 5, 312, 264 + idx * 5, 327 );
		XDrawLine( display, window, graphics, 326 + idx * 5, 312, 326 + idx * 5, 327 );
		
		idx++;
	}

	// światła dla samochodów
	for( idx = 0; idx < 12; ++idx )
	{
		XSetForeground( display, graphics, light_xcolor[light_state[idx]].pixel );
		XFillArc( display, window, graphics, light_pos[idx][0], light_pos[idx][1], 10, 10, 0, 360 * 64 );
	}
	// światła dla pieszych
	for( idx = 12; idx < 14; ++idx )
	{
		XSetForeground( display, graphics, light_xcolor[light_state[idx]].pixel );
		XFillRectangle( display, window, graphics, light_pos[idx][0], light_pos[idx][1], 10, 5 );
	}
	// światła dla samochodów
	for( idx = 14; idx < 16; ++idx )
	{
		XSetForeground( display, graphics, light_xcolor[light_state[idx]].pixel );
		XFillRectangle( display, window, graphics, light_pos[idx][0], light_pos[idx][1], 5, 10 );
	}

	// światła dla tramwaju
	XSetForeground( display, graphics, light_xcolor[light_state[13]].pixel );
	XFillArc( display, window, graphics, 200, 335, 10, 10, 0, 360 * 64 );
}

/**
 * Rysowanie samochodów.
 * 
 * @param void
 * @return void
 */
void draw_cars( void )
{
	static int lcars = 0;

	int x;
	int bx[2], by[2], bw, bh;
	struct CAR_INFO *car;
	int ccars = 0;
	// sprawdź wszystkie samochody
	for( x = 0; x < MAX_CARS; ++x )
	{
		// nie sprawdzaj nieaktywnych
		if( !cars[x].active )
			continue;
			
		car = &cars[x];
		
		// ustaw kolor samochodu
		XSetForeground( display, graphics, car_xcolor[car->color].pixel );
			
		// rysuj samochód
		XFillRectangle( display, window, graphics, car->x, car->y, car->width, car->height );
		
		// kierunek wyświetlania kierunkowskazów
		switch( car->dir )
		{
		case 0: case 1: case 2:
			bx[1] = bx[0] = car->x + 15;
			by[0] = car->y;
			bw = 2;
			bh = 3;
			by[1] = car->y + 7;
		break;
		case 3: case 4: case 5:
			bx[0] = car->x;
			bx[1] = car->x + 7;
			by[1] = by[0] = car->y + 15;
			bw = 3;
			bh = 2;
		break;
		case 6: case 7: case 8:
			bx[0] = car->x;
			bx[1] = car->x + 7;
			by[1] = by[0] = car->y - 2;
			bw = 3;
			bh = 2;
		break;
		case 9: case 10: case 11:
			bx[1] = bx[0] = car->x - 2;
			by[0] = car->y;
			by[1] = car->y + 7;
			bw = 2;
			bh = 3;
		break;
		}
		
		// brak zapalonego kierunku
		if( car->blinker == 0 )
		{
			XSetForeground( display, graphics, blinker_xcolor[1].pixel );
			XFillRectangle( display, window, graphics, bx[0], by[0], bw, bh );
			XFillRectangle( display, window, graphics, bx[1], by[1], bw, bh );
		}
		// zapalony lewy kierunkowskaz
		else if( (car->blinker == 1 && car->ym != 1 && car->xm != -1) || (car->ym == 1 && car->blinker == 2) || (car->xm == -1 && car->blinker == 2) )
		{
			// migacz
			if( car->interval++ > blinkerinterval * 2 )
				car->interval = 0;

			XSetForeground( display, graphics, blinker_xcolor[car->interval > blinkerinterval ? 0 : 1].pixel );
			XFillRectangle( display, window, graphics, bx[0], by[0], bw, bh );
			XSetForeground( display, graphics, blinker_xcolor[1].pixel );
			XFillRectangle( display, window, graphics, bx[1], by[1], bw, bh );
		}
		// zapalony prawy kierunkowskaz
		else 
		{
			// migacz
			if( car->interval++ > blinkerinterval * 2 )
				car->interval = 0;

			XSetForeground( display, graphics, blinker_xcolor[1].pixel );
			XFillRectangle( display, window, graphics, bx[0], by[0], bw, bh );
			XSetForeground( display, graphics, blinker_xcolor[car->interval > blinkerinterval ? 0 : 1].pixel );
			XFillRectangle( display, window, graphics, bx[1], by[1], bw, bh );
		}
		ccars++;
	}

	if( ccars != lcars )
		printf( COLOR_MAGNETA("[KLIENT] ") "Ilość wyświetlanych samochodów: %d.\n", ccars );
	lcars = ccars;
}

/**
 * Rysowanie pieszych.
 * 
 * @param void
 * @return void
 */
void draw_peds( void )
{
	static int lpeds = 0;

	int x;
	struct PED_INFO *ped;
	int cpeds = 0;
	
	// rysuj pieszych
	for( x = 0; x < MAX_PEDS; ++x )
	{
		if( !peds[x].active )
			continue;
		
		ped = &peds[x];
		
		// wypełnienie...
		XSetForeground( display, graphics, ped_xcolor[ped->color].pixel );
		XFillRectangle( display, window, graphics, ped->x, ped->y, 5, 5 );

		cpeds++;
	}

	if( cpeds != lpeds )
		printf( COLOR_BLUE("[KLIENT] ") "Ilość wyświetlanych pieszych: %d.\n", cpeds );
	lpeds = cpeds;
}

/**
 * Rysowanie tramwaju.
 * 
 * @param void
 * @return void
 */
void draw_tram( void )
{
	if( !tram->active )
		return;
	
	XSetForeground( display, graphics, BlackPixel(display, 0) );
	XFillRectangle( display, window, graphics, tram->x, tram->y, tram->width, tram->height );
}

/**
 * Zarządzanie oknem.
 * Tworzenie i wyświetlanie zawartości w oknie.
 * 
 * @param void
 * @return int Kod błędu.
 */
int window_manager( void )
{
	Screen *screen  = NULL;
	int     running = 1,
			scrnum;
	XEvent  ev;
	Atom    wmdelmsg;

	printf( "Tworzenie prostego okna.\n" );
	
	// otwórz połączenie z serwerem
	display = XOpenDisplay( NULL );
	if( !display )
	{
		printf( COLOR_RED("Nie można otworzyć połączenia z serwerem\n") );
		return 1;
	}
	scrnum   = DefaultScreen( display );
	screen   = DefaultScreenOfDisplay( display );
	wmdelmsg = XInternAtom( display, "WM_DELETE_WINDOW", False );

	// tworzenie okna
	window = XCreateSimpleWindow( display, DefaultRootWindow(display), 0, 0, 640, 480, 0,
		BlackPixel(display, scrnum), WhitePixel(display, scrnum) );
	XStoreName( display, window, "[SERWER] Prosty symulator skrzyżowania..." );

	// inicjalizacja kolorów
	init_colors();

	// mapowanie okna
	XSelectInput( display, window, StructureNotifyMask | ExposureMask | KeyPressMask );
	XMapWindow( display, window );

	// przechwytywanie zdarzenia usuwania okna
	XSetWMProtocols( display, window, &wmdelmsg, 1 );

	// grafika
	graphics = XCreateGC( display, window, scrnum, NULL );
	printf( "Wejście w główną pętle programu.\n" );

	// wyślij informacje o oczekiwaniu na klienta
	send_server_message( COLOR_GREEN("Oczekiwanie na klienta..."), IPC_NOWAIT );

	// pętla główna programu
	while( running )
	{
		// przechwytywanie zdarzeń
		while( XPending(display) )
		{
			// następne zdarzenie w kolejce
			XNextEvent( display, &ev );

			// wiadomości klienta (okna)
			if( ev.type == ClientMessage )
			{
				// zamknięcie okna
				if( ev.xclient.data.l[0] == wmdelmsg )
				{
					// wyślij informacje o zakończeniu działania okna
					send_server_message( COLOR_CYAN("Przechwycono sygnał zamknięcia."), IPC_NOWAIT );

					running = 0;
					continue;
				}
			}
		}

		// czyść obraz
		XSetForeground( display, graphics, WhitePixel(display, scrnum) );
		XFillRectangle( display, window, graphics, 0, 0, 640, 480 );

		// rysuj skrzyżowanie
		light_switcher();
		draw_crossing();

		// rysuj obiekty
		draw_cars();
		draw_peds();
		draw_tram();

		// wyświetl obraz
		XFlush( display );

		usleep( SPEED_DOWN );
	}

	// usuń pamięć po obiektach
	XFreeGC( display, graphics );
	XDestroyWindow( display, window );

	// zakończ połączenie z serwerem
	XCloseDisplay( display );

	return 0;
}

/**
 * Zarządzanie wiadomościami.
 * Odbiera, wyświetla, wysyła wiadomości do klientów.
 * 
 * @param void
 * @return int Kod błędu.
 */
int message_manager( void )
{
	struct MSG_MESSAGE msg;

	printf( "Wejście w pętle przechwytywania wiadomości.\n" );
	
	while( 1 )
	{
		// przechwytuj wiadomości przeznaczone dla serwera
		if( msgrcv(msqid, &msg, MSGSTR_SIZE, 0x0A, 0) < 0 )
		{
			printf( CLEAR_LINE );
			printf( COLOR_YELLOW("[SERWER] ") COLOR_RED("Wystąpił błąd podczas pobierania wiadomości.\n") );
			return 1;
		}

		switch( msg.Msqt )
		{
		// komunikaty
		case 0x0A: printf( COLOR_YELLOW("[SERWER] ")  "%s\n", msg.Message ); break;
		case 0x0B: printf( COLOR_MAGNETA("[KLIENT] ") "%s\n", msg.Message ); break;
		case 0x0C: printf( COLOR_BLUE("[KLIENT] ")    "%s\n", msg.Message ); break;
		case 0x0D: printf( COLOR_GREEN("[KLIENT] ")   "%s\n", msg.Message ); break;
		case 0x1B:
		{
			struct MSG_MESSAGE mssg;

			mssg.Type = 0x0BL;
			mssg.Msqt = 0x20;

			mssg.Message[0] = MAX_CARS & 0xFF;
			mssg.Message[1] = (MAX_CARS >> 0x08) & 0xFF;
			mssg.Message[2] = (MAX_CARS >> 0x10) & 0xFF;
			mssg.Message[3] = (MAX_CARS >> 0x18) & 0xFF;

			mssg.Message[4] = SPEED_DOWN & 0xFF;
			mssg.Message[5] = (SPEED_DOWN >> 0x08) & 0xFF;
			mssg.Message[6] = (SPEED_DOWN >> 0x10) & 0xFF;
			mssg.Message[7] = (SPEED_DOWN >> 0x18) & 0xFF;

			msgsnd( msqid, &mssg, MSGSTR_SIZE, IPC_NOWAIT );
		}
		break;
		case 0x1C:
		{
			struct MSG_MESSAGE mssg;

			mssg.Type = 0x0CL;
			mssg.Msqt = 0x20;

			mssg.Message[0] = MAX_PEDS & 0xFF;
			mssg.Message[1] = (MAX_PEDS >> 0x08) & 0xFF;
			mssg.Message[2] = (MAX_PEDS >> 0x10) & 0xFF;
			mssg.Message[3] = (MAX_PEDS >> 0x18) & 0xFF;

			mssg.Message[4] = SPEED_DOWN & 0xFF;
			mssg.Message[5] = (SPEED_DOWN >> 0x08) & 0xFF;
			mssg.Message[6] = (SPEED_DOWN >> 0x10) & 0xFF;
			mssg.Message[7] = (SPEED_DOWN >> 0x18) & 0xFF;

			msgsnd( msqid, &mssg, MSGSTR_SIZE, IPC_NOWAIT );
		}
		break;
		case 0x1D:
		{
			struct MSG_MESSAGE mssg;

			mssg.Type = 0x0DL;
			mssg.Msqt = 0x20;

			mssg.Message[4] = SPEED_DOWN & 0xFF;
			mssg.Message[5] = (SPEED_DOWN >> 0x08) & 0xFF;
			mssg.Message[6] = (SPEED_DOWN >> 0x10) & 0xFF;
			mssg.Message[7] = (SPEED_DOWN >> 0x18) & 0xFF;

			msgsnd( msqid, &mssg, MSGSTR_SIZE, IPC_NOWAIT );
		}
		break;
		// serwer zamknięty, wyzeruj ilość samochodów
		case 0xFB:
		{
			int x;
			for( x = 0; x < MAX_CARS; ++x )
				cars[x].active = 0;
		}
		break;
		case 0xFC:
		{
			int x;
			for( x = 0; x < MAX_PEDS; ++x )
				peds[x].active = 0;
		}
		break;
		case 0xFD:  break;
		default:
			printf( COLOR_YELLOW("[SERWER] ") COLOR_RED("Błędny identyfikator zmiennej Msqt.\n") );
		}
	}

	return 0;
}

/**
 * Funkcja uruchamiana przy wymuszonym zamknięciu serwera (CTL+C).
 * 
 * @param signum Identyfikator sygnału
 * @return void
 */
void server_out( int signum )
{
	struct MSG_MESSAGE msg;

	if( getpid() == mnpid )
	{
		printf( CLEAR_LINE );
		printf( COLOR_YELLOW("[SERWER] ") COLOR_RED("Wymuszone zakończenie działania programu.\n") );
	}
}

/**
 * Funkcja główna serwera.
 * Wyświetla informacje w konsoli i tworzy proces do obsługi grafiki.
 * 
 * @param argc Ilość przekazywanych argumentów do funkcji.
 * @param argv Przekazywane argumenty do funkcji.
 * 
 * @return Zwracany kod błędu (0 - OK, 1 - BŁĄD).
 */
int main( int argc, char **argv )
{
	int  running = 1,
		 wndproc,
		 consproc,
		 wstatus;

	mnpid = getpid();

	// przechwytywanie sygnałów zakończenia aplikacji
	signal( SIGINT, server_out );

	// pamięć współdzielona dla sygnalizacji świetlnej
	if( (shmid = shmget(SHARED_KEY, 16 * sizeof *light_state, IPC_CREAT | 0644)) < 0 )
	{
		printf( COLOR_RED("Błąd tworzenia pamięci współdzielonej.\n") );
		return 1;
	}
	if( (light_state = shmat(shmid, NULL, 0)) == (int*)-1 )
	{
		printf( COLOR_RED("Błąd podczas pobierania adresu pamięci współdzielonej.\n") );
		return 1;
	}
	// utwórz segment pamięci współdzielonej dla samochodów
	if( (shmidc = shmget(CSHARED_KEY, MAX_CARS * sizeof *cars, IPC_CREAT | 0644)) < 0 )
	{
		printf( COLOR_RED("Błąd tworzenia pamięci współdzielonej.\n") );
		return 1;
	}
	if( (cars = shmat(shmidc, NULL, 0)) == (struct CAR_INFO*)-1 )
	{
		printf( COLOR_RED("Błąd podczas pobierania adresu pamięci współdzielonej.\n") );
		return 1;
	}
	// utwórz segment pamięci współdzielonej dla pieszych
	if( (shmidp = shmget(PSHARED_KEY, MAX_PEDS * sizeof *peds, IPC_CREAT | 0644)) < 0 )
	{
		printf( COLOR_RED("Błąd tworzenia pamięci współdzielonej.\n") );
		return 1;
	}
	if( (peds = shmat(shmidp, NULL, 0)) == (struct PED_INFO*)-1 )
	{
		printf( COLOR_RED("Błąd podczas pobierania adresu pamięci współdzielonej.\n") );
		return 1;
	}
	// utwórz segment pamięci współdzielonej dla pieszych
	if( (shmidt = shmget(TSHARED_KEY, sizeof *tram, IPC_CREAT | 0644)) < 0 )
	{
		printf( COLOR_RED("Błąd tworzenia pamięci współdzielonej.\n") );
		return 1;
	}
	if( (tram = shmat(shmidt, NULL, 0)) == (struct TRAM_INFO*)-1 )
	{
		printf( COLOR_RED("Błąd podczas pobierania adresu pamięci współdzielonej.\n") );
		return 1;
	}

	// początkowy stan świateł na drodze
	memset( light_state, 0, sizeof *light_state * 16 );

	// kolejka komunikatów
	if( (msqid = msgget(MESSAGE_KEY, IPC_CREAT | 0660)) < 0 )
	{
		printf( COLOR_RED("Błąd tworzenia kolejki komunikatów!\n") );
		return 1;
	}

	// informacja
	printf( "=================================================\n"
			"@ " COLOR_GREEN("Symulator skrzyżowania") "\n"
			"@ " COLOR_GREEN("Serwer") "\n"
			"=================================================\n"
			"@ " COLOR_BLUE("Kamil Biały") "\n"
			"=================================================\n" );
	
	// utwórz proces potomny - odpowiedzialny za wyświetlanie okna
	wndproc = fork();
	if( wndproc == 0 )
		return window_manager();

	// utwórz proces potomny - odpowiedzialny za wyświetlanie komunikatów
	consproc = fork();
	if( consproc == 0 )
		return message_manager();

	// czekaj na zakończenie jednego z procesów
	wait( &wstatus );

	if( wstatus == -1 )
		printf( COLOR_RED("Wystąpił błąd podczas oczekiwania na proces potomny.\n") );
	else if( wstatus == 1 )
		printf( COLOR_RED("W procesie potomnym wystąpił błąd.\n") );

	// wyślij do klientów sygnał zakończenia
	send_close_signal();

	// poczekaj na klientów jeżeli otrzymali sygnał zakończenia
	sleep(1);

	// zakończ proces
	kill( wndproc, SIGKILL );
	kill( consproc, SIGKILL );

	// usuń kolejkę
	msgctl( msqid, IPC_RMID, NULL );

	// pamięć współdzielona
	shmdt( light_state );
	shmctl( shmid, IPC_RMID, NULL );

	// pamięć współdzielona samochodów
	shmdt( cars );
	shmctl( shmidc, IPC_RMID, NULL );

	// pamięć współdzielona pieszych
	shmdt( peds );
	shmctl( shmidp, IPC_RMID, NULL );

	// pamięć współdzielona pieszych
	shmdt( tram );
	shmctl( shmidt, IPC_RMID, NULL );

	return 0;
}
