# Road Crossing Simulator

Symulator Skrzyżowania :car:

Napisany w języku C dla systemu Linux.
Używa biblioteki X11 oraz pthread.

## Kompilacja programu

Sprawdź czy posiadasz na swoim komputerze biblioteki X11 i pthread.
Jeżeli tak, poniższy kod nie powinien generować błędów:

    $ mkdir output
    $ cd output
    $ gcc ../klient_samochod.c -o klient_samochod -lpthread
    $ gcc ../klient_pieszy.c -o klient_pieszy -lpthread
    $ gcc ../klient_tramwaj.c -o klient_tramwaj -lpthread
    $ gcc ../serwer.c -o serwer -lX11

Oczywiście, jeżeli folder `output` już istnieje, należy pominąć linię, zawierającą komendę `mkdir`.

Możesz również skompilować program w ten sposób:

    $ ./compile.sh

Skrypt ten robi to samo co kod powyżej, z tą różnicą, że dzieje się to automatycznie, razem z operacją tworzenia folderu.

## Uruchamianie

Przejdź do folderu `output` i otwórz 4 oddzielne terminale, ponieważ cały program tworzą cztery pliki - jeden serwer i trzech klientów.

Na początku otwórz serwer, wpisując:

    $ ./serwer

To powinno wyświetlić okno wyglądające mniej więcej tak:

![EmptxyServer](http://img.aculo.pl/roadcsimulator/server.png)

Następnie uruchom przynajmniej jednego z klientów. Każdy klient powinien automatycznie połączyć się z serwerem. 
Aby uruchomić klientów, wpisz poniższe linijki, każdą w oddzielnym terminalu:

    $ ./klient_pieszy
    $ ./klient_samochod
    $ ./klient_tramwaj

Tym sposobem uruchomiony został serwer wraz z klientami.
Po wpisaniu powyższych linijek powinien się pokazać tramwaj. Co się stało więc z pieszymi i samochodami?

## Komendy klientów

Każdy klient posiada możliwość wpisywania komend.
Komenda `help` wyświetla informacje o dostępnych poleceniach dla aktualnego klienta.
To jest przykładowa pomoc dla klienta samochodów:

![HelpExample](http://img.aculo.pl/roadcsimulator/helpcar.png)

Aby wyświetlić więc samochody lub pieszych, powiedzmy w liczbie 60, wystarczy wpisać:

    $ set 60

Można również ustawić interwał pomiędzy kolejnymi losowaniami samochodów lub pieszych, wpisując:

    $ ival 10

Powyższe komendy nie są dostępne dla klienta tramwaju, gdyż wyświetlany może być tylko jeden tramwaj.

Całość po ustawieniu odpowiednio klientów wygląda mniej więcej tak:

![Preview](http://img.aculo.pl/roadcsimulator/preview.png)
