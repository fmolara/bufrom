# Gestione di BufRom

Stato attuale: esecuzione live verificata in sviluppo e primo aggiornamento manuale
riuscito su `quixote.molara.net`, con anteprima privata e 236 osservazioni nuove
alle 2026-11-05T21:21:36Z (output confermato da Federico).
URL scelto: `https://www.molara.net/bufrom/`; collegamento ad Apache ancora da
configurare e verificare. Nessuna unità systemd è stata installata o attivata.
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
[Installazione senza compilazione](README.md#installazione-in-produzione-su-debian-senza-compilazione):
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
L’avvio del servizio tramite scheduler resta da verificare.

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

Il percorso pubblico scelto per il collegamento Apache è `/var/www/bufrom`;
la sua creazione e l’inclusione dell’Alias restano da eseguire.
L’URL scelto è `https://www.molara.net/bufrom/`, ancora da collegare e verificare. Apache non deve accedere alla directory
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
di Federico; non sono ancora state eseguite su quixote:

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
verificati. Questo controllo locale non sostituisce il test HTTPS su quixote.

## Pianificazione

Preferenza iniziale: **timer systemd di sistema**. Adattare il file
`systemd/bufrom.service.example` indicando utente e percorsi reali, installarlo
come `bufrom.service` insieme a `systemd/bufrom.timer` nelle unità di sistema.
Queste operazioni richiedono root e l'autorizzazione di Federico; nessun comando
seguente è stato eseguito sul server:

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

Prima del rilascio verificare una vera attivazione da timer: un amministratore
può installare temporaneamente un override del solo timer con `OnActiveSec=30s`
e `RandomizedDelaySec=0`, riavviarlo, attendere la breve scadenza e controllare
journal e ultimo successo. Rimuovere poi quell'override e verificare il calendario
orario. L'avvio manuale del servizio non prova il funzionamento dello scheduler;
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
