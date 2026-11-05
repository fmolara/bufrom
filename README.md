# Diario meteo della Romagna — v0.1.0

Primo prototipo C++ del nucleo del diario, collegato direttamente a **libwreport**.
Legge file BUFR locali, permette di esplorarli attraverso la libreria nativa ed
estrae un profilo limitato di temperature di stazioni al suolo. Produce una
pagina HTML statica e grafici SVG con gnuplot, utilizzabili da Apache.

L'obiettivo di questa versione è avere un'applicazione concreta con cui conoscere
e verificare wreport. Il sorgente è piccolo, diviso per responsabilità e corredato
di campioni reali provenienti dalla suite di test upstream.

## Avvio su Debian

Il target è GNU/Linux, con compilatore C++20 e supporto a `std::format` (GCC 13 o
successivo; Debian 13/trixie è un ambiente adatto). Installare le dipendenze:

```sh
sudo apt install build-essential pkg-config libwreport-dev wreport-common gnuplot-nox
make
make check
```

`make` consulta `pkg-config --cflags --libs libwreport`. `gnuplot` serve a runtime
per `build` e per i test di integrazione; gli altri comandi funzionano anche
senza gnuplot. Non occorrono Python o Node.js per compilare o usare il progetto.

Questa consegna è stata compilata e provata in un ambiente **Ubuntu 24.04**, con
GCC 13.3, libwreport 3.36 e gnuplot 6.0.0. Le dipendenze sono state estratte da
pacchetti ufficiali Ubuntu in un prefisso locale. Non è ancora stata eseguita
una prova su una macchina Debian o su Raspberry Pi.

## Quattro comandi

### 1. Ispezionare il modello nativo

```sh
./build/diario-meteo inspect tests/data/obs0-1.22.bufr
./build/diario-meteo inspect --details tests/data/obs0-1.22.bufr
```

Il programma legge tutti i messaggi contenuti in ciascun file usando
`BufrBulletin::read` e `decode`. Mostra intestazioni, subset e variabili con
`Bulletin::print` o `print_structured`. La data nell'intestazione è indicata come
data di riferimento del bollettino: il comando non la trasforma automaticamente
in data di osservazione.

### 2. Esportare variabili e attributi

```sh
./build/diario-meteo export --output variabili.csv tests/data/obs0-1.22.bufr
```

In assenza di `--output`, il CSV va su standard output. La diagnostica va su
standard error. Le colonne sono:

```text
source,message_index,subset_index,position,kind,parent_code,code,is_set,value,unit,scale,description
```

Ogni variabile mantiene la propria posizione nel subset; descrittori ripetuti
non vengono accorpati in una mappa. Gli attributi sono righe distinte collegate
alla posizione della variabile e al suo `parent_code`. Un valore mancante ha
`is_set=0` e campo `value` vuoto: rimane distinto da uno zero definito.

`source` ha la forma `input1:nome.bufr`, `input2:nome.bufr`, ecc. Il numero
corrisponde all'ordine dei file negli argomenti di questa esecuzione: permette di
distinguere anche file con lo stesso nome, senza pubblicare le directory locali.
Gli indici di messaggio, subset e posizione partono da zero; quello del messaggio
riparte da zero per ciascun file.

Questo CSV serve all'esplorazione. Non comprende tutte le sezioni, le intestazioni
e le sequenze di descrittori del bollettino e non sostituisce il BUFR originale.
Se il CSV indicato esiste già, `export` lo sovrascrive; rifiuta un output che
coincida con uno dei BUFR di input.

### 3. Verificare decodifica e ricodifica

```sh
./build/diario-meteo verify tests/data/*.bufr
```

Per ogni messaggio il programma esegue `decode → encode → decode` e confronta
i bollettini con `Bulletin::diff`. È una verifica di stabilità semantica effettuata
con la stessa libreria. Non richiede identità dei byte e non costituisce una
validazione WMO indipendente. Un errore o una differenza produce exit status non
zero; la verifica non modifica i file di input.

### 4. Costruire il diario di esempio

```sh
./build/diario-meteo build \
    --output site \
    --station 16148 \
    --station 16149 \
    tests/data/synop3new.bufr
```

