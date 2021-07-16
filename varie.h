
// struttura contenente i dati di configurazione, prelevati dal file conf.txt
typedef struct
{
	int K,C,E,T,P,S,A,S1,S2;
	char LOG[101];
} config;


// struttura per collezionare i dati del cliente
typedef struct
{
	pthread_t id;
	int n_acquisti, code_visitate;
	int *finito, *cambia;
	float tempo_tot, tempo_coda;
} dati_cliente;



// struttura nodo di lista per la lista di clienti
typedef struct _nodo_cl
{
	dati_cliente dati;
	struct _nodo_cl *next;
} nodo_cl;



// struttura lista FIFO di clienti
typedef struct 
{
	nodo_cl *primo;
	nodo_cl *ultimo;
	int *chiudi;
	int lung;
} coda_cl;



// struttura identificativa delle casse
typedef struct
{
	pthread_t id;
	coda_cl *coda; // coda clienti della cassa id
} casse;



// struttura nodo di lista per la lista delle casse disponibili
typedef struct _nodo_ca
{
	casse info;
	struct _nodo_ca *next;
} nodo_ca;



// struttura lista delle casse
typedef struct 
{
	nodo_ca *primo;
	nodo_ca *ultimo;
	int lung;
} coda_ca;



// struttura per il collezionamento dei dati delle casse
typedef struct
{
	pthread_t id;
	int n_clienti, n_prodotti, chiusure;
	float tempo_apertura, tempo_medio_servizio;	
} dati_cassa;



// struttura nodo della lista che conterra i dati delle casse chiuse
typedef struct _nodo_res
{
	dati_cassa dati;
	struct _nodo_res *next;
} nodo_res;



// procedura che aggiunge i dati di una cassa appena chiusa, in una lista
void risultati(nodo_res **c, dati_cassa d)
{
	nodo_res *new = malloc(sizeof(nodo_cl));

	new->dati = d;
	new->next = *c;
	*c = new;
}




// funzione che crea una nuova coda di clienti (vuota)
coda_cl *crea_coda_cl()
{
	coda_cl *c = malloc(sizeof(coda_cl));
	c->primo = c->ultimo = NULL;
	c->lung = 0;
	c->chiudi = NULL;
	return c;
}



// procedura che inserisce in coda un nuovo cliente
void inserisci_cl(coda_cl *c, dati_cliente d)
{
	nodo_cl *new = malloc(sizeof(nodo_cl));

	new->dati = d;
	new->next = NULL;
	if (c->primo)
	{
		c->ultimo->next = new;
		c->ultimo = new;
	}
	else
		c->primo = c->ultimo = new;
	c->lung++;
}



// funzione che crea una nuova coda di casse (vuota)
coda_ca *crea_coda_ca()
{
	coda_ca *c = malloc(sizeof(coda_ca));
	c->primo = c->ultimo = NULL;
	c->lung = 0;
	return c;
}



// procedura che inserisce una nuova cassa nella lista di quelle disponibili
void inserisci_ca(coda_ca *c, pthread_t id)
{
	nodo_ca *new;

	if((new = malloc(sizeof(nodo_ca))) == NULL)
	{
		fprintf(stderr, "Malloc durante inserimento di %ld fallita\n",id);
	}
	new->info.id = id;
	if((new->info.coda = crea_coda_cl()) == NULL)
	{
		fprintf(stderr, "Creazione coda clienti per cassa %ld fallita\n",id);
	}
	new->next = NULL;

	if (c->primo)
	{
		c->ultimo->next = new;
		c->ultimo = new;
	}
	else
		c->primo = c->ultimo = new;
	c->lung++;
}



// funzione che restituisce al chiamante (thread cliente) la coda in cui inserirsi (pos scelto casualmente, prima della chiamata)
coda_cl *cerca_cassa(coda_ca *c, int pos)
{
	nodo_ca *corr = c->primo;
	int i = 1;

	while (i != pos)
	{
		corr = corr->next;
		i++;
	}
	return corr->info.coda;
}



// funzione che restituisce al chiamante (thread cassiere) la coda dei clienti della propria cassa
coda_cl *ottieni_cassa(coda_ca *c, pthread_t id)
{
	nodo_ca *corr = c->primo;

	while (corr != NULL && corr->info.id != id)
		corr = corr->next;

	return corr->info.coda;
}



// procedura che elimina dalla coda il cliente appena servito
void rimuovi_cliente(coda_cl *c)
{
	if (c->lung)
	{
		nodo_cl *secondo = c->primo->next;
		free(c->primo);
		c->primo = secondo;
		if (c->primo == NULL) c->ultimo = NULL;
		c->lung--;
	}
}



// funzione che restituisce 1 se la soglia S1 è raggiunta, 0 altrimenti
int S1 (coda_ca *c, int init)
{
	int count = 0;
	nodo_ca *corr = c->primo;
	for (int i = 0; i < c->lung; ++i)
	{
		if (corr->info.coda->lung <= 1) count++;
		if (count >= init) return 1;
	}
	return 0;
}



// funzione che restituisce 1 se la soglia S2 è raggiunta, 0 altrimenti
int S2 (coda_ca *c, int init)
{
	nodo_ca *corr = c->primo;
	for (int i = 0; i < c->lung; ++i)
		if (corr->info.coda->lung >= init) return 1;
	return 0;
}



// procedura che notifica ad una cassa, la sua chiusura
void notifica(coda_ca *c)
{
	nodo_ca *corr = c->primo;

	while (corr != NULL && corr->info.coda->lung > 1)
		corr = corr->next;

	if (corr != NULL) *corr->info.coda->chiudi = 1;
}



// procedura che rimuove una cassa dalla lista delle casse disponibili
void rimuovi_cassa(coda_ca *c, pthread_t id)
{
	nodo_ca *corr = c->primo;
	nodo_ca *prec = NULL;

	while(corr != NULL)
	{
		if (corr->info.id == id)
		{
			if(prec == NULL)
			{
				c->primo = c->primo->next;
				free(corr);
				corr = c->primo;
				c->lung--;
				break;
			}
			else
			{
				prec->next = corr->next;
				free(corr);
				corr = prec->next;
				c->lung--;
				break;
			}
		}
		else
		{
			prec = corr;
			corr = corr->next;
		}
	}
}



// procedura di supporto a "chiudi_casse"
void chiudi_casse_supporto(coda_ca *c)
{
	if (c->lung)
	{
		nodo_ca *secondo = c->primo->next;
		free(c->primo->info.coda);
		free(c->primo);
		c->primo = secondo;
		if (c->primo == NULL) c->ultimo = NULL;
		c->lung--;
	}
}


// procedura che libera la memoria occupata dalla lista di casse disponibili
void chiudi_casse(coda_ca *c)
{
	while (c->lung) chiudi_casse_supporto(c);
	free (c);
}
