#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "./varie.h"
#include <signal.h>
#include <time.h>



volatile sig_atomic_t sighup = 0;
volatile sig_atomic_t sigquit = 0;

static void gestore_segnali(int signum)
{
	if (signum == 1)
	{
		sighup = 1;
		fprintf(stdout,"Segnale SIGHUP ricevuto. Chiusura supermercato in corso...\n");
		fflush(stdout);
	}
	if (signum == 3)
	{
		sigquit = 1;
		fprintf(stdout,"Segnale SIGQUIT ricevuto. Chiusura supermercato in corso...\n");
		fflush(stdout);
	}
}


static config init;

static pthread_mutex_t mtx_soglia = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_soglia = PTHREAD_COND_INITIALIZER;
static int soglia = 0;

static pthread_mutex_t mtx_logfile = PTHREAD_MUTEX_INITIALIZER;
static FILE *logfile = NULL;

static pthread_mutex_t mtx_casse_disponibili = PTHREAD_MUTEX_INITIALIZER;
static coda_ca *casse_disponibili = NULL;

static pthread_mutex_t mtx_permesso_uscita = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_permesso_uscita = PTHREAD_COND_INITIALIZER;
static int permesso_uscita = 0;

static pthread_mutex_t mtx_tot_casse_attive = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_tot_casse_attive = PTHREAD_COND_INITIALIZER;
static int tot_casse_attive = 0;

static pthread_mutex_t mtx_clienti_attuali = PTHREAD_MUTEX_INITIALIZER;
static int clienti_attuali = 0;

static pthread_mutex_t mtx_clienti_tot = PTHREAD_MUTEX_INITIALIZER;
static int clienti_tot = 0;

static pthread_mutex_t mtx_vendite_tot = PTHREAD_MUTEX_INITIALIZER;
static int vendite_tot = 0;

static pthread_mutex_t mtx_resoconto = PTHREAD_MUTEX_INITIALIZER;
static nodo_res *resoconto = NULL;

static long tempo;


// Funzione per inizializzare il supermercato coi parametri presi dal file conf.txt
config inizializza(const char *file)
{
	config ris;
	int dim = 1024;
	int count = 0;
	char *buf = malloc(dim*sizeof(char));
	char *buf_copia;
	FILE *in = NULL;

	if ((in = fopen(file,"r")) == NULL)
		{
			perror("fopen");
			fclose(in);
			exit(EXIT_FAILURE);
		}

	while (fgets(buf,dim,in) != NULL)
	{
		buf_copia = buf;
		while (*buf != '=')
			buf++;
		buf++;
		switch (count)
		{
			
			case 0: ris.K = atoi(buf);
				break;
			case 1: ris.C = atoi(buf);
				break;
			case 2: ris.E = atoi(buf);
				break;
			case 3: ris.T = atoi(buf);
				break;
			case 4: ris.P = atoi(buf);
				break;
			case 5: ris.S = atoi(buf);
				break;
			case 6: ris.A = atoi(buf);
				break;
			case 7: ris.S1 = atoi(buf);
				break;
			case 8: ris.S2 = atoi(buf);
				break;
			case 9: strcpy(ris.LOG,buf);
				break;
			default: break;
		}
		count++;
		buf = buf_copia;
	}
	free(buf);
	fclose(in);
	return ris;
}


static void *aggiornamenti(void *id)
{
	coda_cl *cl_da_servire;

	pthread_mutex_lock(&mtx_casse_disponibili);
	if ((cl_da_servire = ottieni_cassa(casse_disponibili,(pthread_t) id)) == NULL)
	{
		fprintf(stderr, "Errore acquisizione cassa!\n");
		pthread_mutex_unlock(&mtx_casse_disponibili);
		exit(EXIT_FAILURE);
	}
	int chiudi = *cl_da_servire->chiudi;
	pthread_mutex_unlock(&mtx_casse_disponibili);

	struct timespec t;
	t.tv_sec = 0;
	t.tv_nsec = init.A * 1000000;

	while (1)
	{
		nanosleep(&t, NULL);

		pthread_mutex_lock(&mtx_soglia);
		while (soglia) pthread_cond_wait(&cond_soglia,&mtx_soglia);
		soglia = 1;
		pthread_cond_signal(&cond_soglia);
		pthread_mutex_unlock(&mtx_soglia);

		if (sighup || sigquit || chiudi) break;
	}
	pthread_detach(pthread_self());
	return NULL;
}