Aprire `site/index.html` nel browser. I due identificativi corrispondono a Cervia
e Rimini Miramare; il collegamento con l'anagrafica esterna è documentato in
[`tests/data/PROVENANCE.txt`](tests/data/PROVENANCE.txt). La pagina usa i nomi
contenuti nei dati quando disponibili, altrimenti mostra l'identificativo WMO.

**Il campione è storico: 15 agosto 2010, ore 00:00 UTC.** Contiene 66 messaggi,
una stazione per messaggio; il filtro ne seleziona due. C'è un solo punto per
stazione, quindi non viene mostrato un andamento giornaliero inventato.
[`examples/site/index.html`](examples/site/index.html) contiene l'output già
generato con questo stesso comando.

La directory di output deve essere nuova. Per ripetere la prova scegliere un
altro nome, per esempio `site-2`. `--station` è ripetibile; se omesso include
tutte le stazioni compatibili. `--gnuplot /percorso/gnuplot` permette di indicare
un eseguibile diverso da quello trovato nel `PATH`.

L'output contiene:

| File | Contenuto |
|---|---|
| `index.html` | Pagina statica con grafici, tabella e diagnostica |
| `observations.csv` | Tutte le temperature estratte, con origine e indici |
| `temperature-N.svg` | Grafico generato da gnuplot per una serie |
| `temperature-N.dat` | Timestamp Unix UTC e temperatura usati nel grafico |
| `temperature-N.gp` | Programma gnuplot ripetibile dalla directory dell'output |

Le serie sono separate per stazione, descrittore della temperatura e altezza
del sensore. I punti sono ordinati nel tempo, senza interpolazione o aggregazioni.
La tabella HTML mostra al massimo 200 osservazioni; il CSV è completo. La
diagnostica riguarda tutti i messaggi letti, anche quelli non selezionati dal
filtro stazione, e dichiara le assunzioni effettuate.

## Integrazione diretta con wreport

| Modulo | Responsabilità e API native |
|---|---|
| `src/bufr.cpp` | Lettura messaggi, `BufrCodecOptions`, decodifica, attraversamento `Subset`/`Var`, `Varinfo`, attributi e CSV |
| `src/observations.cpp` | Interpretazione del profilo, `isset`, `enqi`/`enqd`, descrittori WMO, conversione tramite `convert_units` |
| `src/render.cpp` | Ordinamento e raggruppamento delle osservazioni, gnuplot, HTML e pubblicazione della directory completa |
| `src/main.cpp` | Interfaccia a riga di comando, selezione delle stazioni e verifica con `encode`/`diff` |
| `tests/native_tests.cpp` | Test C++ sul comportamento della libreria e dell'estrazione |

Il callback di `read_bufr` riceve un riferimento al vero `wreport::BufrBulletin`,
valido per la durata della chiamata. L'interfaccia interna non nasconde il modello
di wreport dietro un formato di scambio. Il programma usa l'API pubblica C++ e si
collega alla libreria di sistema: non include un fork della libreria e non
richiama un convertitore BUFR esterno.

## Profilo meteorologico iniziale

La decodifica nativa e il CSV grezzo sono distinti dall'interpretazione necessaria
per un diario. `build` accetta deliberatamente un sottoinsieme limitato:

- Categoria BUFR `0` e identificativo WMO tradizionale dato da B01001/B01002.
- Una sola temperatura dell'aria definita per subset, fra B12001, B12101,
  B12004 e B12104. Più temperature definite causano l'esclusione del subset.
- Identificativo e data devono precedere la temperatura nel subset. Campi
  ripetuti discordanti o contesti temporali non supportati causano l'esclusione.
- La data deriva da B04001–B04005, con controllo del calendario. B04006 fornisce
  i secondi; quando assente o indefinito si assumono `00`, con diagnostica.
  L'istante è UTC indipendentemente dal fuso del processo. I secondi intercalari
  non sono gestiti.
- B12004/B12104 indicano esplicitamente 2 metri. Per B12001/B12101 l'altezza resta
  sconosciuta; B07032 non è ancora interpretato senza un template riconosciuto.
- Le unità sono lette da `Varinfo` e convertite tramite wreport. I valori mancanti
  vengono esclusi, senza sostituirli con zero.

Un'altezza sconosciuta non prova che due misure provengano dallo stesso livello,
anche quando vengono mostrate nella stessa serie. Servirà riconoscere i template
effettivamente usati dalla fonte prima di trattare tali serie come omogenee.
Gli attributi di qualità restano consultabili nel CSV grezzo; questa versione
non li usa per certificare o filtrare le temperature del diario.

