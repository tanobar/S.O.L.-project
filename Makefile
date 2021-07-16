
CC		=	gcc
CFLAGS	=	-g -Wall -o

.PHONY: test clean all

all: ./supermercato

./supermercato: ./supermercato.c ./varie.h
	$(CC) $(CFLAGS) $@ $< -lpthread

test:
	./supermercato conf.txt & sleep 25s; \
	killall -s SIGHUP supermercato; \
	chmod +x ./analisi.sh; \
	./analisi.sh; \

clean:
	rm -f supermercato resoconto.log
