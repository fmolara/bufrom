# Note dall'uso diretto di wreport 3.36

Questi comportamenti sono emersi compilando ed eseguendo il prototipo contro
libwreport 3.36, pacchetti Ubuntu `3.36-1.1build1`. I riferimenti al sorgente
Debian sono fissati alla versione `3.36-1.1`. Non sono affermazioni su tutte le
versioni della libreria e non sono state aperte segnalazioni upstream.

## Conversione Kelvin–Celsius

[`wreport/conv.cc`](https://sources.debian.org/src/wreport/3.36-1.1/wreport/conv.cc/)
registra la conversione con:

```cpp
add_linear("K", "C", 1, -273.15001);
```

La chiamata reale `convert_units("K", "C", 289.2)` restituisce circa
`16.04999`, mentre la conversione teorica con `273.15` dà `16.05`. Il sorgente
consultato non spiega il motivo storico di questo scarto.

Il progetto conserva la conversione della libreria. I test controllano
separatamente il valore decodificato in kelvin e usano una tolleranza di
`1e-4 °C` per il risultato in Celsius: è mille volte più piccola del passo
`0.1 K` del descrittore nei campioni. Il CSV conserva il valore calcolato; la
tabella HTML lo presenta con due decimali. La precisione della misura rimane
quella originaria.

## Assegnazione di `Var` fra descrittori differenti

Nel test che sostituisce B12004 con B12101, l'assegnazione da una temporanea:

```cpp
temperature = wreport::Var(generic_info, 289.2);
```

ha conservato il vecchio `Varinfo` mentre trasferiva il nuovo valore scalato.
Il risultato decodificato era quindi dieci volte maggiore, dato che i due
descrittori hanno scale differenti.

Il confronto con
[`wreport/var.cc`](https://sources.debian.org/src/wreport/3.36-1.1/wreport/var.cc/)
mostra che in questa versione l'operatore di move-assignment non assegna
`m_info`, mentre quello di copy-assignment lo assegna. Il test ora costruisce
un `const Var` intermedio, usa il copy-assignment e verifica esplicitamente sia
il codice del descrittore sia il valore in kelvin prima dell'estrazione.

Questo caso è un candidato a una verifica specifica con upstream, confrontando
il comportamento atteso e la versione corrente. Non è stato aggirato alterando
l'estrattore: il programma applicativo legge le variabili decodificate e non
esegue questa sostituzione. Il caso modificato resta confinato al test.

## Perché conservare queste note

Sono esempi del contributo che un utilizzatore concreto può dare: piccoli casi
riproducibili, con versione e comportamento osservato. Prima di proporre una
patch alla libreria servirà confrontare il codice corrente e discutere le
aspettative dell'API. I test del diario forniscono già il contesto applicativo
da cui partire.
