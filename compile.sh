
DIR="output"
CLPED="klient_pieszy"
CLCAR="klient_samochod"
CLTRAM="klient_tramwaj"
SERVER="serwer"

if [ ! -d $DIR ]; then
	mkdir $DIR
fi

cd $DIR
if gcc "../$CLPED.c" -o "$CLPED" -lpthread; then
	echo "$CLPED compiled successfully"
else
	echo "Compile failed for $CLPED"
fi
if gcc "../$CLCAR.c" -o "$CLCAR" -lpthread; then
	echo "$CLCAR compiled successfully"
else
	echo "Compile failed for $CLCAR"
fi
if gcc "../$CLTRAM.c" -o "$CLTRAM" -lpthread; then
	echo "$CLTRAM compiled successfully"
else
	echo "Compile failed for $CLTRAM"
fi
if gcc "../$SERVER.c" -o "$SERVER" -lX11; then
	echo "$SERVER compiled successfully"
else
	echo "Compile failed for $SERVER"
fi
