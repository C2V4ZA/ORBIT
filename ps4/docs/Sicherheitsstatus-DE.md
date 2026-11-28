# ORBIT Sicherheitsstatus (PS4 Test 9 / PS3 1.3.0 Test 1)

Ehrlich und vollständig: was **fertig** ist, was **teilweise** da ist und was **noch fehlt**.
„Fertig“ heißt: eingebaut und **am PC** automatisch getestet. Auf einer Konsole ist noch nichts
davon geprüft, und niemand außerhalb des Projekts hat den Code begutachtet.
Technische Details: `docs/next/THREAT-MODEL.md` und `docs/next/PRIVACY-SHIELD.md` im PS3-Projekt.

## Neu in Test 8 – fertig
- **Privacy Shield** (Einstellungen → Datenschutz, standardmäßig **an**):
  - Keine Konsole verbindet sich mehr direkt mit deiner IP-Adresse und deine Konsole sich mit
    keiner anderen. Alles läuft über den Orbit-Tunnel (bore.pub) oder einen Orbit-Vermittler.
  - Gilt auch für Sprachchat und Server.
  - Deine Konsole nimmt nur noch Verbindungen an, die über den eigenen Tunnel/Vermittler kommen.
    Direkte Versuche werden abgewiesen, Heimnetz-Suchanfragen nicht beantwortet, und am Router
    wird kein Port geöffnet.
  - Einladungs- und Freundescodes enthalten nie deine eigene IP-Adresse.
  - **Kein heimliches Zurückfallen:** Ist kein Tunnel/Vermittler erreichbar, verbindet ORBIT nicht
    und zeigt den Grund. Nur du kannst Shield ausschalten.
  - Ein alter Code, der direkt auf die IP einer Konsole zeigt, wird abgelehnt (mit Hinweis).
  - Spiel-Cover: ORBIT zeigt immer die Cover, die schon auf der Konsole sind. Offizielle Cover von
    GameTDB werden nur geladen, wenn du beim ersten Start „Ja“ sagst; die Frage erklärt, dass
    GameTDB dabei deine IP-Adresse sieht. Das ist unabhängig vom Shield.
- **Forward Secrecy bei der Verbindung:** Zwei Konsolen mit 1.3 verschlüsseln jetzt mit einem
  frischen Schlüssel pro Verbindung (ECDHE-PSK, X25519, ChaCha20-Poly1305). Wer später einen
  Servercode erfährt, kann alte mitgeschnittene Verbindungen nicht entschlüsseln.
  Ältere ORBIT-Versionen verbinden sich weiter (dann wie bisher ohne Forward Secrecy).
- **Vermittler-Server (zum Selbst-Betreiben):** schreibt standardmäßig **keine IP-Adressen** mehr in
  sein Protokoll.
- **Bild-Metadaten:** Ein Test beweist jetzt, dass GPS-/EXIF-Daten aus Fotos und Kommentare in GIFs
  nie im Profilbild landen, das andere bekommen (ORBIT baut Bilder nur aus den Pixeln neu).
- **Fuzzing:** über 900 000 kaputte Eingaben gegen 11 Angriffsflächen (Anfragen an den Gastgeber,
  Einladungscodes, JPEG/PNG/GIF, Profilbilder, Updates, Verzeichnis, verschlüsselte Nachrichten,
  Müll-Daten an den Netzwerk-Port), dazu 500 000 kaputte DNS-Antworten auf der PS4. Geprüft mit
  AddressSanitizer + UBSan: **kein Absturz, kein Speicherfehler gefunden.** Das ist ein gutes Zeichen,
  aber kein Beweis, dass es keine Fehler gibt.

## War schon vorher da – fertig
- Keine Hardware-IDs, keine Seriennummer, keine MAC-Adresse (im Code geprüft: wird nirgends gelesen).
- Keine Standortdaten, keine versteckte Telemetrie (alle Internetverbindungen sind in
  THREAT-MODEL.md aufgelistet).
- Signierte Updates (Ed25519 + SHA-256).
- Strenge Bild-/GIF-Leser mit Größen- und Zeitgrenzen.
- Keine selbst erfundenen Verschlüsselungsverfahren (Monocypher, mbedTLS).

## Teilweise
- Identität: Schlüssel pro Konsole + Signatur je Gastgeber; aber noch keine Sicherheitsnummern
  zum Vergleichen in der Oberfläche.
- Schutz gegen Spam: 1 Nachricht pro 300 ms, max. 32 Verbindungen pro Gastgeber, Vermittler max. 64
  pro IP. Für andere Aktionen gibt es noch kein Limit.
- Prüfung aller Netzwerkdaten, Pfade und Fehlerbehandlung: geprüft, wo wir es angesehen und
  gefuzzt haben, aber nicht vollständig begutachtet.
- Schlüsselverwaltung: zufällige Schlüssel, aber die Schlüsseldateien liegen unverschlüsselt.

## Fehlt noch (nicht behaupten, dass es da ist!)
- **Ende-zu-Ende-Verschlüsselung für Direktnachrichten:** Der Kern ist gebaut und getestet,
  aber **noch nicht eingebaut**. Heute kann der Gastgeber Nachrichten lesen.
- **Ende-zu-Ende-Verschlüsselung für Sprache:** fehlt. Der Gastgeber könnte Sprache mithören.
- **Verschlüsselter Nachrichtenverlauf auf der Konsole:** fehlt (`data.json` ist lesbar).
- **DDoS-Schutz auf Serverseite:** fehlt; das braucht Schutz beim Hoster des Vermittlers.

## Was Privacy Shield nicht verstecken kann
- Der Tunnel-Server (bore.pub, ein fremder Gratisdienst) bzw. der Betreiber des Vermittlers sieht
  die IP-Adressen beider Seiten, aber keine Inhalte. Wer das nicht möchte, betreibt einen eigenen
  Vermittler.