static void *cassiere(void *arg)
{

	pthread_mutex_lock(&mtx_tot_casse_attive);
	tot_casse_attive++;
	pthread_mutex_unlock(&mtx_tot_casse_attive);

	struct timespec inizio,fine;
	clock_gettime(CLOCK_REALTIME,&inizio);

	dati_cassa CA;

	CA.id = pthread_self();
	CA.n_clienti = 0;
	CA.n_prodotti = 0;
	CA.tempo_apertura = 0;
	CA.tempo_medio_servizio = 0;
	CA.chiusure = 0;

	float tot_servizio = 0;

	int tempo_servizio_fisso;
	unsigned int seed = CA.id+tempo;
	while ((tempo_servizio_fisso = rand_r(&seed) % (81)) < 20);

	coda_cl *cl_da_servire;

	pthread_mutex_lock(&mtx_casse_disponibili);
	if ((cl_da_servire = ottieni_cassa(casse_disponibili,pthread_self())) == NULL)
	{
		fprintf(stderr, "Errore acquisizione cassa!\n");
		pthread_mutex_unlock(&mtx_casse_disponibili);
		exit(EXIT_FAILURE);
	}
	int chiudi = 0;
	cl_da_servire->chiudi = &chiudi;
	pthread_mutex_unlock(&mtx_casse_disponibili);

	pthread_t agg;
	
	if (pthread_create(&agg, NULL, aggiornamenti, (pthread_t*) pthread_self()) != 0)
		{
			fprintf(stderr, "Creazione thread per gli aggiornamenti col direttore fallita.\n");
			exit(EXIT_FAILURE);
		}


	while(1)
	{

		while (!cl_da_servire->lung && !sighup && !sigquit && !chiudi);
		if (!sighup && !sigquit && !chiudi)
		{
			dati_cliente cl = cl_da_servire->primo->dati;
			struct timespec t;
			t.tv_sec = 0;
			t.tv_nsec = (tempo_servizio_fisso * 1000000) + (cl.n_acquisti * init.S * 1000000);
			nanosleep(&t, NULL);
			tot_servizio += (float) t.tv_nsec/1000000000;

			CA.n_clienti++;
			CA.n_prodotti += cl.n_acquisti;

			pthread_mutex_lock(&mtx_vendite_tot);
			vendite_tot += cl.n_acquisti;
			pthread_mutex_unlock(&mtx_vendite_tot);

			*cl_da_servire->primo->dati.finito = 1;
			pthread_mutex_lock(&mtx_casse_disponibili);
			rimuovi_cliente(cl_da_servire);
			pthread_mutex_unlock(&mtx_casse_disponibili);
		}


		if (sighup)
		{
			while(cl_da_servire->lung)
			{
				dati_cliente cl = cl_da_servire->primo->dati;

				struct timespec t;
				t.tv_sec = 0;
				t.tv_nsec = (tempo_servizio_fisso * 1000000) + (cl.n_acquisti * init.S * 1000000);
				nanosleep(&t, NULL);
				tot_servizio += (float) t.tv_nsec/1000000000;

				CA.n_clienti++;
				CA.n_prodotti += cl.n_acquisti;

				pthread_mutex_lock(&mtx_vendite_tot);
				vendite_tot += cl.n_acquisti;
				pthread_mutex_unlock(&mtx_vendite_tot);

				*cl_da_servire->primo->dati.finito = 1;
				pthread_mutex_lock(&mtx_casse_disponibili);
				rimuovi_cliente(cl_da_servire);
				pthread_mutex_unlock(&mtx_casse_disponibili);
			}
			break;
		}

		if (sigquit)
		{
			while(cl_da_servire->lung)
			{
				*cl_da_servire->primo->dati.finito = 1;
				pthread_mutex_lock(&mtx_casse_disponibili);
				rimuovi_cliente(cl_da_servire);
				pthread_mutex_unlock(&mtx_casse_disponibili);
			}
			break;
		}
		
		if (chiudi)
		{
			pthread_mutex_lock(&mtx_casse_disponibili);
			while(cl_da_servire->lung)
			{
				*cl_da_servire->primo->dati.cambia = 1;
				rimuovi_cliente(cl_da_servire);
			}
			rimuovi_cassa(casse_disponibili,pthread_self());
			pthread_mutex_unlock(&mtx_casse_disponibili);
			break;
		}
	}

	clock_gettime(CLOCK_REALTIME,&fine);
	CA.tempo_apertura = (fine.tv_sec - inizio.tv_sec) + (float)(fine.tv_nsec - inizio.tv_nsec)/1000000000;
	CA.tempo_medio_servizio = (float) tot_servizio/CA.n_clienti;
	CA.chiusure++;

	pthread_mutex_lock(&mtx_resoconto);
	risultati(&resoconto,CA);
	pthread_mutex_unlock(&mtx_resoconto);

	pthread_mutex_lock(&mtx_tot_casse_attive);
	tot_casse_attive--;
	if (!tot_casse_attive) pthread_cond_signal(&cond_tot_casse_attive);
	pthread_mutex_unlock(&mtx_tot_casse_attive);

	pthread_detach(pthread_self());	
	return NULL;

}



