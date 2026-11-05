# Gestione di BufRom

Stato attuale: esecuzione live verificata in sviluppo e primo aggiornamento manuale
riuscito su `quixote.molara.net`, con anteprima privata e 236 osservazioni nuove
alle 2026-11-05T21:21:36Z (output confermato da Federico).
Sito pubblico: `https://www.molara.net/bufrom/`, verificato dall’esterno con
HTML, due SVG, CSV, redirect e link GitHub. La release pubblicata alle
2026-11-05T21:36:05Z mostra 238 osservazioni delle due stazioni; la precedente
con 236 osservazioni è ancora disponibile. La pagina iniziale del sito è invariata.
La reimportazione ha riportato 472 duplicati, nessuna osservazione nuova o
rettificata. Una prova di download fallito ha conservato esattamente HTML e
ultimo successo, verificati con SHA-256 e con una nuova lettura HTTPS.
Timer di sistema installato e prima esecuzione automatica accelerata riuscita
alle 2026-11-05T21:50:33Z: Result=success, ExecMainStatus=0. Override di prova
rimosso; calendario orario attivo, prossimo turno mostrato alle 23:03 CET
(22:03 UTC). I successivi turni orari restano da osservare.
Il VirtualHost HTTPS è ora configurato con `molara-net.conf`, indipendente
da WordPress. Il ripristino è stato verificato dall’esterno sulla release generata
dal timer: pagina, due SVG, CSV e redirect corretti; pagina iniziale invariata.
Non serve lasciare una sessione SSH aperta dopo l'installazione del servizio.

## Preparare il server

Come utente del servizio, raccogliere prima queste informazioni senza privilegi:

```sh
hostname
cat /etc/os-release
command -v gnuplot curl jq flock timeout
dpkg --print-architecture
dpkg-query -W libwreport3t64 wreport-common libsqlite3-0 libssl3t64 libstdc++6
ls -ld /var/www /srv/www
```

Leggere la configurazione Apache esistente, se accessibile, per scegliere una
sottodirectory o un Alias dedicati. Non sostituire il sito www.molara.net.
Dipendenze da installare come root se mancanti: quelle indicate nel README.
Sul server non si presume disponibile `sudo`: l'amministratore può usare il
proprio accesso root. In questa sessione tutti i comandi root richiedono prima
l'autorizzazione di Federico.

Scegliere separatamente:

- directory del programma installato, con `build/bufrom` e `tables/`;
- configurazione `0600` e `STATE_DIR` privata `0700`, fuori dalla document root;
- `PUBLIC_DIR` dedicata, leggibile da Apache (`0755` anche lungo il percorso);
- URL HTTPS reale corrispondente, da verificare dopo il primo aggiornamento.