I BUFR inclusi usano B12004 con risoluzione 0,1 K. La visualizzazione in °C con
due decimali non aumenta la precisione della misura: i centesimi derivano dalla
conversione di unità. I test tengono conto anche della convenzione numerica di
`convert_units` in wreport 3.36; i valori dell'applicazione non sono corretti
artificialmente per far coincidere le aspettative dei test.
I due comportamenti della libreria emersi durante lo sviluppo sono descritti
in [`NOTES-wreport.md`](NOTES-wreport.md), con riferimenti al sorgente della
versione utilizzata.

## Apache e aggiornamento

Apache deve servire i file prodotti. Il programma C++ viene eseguito separatamente
dal server web: nella prima prova lo si avvia a mano; in seguito potrà essere
invocato da un timer con i nuovi BUFR acquisiti.

`build` prepara una directory temporanea accanto alla destinazione, esegue
gnuplot e rinomina la directory solo dopo la riuscita di tutti i grafici. Un
errore elimina soltanto quella directory temporanea. Le destinazioni esistenti
sono rifiutate. I permessi finali sono `0755` per la directory e `0644` per i file;
le directory superiori devono essere accessibili al processo che serve i file.

Gnuplot viene avviato con `posix_spawnp`, senza passare da una shell. I suoi script
contengono solo nomi di file generati e numeri. Il testo proveniente dai BUFR è
inserito in HTML e CSV con escaping. La chiamata per impostare la directory del
processo figlio usa l'estensione glibc `posix_spawn_file_actions_addchdir_np`:
questo prototipo ha GNU/Linux come target.

Non sono ancora presenti acquisizione automatica da MeteoHub, archivio storico
persistente, deduplicazione fra acquisizioni, timer o configurazione Apache.
Il comando mantiene in memoria i risultati dell'importazione: è pensato per
campioni e piccoli lotti. Nessun server o sito viene modificato eseguendo i test.

## Verifica

`make check` esegue sette gruppi di test nativi e una prova completa della CLI.
Verifica valori noti delle fixture, attributi, provenienza, date UTC indipendenti
da `TZ`, uso della data osservativa anche modificando quella dell'header, distinzione
fra mancanti e zero, escaping CSV, altezza sconosciuta, rifiuto di temperature
ambigue, lettura di messaggi concatenati, errori su file vuoti o troncati e
ricodifica semantica. La prova CLI usa gnuplot reale, controlla il filtro delle
stazioni e verifica il rifiuto di output esistente e il cleanup dopo un errore.

Le fixture intatte contengono complessivamente 69 messaggi. I casi modificati in
memoria sono esclusivamente test: nessuna osservazione sintetica viene inserita
nella pagina di esempio.

## Passo successivo

Usare un BUFR reale della fonte che alimenterà il diario e confrontarne template,
timestamp, variabili e attributi con il profilo iniziale. Da quel caso si può
aggiungere un secondo profilo di estrazione documentato e un test di regressione;
poi introdurre l'archivio storico e l'acquisizione periodica. Questa progressione
produce casi d'uso e possibili contributi upstream a wreport. L'eventuale lavoro
futuro sui binding Python potrà partire dalle API effettivamente utilizzate.

## Provenienza e licenza

Il codice di questo progetto è distribuito con **GPL-2.0-or-later**; il testo GPL
v2 è in [`COPYING`](COPYING). Le fixture appartengono al sorgente upstream wreport
e mantengono i rispettivi avvisi in `tests/data/LICENSE.wreport` e
`tests/data/debian-copyright.wreport`. URL di origine, checksum e valori attesi
sono riportati in [`tests/data/PROVENANCE.txt`](tests/data/PROVENANCE.txt).

Riferimenti primari:

- [Repository upstream wreport](https://github.com/ARPA-SIMC/wreport)
- [Documentazione C++ di wreport](https://arpa-simc.github.io/wreport/)
- [Sorgente Debian wreport 3.36-1.1](https://sources.debian.org/src/wreport/3.36-1.1/)
- [Descrizione delle fixture upstream](https://sources.debian.org/src/wreport/3.36-1.1/testdata/bufr/README)
