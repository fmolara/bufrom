# BufRom — Diario meteo della Romagna

BufRom è un piccolo programma **C++20 collegato direttamente a libwreport**.
Acquisisce temperature in BUFR da MeteoHub, le archivia in SQLite e genera
HTML, CSV e grafici SVG con gnuplot. Apache serve soltanto i file statici.

**Stato: 0.2.0-dev, verificata su Debian 13 in sviluppo.** Un aggiornamento live
ha prodotto serie reali di due stazioni della Romagna. La pubblicazione HTTPS
su quixote.molara.net e l'attivazione/verifica dello scheduler in produzione
sono ancora da completare: non esiste ancora un URL del diario verificato.
Il repository è <https://github.com/fmolara/bufrom>.

## Compilazione e test su Debian

Questa sezione è riservata alla **macchina di sviluppo**. Per un server che esegue
il binario già compilato seguire la sezione
[Installazione in produzione, senza compilazione](#installazione-in-produzione-su-debian-senza-compilazione).

Dipendenze da installare come amministratore (non è necessario usare sudo se
l'amministratore accede direttamente come root):

```sh
apt install build-essential pkg-config libwreport-dev wreport-common \
    libsqlite3-dev libssl-dev gnuplot-nox curl jq util-linux coreutils
```

Come utente ordinario, nella directory del progetto:

```sh
make -j2
make check
./build/bufrom --help
```

GCC 13 o successivo con `std::format`; mantenuti Makefile e C++20. Le sole
librerie aggiunte sono SQLite e OpenSSL (SHA-256). L'acquisizione usa Bash,
curl e jq: la decodifica e l'interpretazione restano nel nucleo C++.
I test di pipeline usano Python 3 per controllare HTML/CSV; `make check` è offline
e non richiede account, rete, Apache o privilegi. Python non è usato dal servizio.

La suite conserva i sette gruppi nativi e l'integrazione del prototipo, aggiunge
la fixture MeteoHub reale di 648 byte e controlla identità fra reti, timestamp,
livello, unità, mancanti, persistenza, duplicati, rettifiche, conflitti e rollback
SQLite. Una prova di pipeline con input temporanei sintetici controlla anche
fallimenti di download/gnuplot, asset versionati e ripristino della release.
Le prove live sono separate: dettagli e versioni in
[VALIDATION-debian.txt](VALIDATION-debian.txt).

## Installazione in produzione su Debian, senza compilazione

Sul server è sufficiente installare un eseguibile già compilato e le dipendenze
di esecuzione. Il binario deve essere preparato per la stessa architettura e una
versione Debian compatibile con il server: quello verificato in sviluppo è per
**Debian 13 (trixie), amd64**. Per Debian 12 o altre architetture occorre preparare
un binario adatto sulla macchina di sviluppo.

Attualmente non sono pubblicati un pacchetto `.deb` o un archivio binario nelle
Release GitHub. Gli archivi dei sorgenti non contengono l'eseguibile. Un archivio
di esecuzione si può preparare sulla macchina di sviluppo, dopo build e test,
includendo soltanto questi file del progetto già compilato:

```sh
tar -czf ../bufrom-runtime-debian13-amd64.tar.gz \
    build/bufrom tables scripts config systemd README.md OPERATIONS.md COPYING
```

Accompagnare l'archivio con il suo checksum SHA-256 e i sorgenti corrispondenti
allo stesso commit, con le rispettive licenze. Trasferire questi file sul server.
Non occorrono compilatore, `make`, `pkg-config`, pacchetti `-dev`, Python o Git
per eseguire BufRom in produzione.

Su **Debian 13**, l'amministratore installa le dipendenze di esecuzione come root:

```sh
apt install --no-install-recommends \
    libwreport3t64 wreport-common libsqlite3-0 libssl3t64 libstdc++6 \
    gnuplot-nox fonts-liberation curl jq bash util-linux coreutils findutils \
    ca-certificates tar gzip
```

**In produzione usare `libwreport3t64`, senza `libwreport-dev`, `libsqlite3-dev`
o `libssl-dev`.** Il comando nella sezione «Compilazione e test» è destinato
alla macchina di sviluppo. In particolare, `libwreport-dev` dipende da
`liblua5.1-0-dev`, che raccomanda Libtool; attraverso ulteriori raccomandazioni
e dipendenze possono essere installati anche Automake e Autoconf. Questi strumenti
non servono per eseguire BufRom.

`--no-install-recommends` mantiene le dipendenze obbligatorie ed evita, fra l'altro,
la catena di programmi raccomandati da gnuplot attraverso `groff`, come Ghostscript
e ImageMagick, non necessari ai grafici SVG del diario. `fonts-liberation` viene
installato esplicitamente per rendere disponibili i font.

Le altre librerie necessarie sono risolte da APT. Apache deve essere già
configurato per servire la directory pubblica scelta; il programma viene
eseguito separatamente dal server web. `sqlite3` è facoltativo per manutenzione
e backup da riga di comando.

Come utente del servizio, verificare il checksum ricevuto e installare l'archivio
in una directory nuova, esterna alla document root. Per esempio:

```sh
BUFROM_DIR="$HOME/.local/opt/bufrom-0.2.0-dev"
mkdir -p "$HOME/.local/opt"
mkdir "$BUFROM_DIR"
tar -xzf /percorso/bufrom-runtime-debian13-amd64.tar.gz -C "$BUFROM_DIR"
"$BUFROM_DIR/build/bufrom" --version
ldd "$BUFROM_DIR/build/bufrom"
install -d -m 700 "$HOME/.config/bufrom"
install -m 600 "$BUFROM_DIR/config/bufrom.conf.example" \
    "$HOME/.config/bufrom/bufrom.conf"
```

`ldd` deve risolvere tutte le librerie, senza righe `not found`. Conservare la
struttura dell'archivio: `build/bufrom` individua le tabelle in `../tables`
rispetto alla propria directory, e gli script richiamano altri file del pacchetto.
Adattare la configurazione privata impostando `PROGRAM` al percorso assoluto di
`build/bufrom`, `STATE_DIR` a una directory privata esterna al pacchetto e alla
document root, e `PUBLIC_DIR` alla directory dedicata servita da Apache.
Gli esempi propongono i percorsi dell'utenza di servizio `wreport`; adattarli
se si sceglie un'altra disposizione o un'installazione nella home dell'utente.

Dopo la configurazione, effettuare il primo aggiornamento come utente del servizio:

```sh
"$BUFROM_DIR/scripts/update.sh" "$HOME/.config/bufrom/bufrom.conf"
```

Verificare quindi la pagina attraverso il suo URL HTTPS. Per attivare il timer,
consultare [OPERATIONS.md](OPERATIONS.md#pianificazione), adattando utente, percorsi
e configurazione dell'unità al pacchetto installato. Per gli aggiornamenti
successivi installare un nuovo archivio compatibile in una nuova directory e
seguire la procedura operativa; database e raw rimangono nella stessa `STATE_DIR`.
Questa procedura non è ancora stata verificata sul server di produzione.
Per il server quixote è prevista l'utenza di servizio dedicata `wreport`:
[creazione, percorsi e permessi](OPERATIONS.md#utente-di-servizio-wreport).
Gli esempi di configurazione e unità systemd sono predisposti per questa utenza.

## Dal BUFR al diario

1. `scripts/fetch-meteohub.sh` usa l'endpoint pubblico ufficiale della mappa,
   con area, rete e intervallo UTC limitati. Controlla HTTP, tipo della risposta,
   magic BUFR e decodifica nativa completa; salva ricevuta con query, hash e ora.
2. `src/bufr.cpp` usa `BufrBulletin::read`, `BufrCodecOptions::create` e
   `BufrBulletin::decode`. Attraversa `subsets`, `Var`, `Varinfo` e `next_attr`.
   Rifiuta file vuoti, troncati, HTML/JSON e byte estranei anche in coda.
3. `src/meteohub.cpp` riconosce il template osservato, mantiene le posizioni
   delle variabili e interpreta rete, coordinate, calendario, livello e tempo.
   Usa `isset`, `enqi`, `enqd`, `enqs` e `convert_units`; questa interpretazione
   meteorologica è responsabilità dell'applicazione, non della sola decodifica.
4. `src/archive.cpp` conserva i BUFR intatti per SHA-256 e importa con una
   transazione SQLite. Le revisioni collegano valore, lotto e posizione originale.
5. `src/render.cpp` legge le osservazioni selezionate, genera gnuplot senza shell
   tramite `posix_spawnp` e prepara una directory completa prima della pubblicazione.
   `scripts/update.sh` aggiorna atomicamente il solo HTML di ingresso.

Non vengono usati un convertitore BUFR esterno, JSON riconvertito in BUFR,
un server applicativo o un framework web.

## Fonte e profilo supportato

Fonte: [MeteoHub di Agenzia ItaliaMeteo](https://meteohub.agenziaitaliameteo.it/ui/user-guide),
[osservazioni al suolo](https://dati.agenziaitaliameteo.it/dataset/osservazioni-idro-meteo-stazioni-al-suolo).
Rete/dataset `simnbo` (`simnbo-emiliaromagna` nel catalogo), attribuzione
**Arpae Emilia-Romagna Idro-Meteo-Clima Service**, dati **CC BY 4.0**.
Licenza e attribuzione verificate nell'API ufficiale
[`datasets?licenceSpecs=true`](https://meteohub.agenziaitaliameteo.it/api/datasets?licenceSpecs=true).

| Nome fornito dalla fonte | Identità della stazione (rete e coordinate) | Quota suolo |
| --- | --- | --- |
| Forlì Urbana | `simnbo:44.22039,12.04182` | 51 m |
| Bertinoro | `simnbo:44.15206,12.14217` | 139 m |

Il flusso non fornisce un codice stazione numerico separato: si conserva il nome
B01019; l'identità stabile segue le stazioni fisse DB-All.e (rete e coordinate).
Non sono inventati codici WMO. Uno spostamento delle coordinate identifica una
nuova stazione, senza ricongiungere automaticamente gli storici.

Il profilo operativo è deliberatamente stretto:

- BUFR edizione 4, categoria 255, centro/sottocentro 200/0, master 0 versione 14,
  tabelle locali versione 1; sequenza espansa di **18 descrittori** verificata.
- B01194 rete; B04001–B04006 data della misura in UTC, tutti richiesti;
  B01019 nome; B05001/B06001 coordinate; B07030 quota, B07031 quota barometro.
- B07192/B07193 = `103,2000`: altezza sopra il suolo di 2000 mm (2 m).
  La quota sul mare resta distinta dall'altezza del sensore.
- B04192–B04194 = `254,0,0`: temperatura istantanea, senza aggregazione temporale.
- B12101 in K, scala 2, risoluzione 0,01 K; Celsius tramite wreport. Si conserva
  il valore nativo, senza arrotondamenti nell'archivio; la tabella mostra due decimali.
- Template/contesti diversi sono esclusi con origine e motivo nei log. Non si
  appiattiscono descrittori ripetuti in una mappa. I template a 17 descrittori
  incontrati su altre reti non fanno parte di questo profilo.

Le tabelle di `wreport-common` da sole non contengono queste definizioni locali.
In [tables/](tables/PROVENANCE.txt) sono incluse le tabelle ufficiali DB-All.e
necessarie, con provenienza, hash e licenza. La CLI le individua in `../tables`
rispetto alla directory dell'eseguibile (`build/bufrom`); `WREPORT_EXTRA_TABLES`
permette di indicare un percorso esplicito. Non vengono modificati i BUFR né
le tabelle di sistema. Il programma deve restare insieme alla directory `tables`.

`src/observations.cpp` conserva anche il limitato profilo WMO della v0.1.0 per
`build` manuale e fixture storiche; `site` operativo accetta solo il profilo
MeteoHub sopra descritto e la rete simnbo.

## Qualità, mancanti, serie e archivio

Nel campione acquisito non erano presenti attributi di qualità. Questo non
certifica le misure: nessuna regola di validazione o correzione automatica viene
applicata. Gli attributi eventualmente presenti sulla temperatura sono conservati
con codice e valore e dichiarati **non interpretati** in pagina e CSV.

I mancanti restano NULL in SQLite e vuoti nel CSV. Le serie distinguono stazione,
profilo, variabile, livello, significato temporale, unità e risoluzione. Nel vecchio
profilo un'altezza sconosciuta separa le provenienze; non prova omogeneità.
I grafici usano solo punti, senza interpolazione, aggregazioni o linee sui buchi.
La pagina riporta ultimo aggiornamento riuscito, ultima misura per stazione e
avvisa quando i dati, alla generazione, hanno oltre tre ore. Le date restano
sempre leggibili anche quando il servizio non si aggiorna.

Schema SQLite versione 1:

- `batches`: SHA-256, origine, dataset, acquisizione UTC, percorso del BUFR originale;
- `revisions`: osservazione e contesto completo, qualità, hash e indici nel BUFR;
- `current_observations`: collegamento alla revisione attuale di ogni chiave.

La chiave comprende profilo, rete/stazione, variabile, istante UTC, livello e tempo;
i componenti sono delimitati per lunghezza, evitando collisioni e NULL SQL nella
chiave. Lo stesso BUFR non viene reimportato, anche se rinominato o ripresentato
dopo una rettifica. Un valore identico da un lotto diverso è un duplicato.
Un valore diverso da un **nuovo lotto acquisito successivamente** crea una revisione;
il precedente resta rintracciabile. Acquisizioni discordanti a pari data o fuori
ordine e conflitti nello stesso lotto fanno fallire l'intera transazione.
La data di acquisizione non dimostra l'autorità di una rettifica: è la politica
esplicita di questo rilascio, non una ricostruzione della sequenza editoriale fonte.

Il servizio rilegge le ultime **30 ore**, configurabili fino a 216 ore entro il
limite anonimo degli ultimi 10 giorni, suddivise per giorno UTC. Recupera ritardi
e rettifiche solo entro quella finestra; nessuna replica completa delle correzioni
più vecchie è garantita. Lo storico locale e i BUFR non vengono cancellati.
Un'assenza di nuove misure non svuota l'archivio; un lotto senza osservazioni
interpretabili blocca l'aggiornamento e conserva il sito precedente.

## Esecuzione

Copiare [config/bufrom.conf.example](config/bufrom.conf.example) in un percorso
privato esterno al repository e alla document root, adattare i percorsi e dare
permessi `0600`. Non occorre un account MeteoHub per questo profilo pubblico.
`STATIONS` seleziona le due identità reali, `OVERLAP_HOURS` la finestra riletta,
`DISPLAY_HOURS` le ultime ore mostrate (48 per impostazione iniziale).

```sh
./scripts/update.sh /percorso/privato/bufrom.conf
./scripts/rollback.sh /percorso/privato/bufrom.conf
```

`PUBLIC_DIR` deve essere una directory dedicata vuota o già inizializzata da
BufRom; il programma rifiuta di sostituire un sito estraneo. Lock `flock`, timeout
globale di 15 minuti, timeout HTTP e al massimo due retry. Database, raw, ricevute,
log e stato restano in `STATE_DIR` privata. Un processo concorrente termina con 75.

Le release complete sono in `PUBLIC_DIR/releases/ID`. L'HTML principale referenzia
SVG e CSV tramite URL con ID della release; il cambio di `index.html` è atomico.
Le release vengono conservate, inclusa la precedente. Il loro `index.html` è una
copia per il ripristino **alla radice** del sito, non un ingresso da aprire dentro
`releases/ID`. Una generazione fallita lascia intatto l'HTML pubblico precedente.
Atomicità del cambio HTML non equivale a durabilità dopo perdita di alimentazione.

[OPERATIONS.md](OPERATIONS.md) descrive scheduler, permessi, log, backup e ripristino.
I file systemd sono esempi: non è stato installato o attivato alcun timer sul server.

## Comandi del nucleo

```sh
./build/bufrom inspect --details tests/data/meteohub-simnbo.bufr
./build/bufrom export --output variabili.csv tests/data/meteohub-simnbo.bufr
./build/bufrom verify tests/data/*.bufr
./build/bufrom import --database /dati/privati/observations.sqlite \
    --raw-dir /dati/privati/raw --origin URL_FONTE --dataset simnbo FILE.bufr
./build/bufrom site --database /dati/privati/observations.sqlite \
    --output nuova-directory --repository https://github.com/fmolara/bufrom
./build/bufrom build --output esempio-storico \
    --station 16148 --station 16149 tests/data/synop3new.bufr
```

`export` conserva indici/attributi e valori indefiniti, sovrascrive l'eventuale CSV
ma rifiuta di sovrascrivere l'input. `verify` usa `encode` e `Bulletin::diff`:
verifica semantica con la stessa libreria, non conformità WMO indipendente.
`build` e `site` rifiutano una directory di output già esistente.
`examples/site` resta l'esempio storico originale del **15 agosto 2010**.

## Licenze e limiti

Codice BufRom: **GPL-2.0-or-later**, [COPYING](COPYING).
Tabelle DB-All.e: GPL versione 2, avvisi e origine in `tables/`.
Fixture upstream wreport: avvisi e attribuzioni in `tests/data/PROVENANCE.txt`.
Fixture MeteoHub e osservazioni: **CC BY 4.0**, attribuzioni separate in
`tests/data/PROVENANCE.meteohub.txt`. La licenza del codice non sostituisce
quella dei dati.

Restano da verificare build, aggiornamento completo, Apache/HTTPS e prima
esecuzione pianificata sul sistema di produzione. Il programma conserva in
memoria piccoli lotti, non interpreta BUFR arbitrari e non garantisce repliche
complete della fonte. Altre variabili, reti, autenticazione MeteoHub e nuovi
profili sono sviluppi futuri. Non sono inclusi CREX, scrittura di nuovi bollettini,
binding Python, pacchetto Debian, interfaccia amministrativa o Raspberry.
`VALIDATION.txt` e `NOTES-wreport.md` descrivono separatamente la verifica storica
v0.1.0 su Ubuntu; non costituiscono prove della produzione attuale.