Per un server di sola produzione seguire la sezione
[Installazione senza compilazione](README.it.md#installazione-in-produzione-su-debian-senza-compilazione):
installare un archivio binario compatibile, verificare le librerie con `ldd` e
copiare/adattare la configurazione di esempio. Build e test vengono eseguiti sulla
macchina di sviluppo; sul server si verifica il primo aggiornamento completo.
Non copiare il database di test o fixture sintetiche nello stato operativo.
Per questa fonte pubblica non ci sono credenziali. Eventuali futuri segreti
restano in file privati, mai in argomenti, repository o log.

## Utente di servizio wreport

Per quixote si prevede un utente di sistema `wreport`, con gruppo omonimo,
senza password e con shell `/usr/sbin/nologin`. Il servizio viene avviato dal
timer systemd di sistema: non occorrono login SSH con questa utenza, privilegi
amministrativi o lingering. Federico ha confermato su quixote la creazione
dell’utenza, home e shell previste, e le directory private `wreport:wreport` con
permessi `0700`. Il binario della prerelease v0.2.0-dev è stato installato sotto
`/opt/bufrom/current`, avviato come `wreport` e verificato con `ldd`. Il primo
aggiornamento completo ha generato l’anteprima in `/var/lib/wreport/site-preview`.
Il primo avvio automatico del servizio tramite timer è verificato (prova accelerata).

Per creare l'utenza e le directory private, l'amministratore esegue questi comandi
come root su quixote, dopo aver verificato che il nome non sia già utilizzato
(`getent passwd wreport` e `getent group wreport`):

```sh
adduser --system --group --home /var/lib/wreport \
    --shell /usr/sbin/nologin wreport
install -d -o wreport -g wreport -m 0700 \
    /var/lib/wreport /var/lib/wreport/bufrom
id wreport
```

Se l'utenza esiste già, esaminarne configurazione e utilizzo prima di riutilizzarla;
questi comandi sono per la prima creazione. Nell'ambiente di questa sessione
l'accesso SSH a quixote non è ancora configurato: i comandi vanno eseguiti nella
sessione root di Federico, oppure dopo aver predisposto l'accesso e autorizzato
l'esecuzione come root.

I percorsi proposti negli esempi sono:

| Percorso | Uso e permessi |
| --- | --- |
| `/opt/bufrom/current` | Collegamento alla versione binaria installata; programma e tabelle gestiti da root, leggibili ed eseguibili da `wreport` |
| `/etc/bufrom/bufrom.conf` | Configurazione `root:wreport`, modo `0640`, dentro una directory `root:wreport` con modo `0750` |
| `/var/lib/wreport/bufrom` | `STATE_DIR`, proprietà `wreport:wreport`, modo `0700`; database, raw, ricevute e log |
| `/var/www/bufrom` | `PUBLIC_DIR` dedicata, scrivibile da `wreport` e leggibile da Apache; solo HTML, CSV e grafici |

Il percorso pubblico `/var/www/bufrom` è collegato ad Apache e verificato
all’URL `https://www.molara.net/bufrom/`. Apache non deve accedere alla directory
privata e l'utente `wreport` non deve possedere o poter modificare il programma.

Dopo l'installazione del pacchetto binario e della configurazione, per il primo
aggiornamento manuale l'amministratore può usare, sempre dalla sessione root:

```sh
cd /var/lib/wreport
runuser -u wreport -- /opt/bufrom/current/scripts/update.sh /etc/bufrom/bufrom.conf
```

Il comando esegue la pipeline come `wreport` anche con la shell `nologin`; non
richiede sudo. Il `cd` evita di ereditare `/root`, che `wreport` non può
attraversare: nella prima esecuzione questo provocava un avviso di GNU find,
senza impedire l’aggiornamento. Lo script nel ramo main ora imposta direttamente
la directory di lavoro privata; la prerelease v0.2.0-dev già installata richiede
ancora il `cd` nel comando manuale. L’unità di esempio è predisposta con
`User=wreport`, `Group=wreport` e `WorkingDirectory=/var/lib/wreport`.

## Aggiornamento e controlli

```sh
/percorso/bufrom/scripts/update.sh /percorso/privato/bufrom.conf
cat /percorso/stato/last-attempt.json
cat /percorso/stato/last-success.json
tail -n 40 /percorso/stato/update.log
```

`last-attempt.json` registra anche i fallimenti; `last-success.json` viene
sostituito solo dopo il cambio dell'HTML. I conteggi di nuove, rettificate,
duplicate ed escluse sono nel log e nello stato dell'ultimo successo.
Perdita di rete, HTTP inatteso, BUFR inutilizzabile e gnuplot fallito conservano
il sito precedente. L'import può essere già riuscito quando fallisce il rendering:
i dati restano archiviati e il tentativo successivo li ritrova.
Un'interruzione durante copia del raw può lasciare un file temporaneo nascosto;
una transazione annullata può lasciare un BUFR originale senza riferimento DB.
Non eliminare automaticamente nessuno dei due: non compromettono le misure attuali.

Controllare HTTPS dall'esterno: HTML, tutti gli SVG, CSV, date delle misure e
link GitHub. Un controllo locale non dimostra che Apache serva la nuova release.
Controllare che il database, i raw e la configurazione non siano sotto il percorso
pubblico. I log possono includere percorsi locali e restano privati.

## Aggiungere stazioni

La configurazione di esempio comprende Forlì Urbana, Bertinoro e le otto
aggiunte scelte: Novafeltria, Ponte Uso, Premilcuore, Ravenna urbana, Rimini Ausa,
Rocca San Casciano Meteo, Tredozio e Verghereto. Il collaudo live locale delle
dieci stazioni è riuscito con dieci grafici SVG e CSV; l'applicazione della
nuova selezione a quixote richiede la modifica di `/etc/bufrom/bufrom.conf`.

Prima della modifica salvare una copia privata della configurazione, conservando
proprietà e permessi. Aggiornare `STATIONS` con le identità riportate nel README
e ampliare l'area di download a `LAT_MIN=43.78`, `LAT_MAX=44.43`, `LON_MIN=11.73`,
`LON_MAX=12.56`. Conservare gli altri parametri del server. Controllare con
`bash -n /etc/bufrom/bufrom.conf` e avviare l'aggiornamento come `wreport`, usando
il comando manuale sopra. La selezione non richiede un nuovo binario o una
ricarica di Apache; il timer leggerà la configurazione al prossimo avvio.

L'area comprende anche stazioni non selezionate e alcuni template non supportati:
il conteggio `excluded` può quindi essere maggiore di zero. Le osservazioni
escluse non vengono aggiunte alle serie. Verificare dopo l'aggiornamento che
pagina, grafici e CSV contengano le dieci stazioni richieste.

Nel catalogo MeteoHub verificato non risulta una stazione denominata Cesena.
Martorano, nel comune di Cesena, fornisce temperature BUFR ma appartiene alla
rete `locali`, non supportata dalla pipeline operativa attuale. Per scelta di
Federico questa stazione resta fuori dall'estensione.

## Pubblicazione Apache su quixote

URL scelto: `https://www.molara.net/bufrom/`, output in `/var/www/bufrom`.
Nel VirtualHost HTTPS fornito da Federico non ci sono regole Rewrite; gli alias
WordPress sono residui di un pacchetto già rimosso. L'intervento BufRom aggiunge
soltanto il proprio collegamento, conservando le altre direttive del server.

Il frammento [apache/bufrom-https.conf.example](apache/bufrom-https.conf.example)
usa un [Alias Apache](https://httpd.apache.org/docs/2.4/mod/mod_alias.html#alias)
per servire la directory generata, con `index.html`, nessun elenco di directory
e nessun `.htaccess`. Va incluso **dentro il solo VirtualHost HTTPS** di
`www.molara.net`, non abilitato globalmente con `a2enconf`.
La prerelease originaria v0.2.0-dev non contiene ancora il frammento; è disponibile
nel repository e sarà incluso nei nuovi archivi di esecuzione.

Le seguenti operazioni sono riservate all'amministratore, previa autorizzazione
di Federico. La prima pubblicazione su quixote è stata eseguita e verificata
seguendo questa procedura:

1. Creare `/var/www/bufrom` soltanto se il percorso è libero, con proprietario
   `wreport:wreport` e modo `0755`. Copiarvi con `cp -a` il contenuto di
   `/var/lib/wreport/site-preview/.`: sono i soli file statici già generati.
   Si preservano così release e asset precedenti, necessari al ripristino.
2. Salvare una copia della configurazione privata sotto `/etc/bufrom`, cambiare
   `PUBLIC_DIR` in `/var/www/bufrom`, poi eseguire un aggiornamento come `wreport`
   partendo da `/var/lib/wreport`. Database, raw e ricevute rimangono nella stessa
   `STATE_DIR`. Verificare che `www-data` possa leggere `index.html`.
3. Installare il frammento come `/etc/apache2/bufrom-https.conf`, proprietà root,
   modo `0644`. Risolvere con `readlink -f` il percorso di
   `/etc/apache2/sites-enabled/wp-molara-net.conf`, farne un backup e aggiungere
   subito dopo `<VirtualHost *:443>` questa sola riga:

   ```apache
   Include /etc/apache2/bufrom-https.conf
   ```

4. Eseguire `apache2ctl configtest`; ricaricare con `systemctl reload apache2`
   soltanto con esito positivo. Se il test fallisce, ripristinare il backup del
   VirtualHost; il server in esecuzione continua a usare la configurazione precedente.
5. Controllare dall'esterno HTML, SVG, CSV, link GitHub, date e redirect
   `/bufrom` → `/bufrom/`; controllare anche che la pagina iniziale del sito
   continui a rispondere. Solo questa verifica conferma la pubblicazione HTTPS.

Il frammento è stato provato con Apache 2.4.69 ufficiale Debian, estratto in un
prefisso privato e avviato senza root su loopback: sintassi, pagina, SVG, CSV,
redirect della barra finale, divieto di listing e isolamento fra VirtualHost
verificati. Anche il successivo controllo HTTPS su quixote è riuscito: 238 righe dati
nel CSV, due SVG XML validi, vecchi asset raggiungibili e sito principale invariato.

## Ripristino del VirtualHost HTTPS del dominio

Dopo il primo aggiornamento automatico riuscito, la verifica HTTPS ha trovato
soltanto `000-default.conf` abilitato sulla porta 80. Il sito
`wp-molara-net.conf` non era più presente in `sites-enabled`: Apache continuava
ad ascoltare sulla porta 443 senza il VirtualHost TLS del dominio. Il problema
è stato riprodotto sia dall’esterno sia con curl verso `127.0.0.1` sul server.

La configurazione [apache/molara-net.conf.example](apache/molara-net.conf.example)
ricrea il VirtualHost del dominio indipendentemente da WordPress. Mantiene:

- redirect HTTP → HTTPS verso www.molara.net;
- ServerName e ServerAlias già forniti da Federico;
- DocumentRoot `/var/www/html`, equivalente a quello prima ereditato;
- gli stessi file di certificato, opzioni TLS e log;
- l’inclusione del frammento `/etc/apache2/bufrom-https.conf` già installato.

L’amministratore deve installarla come
`/etc/apache2/sites-available/molara-net.conf` soltanto se il nome non è già usato,
abilitarla con `a2ensite molara-net.conf`, eseguire `apache2ctl configtest` e
ricaricare Apache soltanto con esito positivo. In caso di test fallito,
disabilitare soltanto il nuovo sito. Il file WordPress eventualmente rimasto
in `sites-available` e il sito predefinito non vengono modificati.

Il nuovo VirtualHost è stato verificato con Apache locale senza root, usando
un certificato effimero di test: handshake TLS con verifica del certificato e
del nome, pagina iniziale, `/bufrom/` e redirect HTTP corretti. I certificati reali
restano sul server. Federico ha installato e abilitato `molara-net.conf`;
`configtest` è riuscito e il VirtualHost risulta caricato sulla porta 443.
L’agente ha poi verificato dall’esterno handshake HTTPS, pagina iniziale,
`/bufrom/`, due SVG, CSV, redirect e accesso alla release precedente.
La pagina iniziale ha lo stesso contenuto acquisito prima delle modifiche.

## Pianificazione

Preferenza iniziale: **timer systemd di sistema**. Adattare il file
`systemd/bufrom.service.example` indicando utente e percorsi reali, installarlo
come `bufrom.service` insieme a `systemd/bufrom.timer` nelle unità di sistema.
Queste operazioni richiedono root e l’autorizzazione di Federico. Su quixote
il servizio e il timer sono stati installati, verificati e attivati da Federico:

```sh
systemd-analyze verify /etc/systemd/system/bufrom.service /etc/systemd/system/bufrom.timer
systemctl daemon-reload
systemctl enable --now bufrom.timer
systemctl list-timers bufrom.timer
journalctl -u bufrom.service -n 40
```

Il timer è orario, con ritardo casuale fino a cinque minuti e recupero dei turni
saltati. Il servizio ha un timeout di 15 minuti, così come il comando manuale.
Usare un solo scheduler: nessun cron aggiuntivo. Un timer utente richiederebbe
anche verifica del lingering (disabilitato sulla macchina di sviluppo).

Prima del rilascio verificare una vera attivazione da timer. Il file
[systemd/bufrom-first-run.conf.example](systemd/bufrom-first-run.conf.example)
è un override temporaneo da installare come
`/etc/systemd/system/bufrom.timer.d/first-run.conf`: azzera `OnCalendar`, imposta
`OnActiveSec=30s`, `AccuracySec=1s`, `RandomizedDelaySec=0` e `Persistent=false`.
Queste opzioni sono documentate in
[systemd.timer](https://www.freedesktop.org/software/systemd/man/latest/systemd.timer.html).
Avviare il timer, attendere circa 35 secondi e controllare journal, risultato del
servizio e cambiamento della release in `last-success.json`. Se l'aggiornamento
è ancora in corso, attendere il completamento prima di considerare passata la prova.

Soltanto dopo un'esecuzione riuscita rimuovere il solo file `first-run.conf`,
fare `systemctl daemon-reload`, riavviare `bufrom.timer` e verificare il calendario
orario con `systemctl list-timers`. Non usare `systemctl revert`, che potrebbe
rimuovere altri override. L’unità base mantiene la frequenza oraria e il recupero
dei turni saltati. Il servizio viene eseguito come `wreport`, dalla sua home privata. L'avvio manuale del servizio non prova il funzionamento dello scheduler;
la sola presenza di un timer attivo non prova aggiornamenti automatici riusciti.

## Ripristino, backup e aggiornamenti

```sh
/percorso/bufrom/scripts/rollback.sh /percorso/privato/bufrom.conf
```

Il comando usa lo stesso lock e ripristina alla radice l'HTML della release
precedente registrata in `last-success.json`. Non modifica database o raw.
Le release sono mantenute tutte in questa prima versione. I vecchi asset devono
restare disponibili anche ai lettori con HTML già caricato; non cancellarli durante
un cambio release. Le copie HTML nelle release usano URL relativi alla radice.

Per backup coerenti usare SQLite `.backup` e copiare **anche** raw e ricevute;
conservare la configurazione privata separatamente. Non copiare soltanto il file
SQLite mentre è in scrittura. Prima di aggiornare il programma fermare il timer,
attendere il termine dell'eventuale servizio e fare il backup. In produzione
installare il nuovo archivio binario in una directory distinta e aggiornare
`PROGRAM` nella configurazione e `ExecStart` nell'unità systemd; fare rileggere
le unità con `systemctl daemon-reload` come root. Chi usa invece un checkout
di sviluppo aggiorna i sorgenti, compila e fa `make check`.
Eseguire un aggiornamento completo e riattivare il timer soltanto dopo averlo
verificato. Lo schema attuale è versione 1; non ci sono
migrazioni o cancellazioni automatiche dello storico.