static void *cliente(void *arg)
{
	struct timespec entrata,uscita,inizio,fine;

	clock_gettime(CLOCK_REALTIME,&entrata);

	pthread_mutex_lock(&mtx_clienti_attuali);
	clienti_attuali++;
	pthread_mutex_unlock(&mtx_clienti_attuali);

	dati_cliente CL;

	int flag_finito = 0;
	CL.finito = &flag_finito;

	int flag_cambia = 0;
	CL.cambia = &flag_cambia;

	CL.id = pthread_self();

	unsigned int seed = CL.id+tempo;
	int t_acquisti;
	while ((t_acquisti = rand_r(&seed) % (init.T + 1)) < 10);
	struct timespec t;
	t.tv_sec = 0;
	t.tv_nsec = t_acquisti * 1000000;
	nanosleep(&t, NULL);

	CL.tempo_tot = (float) t_acquisti/1000;
	CL.tempo_coda = 0;

	CL.n_acquisti = rand_r(&seed) % (init.P + 1);
	CL.code_visitate = 0;

	if (sigquit)
	{
		pthread_mutex_lock(&mtx_logfile);
		fprintf(logfile, "| Cliente %ld | Interrotto da SIGQUIT |\n",CL.id);
		pthread_mutex_unlock(&mtx_logfile);

		pthread_mutex_lock(&mtx_clienti_attuali);
		clienti_attuali--;
		pthread_mutex_unlock(&mtx_clienti_attuali);

		pthread_detach(pthread_self());
		return NULL;
	}

	if (CL.n_acquisti == 0)
	{
		pthread_mutex_lock(&mtx_permesso_uscita);
		while (!permesso_uscita) pthread_cond_wait(&cond_permesso_uscita, &mtx_permesso_uscita);

		pthread_mutex_lock(&mtx_logfile);
		fprintf(logfile, "| Cliente %ld | Acquisti %d | Tempo tot. %0.3fs | Tempo in coda %0.3fs | Code visitate: %d |\n",CL.id, CL.n_acquisti, CL.tempo_tot, CL.tempo_coda, CL.code_visitate);
		pthread_mutex_unlock(&mtx_logfile);

		permesso_uscita = 0;
		pthread_cond_signal(&cond_permesso_uscita);
		pthread_mutex_unlock(&mtx_permesso_uscita);

		pthread_mutex_lock(&mtx_clienti_attuali);
		clienti_attuali--;
		pthread_mutex_unlock(&mtx_clienti_attuali);

		pthread_detach(pthread_self());
		return NULL;
	}
	
	// decido che cassa scegliere.
	int scelta_cassa;
	pthread_mutex_lock(&mtx_casse_disponibili);
	while ((scelta_cassa = rand_r(&seed) % (casse_disponibili->lung + 1)) < 1);
	CL.code_visitate++;
	clock_gettime(CLOCK_REALTIME,&inizio);
	// mi metto in coda nella cassa scelta
	inserisci_cl(cerca_cassa(casse_disponibili,scelta_cassa),CL);
	pthread_mutex_unlock(&mtx_casse_disponibili);


	// aspetto che il cassiere finisca di servire, nell'attesa controllo che non debba cambiare cassa
	while(1)
	{
		if (flag_cambia)
		{
			flag_cambia = 0;
			pthread_mutex_lock(&mtx_casse_disponibili);
			while ((scelta_cassa = rand_r(&seed) % (casse_disponibili->lung + 1)) < 1);
			// mi rimetto in coda nella cassa scelta
			CL.code_visitate++;
			inserisci_cl(cerca_cassa(casse_disponibili,scelta_cassa),CL);
			pthread_mutex_unlock(&mtx_casse_disponibili);
		}

		if (flag_finito) break;
	}


	// calcolo tempo in coda e totale
	clock_gettime(CLOCK_REALTIME,&fine);
	CL.tempo_coda = (fine.tv_sec - inizio.tv_sec) + (float)(fine.tv_nsec - inizio.tv_nsec)/1000000000;

	clock_gettime(CLOCK_REALTIME,&uscita);
	CL.tempo_tot = (uscita.tv_sec - entrata.tv_sec) + (float)(uscita.tv_nsec - entrata.tv_nsec)/1000000000;

	// aggiorno il file log
	if (sigquit)
	{
		pthread_mutex_lock(&mtx_logfile);
		fprintf(logfile, "| Cliente %ld | Interrotto da SIGQUIT |\n",CL.id);
		pthread_mutex_unlock(&mtx_logfile);
	}
	else
	{
		pthread_mutex_lock(&mtx_logfile);
		fprintf(logfile, "| Cliente %ld | Acquisti %d | Tempo tot. %0.3fs | Tempo in coda %0.3fs | Code visitate: %d |\n",CL.id, CL.n_acquisti, CL.tempo_tot, CL.tempo_coda, CL.code_visitate);
		pthread_mutex_unlock(&mtx_logfile);
	}

	pthread_mutex_lock(&mtx_clienti_attuali);
	clienti_attuali--;
	pthread_mutex_unlock(&mtx_clienti_attuali);

	if (!sigquit)
	{
		pthread_mutex_lock(&mtx_clienti_tot);
		clienti_tot++;
		pthread_mutex_unlock(&mtx_clienti_tot);
	}

	pthread_detach(pthread_self());
	return NULL;
}



