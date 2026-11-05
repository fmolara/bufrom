# Gestione di BufRom

Stato attuale: esecuzione live verificata sulla macchina di sviluppo Debian 13;
server scelto `quixote.molara.net`, sito esistente `https://www.molara.net/`.
Percorso e URL del diario non ancora assegnati. SSH e predisposizione del server
restano da completare; nessuna unità systemd è stata installata o attivata.
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
amministrativi o lingering. La creazione sul server non è ancora stata verificata.

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

Il percorso pubblico è una proposta: va confermato confrontandolo con la
configurazione Apache esistente prima di crearne la directory o il relativo Alias.
L'URL HTTPS del diario resta da assegnare. Apache non deve accedere alla directory
privata e l'utente `wreport` non deve possedere o poter modificare il programma.

Dopo l'installazione del pacchetto binario e della configurazione, per il primo
aggiornamento manuale l'amministratore può usare, sempre dalla sessione root:

```sh
runuser -u wreport -- /opt/bufrom/current/scripts/update.sh /etc/bufrom/bufrom.conf
```

Il comando esegue la pipeline come `wreport` anche con la shell `nologin`; non
richiede sudo. L'unità di esempio è predisposta con `User=wreport` e `Group=wreport`.

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
