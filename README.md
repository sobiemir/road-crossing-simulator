# roadcross

Kompilacja kodu:

    gcc klient_samochod.c -o klient_samochod -lpthread
    gcc klient_pieszy.c -o klient_pieszy -lpthread
    gcc klient_tramwaj.c -o klient_tramwaj -lpthread
    gcc serwer.c -o serwer -lX11

Uruchomienie:

    ./serwer
    ./klient_samochod
    ./klient_pieszy
    ./klient_tramwaj