static void *controllo_clienti_P0(void *arg)
{
	while (1)
	{
		pthread_mutex_lock(&mtx_permesso_uscita);
		while (permesso_uscita) pthread_cond_wait(&cond_permesso_uscita, &mtx_permesso_uscita);
		permesso_uscita = 1;
		pthread_cond_signal(&cond_permesso_uscita);
		pthread_mutex_unlock(&mtx_permesso_uscita);
		if (sighup || sigquit) break;
	}

	pthread_detach(pthread_self());
	return NULL;
}



static void *controllo_clienti_E(void *arg)
{
	while (1)
	{
		pthread_mutex_lock(&mtx_clienti_attuali);
		if (clienti_attuali <= init.C - init.E)
		{
			for (int i = 0; i < init.E; ++i)
			{
				pthread_t clid;

				if (pthread_create(&clid, NULL, cliente, NULL) != 0)
				{
					fprintf(stderr, "Creazione del thread cliente fallita.\n");
					exit(EXIT_FAILURE);
				}
			}
		}
		pthread_mutex_unlock(&mtx_clienti_attuali);
		if (sighup || sigquit) break;
	}
	pthread_detach(pthread_self());
	return NULL;
}


static void *controllo_casse(void *arg)
{
	while(1)
	{
		pthread_mutex_lock(&mtx_soglia);
		while (!soglia) pthread_cond_wait(&cond_soglia,&mtx_soglia);
		
		pthread_mutex_lock(&mtx_casse_disponibili);
		if (S1(casse_disponibili, init.S1))	
			if (casse_disponibili->lung > 1)		
				notifica(casse_disponibili);

		if (S2(casse_disponibili, init.S2))
			if (casse_disponibili->lung < init.K)
			{
				pthread_t cid;
				if (pthread_create(&cid, NULL, cassiere, NULL) != 0)
				{
					fprintf(stderr, "Apertura della nuova cassa fallita.\n");
					exit(EXIT_FAILURE);
				}
				inserisci_ca(casse_disponibili,cid);
			}
		pthread_mutex_unlock(&mtx_casse_disponibili);

		soglia = 0;
		pthread_cond_signal(&cond_soglia);
		pthread_mutex_unlock(&mtx_soglia);
		if (sighup || sigquit) break;
	}
	pthread_detach(pthread_self());
	return NULL;
}



static void *direttore(void *arg)
{
	struct timespec t;
	t.tv_sec = 2;
	t.tv_nsec = 0;

	// Creo la coda di casse disponibili
	pthread_mutex_lock(&mtx_casse_disponibili);
	if ((casse_disponibili = crea_coda_ca()) == NULL)
	{
		fprintf(stderr, "Creazione coda casse_disponibili fallita.");
		pthread_mutex_unlock(&mtx_casse_disponibili);
		exit(EXIT_FAILURE);
	}
	pthread_mutex_unlock(&mtx_casse_disponibili);


	// Apro la prima cassa
	pthread_mutex_lock(&mtx_casse_disponibili);
	pthread_t cid;

	if (pthread_create(&cid, NULL, cassiere, NULL) != 0)
	{
		fprintf(stderr, "Apertura della prima cassa fallita.\n");
		exit(EXIT_FAILURE);
	}
	inserisci_ca(casse_disponibili,cid);
	pthread_mutex_unlock(&mtx_casse_disponibili);


	// Avvio il thread per la gestione delle casse
	pthread_t did2;
	
	if (pthread_create(&did2, NULL, controllo_casse, NULL) != 0)
	{
		fprintf(stderr, "Creazione thread gestione casse fallita.\n");
		exit(EXIT_FAILURE);
	}


	// Faccio entrare i primi C clienti
	for (int i = 0; i < init.C; ++i)
	{
		pthread_t clid;

		if (pthread_create(&clid, NULL, cliente, NULL) != 0)
		{
			fprintf(stderr, "Creazione del thread cliente numero %d fallita.\n", i);
			exit(EXIT_FAILURE);
		}
	}


	// Avvio un thread per gestire i clienti in uscita con P=0
	pthread_t did3;

	if (pthread_create(&did3, NULL, controllo_clienti_P0, NULL) != 0)
		{
			fprintf(stderr, "Creazione thread gestione clienti in uscita fallita.\n");
			exit(EXIT_FAILURE);
		}


	// Avvio un thread per gestire i clienti in entrata (valore E)
	nanosleep(&t,NULL);
	pthread_t did4;
	
	if (pthread_create(&did4, NULL, controllo_clienti_E, NULL) != 0)
		{
			fprintf(stderr, "Creazione thread gestione clienti in entrata fallita.\n");
			exit(EXIT_FAILURE);
		}



	//aspetto il segnale di uscita
	while(!sighup && !sigquit);
	pthread_mutex_lock(&mtx_tot_casse_attive);
	while(tot_casse_attive) pthread_cond_wait(&cond_tot_casse_attive,&mtx_tot_casse_attive);
	pthread_mutex_unlock(&mtx_tot_casse_attive);

	pthread_mutex_lock(&mtx_casse_disponibili);
	chiudi_casse(casse_disponibili);
	pthread_mutex_unlock(&mtx_casse_disponibili);

	return NULL;
}




int main(int argc, char const *argv[])
{
	tempo = time(NULL);

	if (argc != 2)
	{
		fprintf(stderr, "Utilizzo: %s conf.txt\n", argv[0]);
		return (EXIT_FAILURE);
	}

	if (strcmp(argv[1],"conf.txt") != 0)
	{
		fprintf(stderr, "Il file da usare come argomento DEVE essere 'conf.txt', presente in directory.\n");
		return (EXIT_FAILURE);
	}


	// installo il gestore per i segnali SIGHUP e SIGQUIT
	struct sigaction s;
	memset(&s,0,sizeof(s));
	s.sa_handler = gestore_segnali;
	sigaction(SIGHUP,&s,NULL);
	sigaction(SIGQUIT,&s,NULL);


	// chiamo la funzione per la configuarazione iniziale del supermercato
	init = inizializza(argv[1]);

	// apro il file in cui verranno registrati tutti i dati collezionati
	if ((logfile = fopen(init.LOG,"w")) == NULL)
	{
		perror("fopen");
		fclose(logfile);
		return (EXIT_FAILURE);
	}

	pthread_t did;

	if (pthread_create(&did, NULL, direttore, NULL) != 0)
	{
		fprintf(stderr,"Thread direttore non avviato\n");
		return(EXIT_FAILURE);
	}

	pthread_join(did, NULL);

	// aggiorno il file log e alla chiusura libero la memoria
	nodo_res *corr = resoconto;
	pthread_mutex_lock(&mtx_logfile);
	while (corr != NULL)
	{	
		fprintf(logfile,"| Cassa %ld | Clienti serviti %d | Prodotti %d | Tempo apertura %0.3fs | Tempo medio servizio %0.3fs | Chiusure %d |\n",corr->dati.id, corr->dati.n_clienti, corr->dati.n_prodotti, corr->dati.tempo_apertura, corr->dati.tempo_medio_servizio, corr->dati.chiusure);
		corr = corr->next;
	}
	pthread_mutex_unlock(&mtx_logfile);

	while (resoconto != NULL)
	{
		corr = resoconto;
		resoconto = resoconto->next;
		free (corr);
	}

	fprintf(logfile, "CLIENTI TOTALI: %d\n",clienti_tot);
	fprintf(logfile, "VENDITE TOTALI: %d\n",vendite_tot);

	fclose(logfile);

	return 0;
}
