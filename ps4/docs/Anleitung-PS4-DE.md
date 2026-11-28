# ORBIT für PS4 – Test 12 (Version 1.3.0)

**Neu in Test 12: Nachrichten im Hintergrund, während du spielst (experimentell)**
- Neues **GoldHEN-Plugin** `orbit_notify.prx`: Schreibt dir jemand, während ein Spiel läuft,
  erscheint oben eine PS4-Meldung („ORBIT · Neue Nachricht …“). Gleiche Logik wie bei der PS3.
- **Voraussetzung:** GoldHEN **2.3 oder neuer**, und in GoldHEN → Einstellungen → Plugins
  muss **„Plugins laden“ (Load plugins)** an sein.
- **Einschalten:** ORBIT → Einstellungen → Benachrichtigungen → Abschnitt „Im Hintergrund (in
  Spielen)“ → **„Hintergrund-Plugin installieren“**. „Neue Nachrichten oben rechts zeigen“ muss an
  sein. Aktiv ab dem **nächsten Spielstart**.
- **Was ORBIT dabei ändert (nur auf deinen Knopfdruck):**
  - kopiert `/app0/assets/orbit_notify.prx` nach `/data/GoldHEN/plugins/orbit_notify.prx`
  - fügt **eine** Zeile unter `[default]` in `/data/GoldHEN/plugins.ini` ein (andere Plugins und
    Spiel-Abschnitte bleiben genau so). Vorher wird einmal eine Kopie
    `plugins.ini.orbit-backup` angelegt.
  - **Entfernen** löscht genau diese Zeile und die Datei wieder.
- Ablauf im Spiel: Das Plugin wartet nach dem Spielstart ~15 Sekunden, und solange ORBIT noch
  offen war, bleibt es noch ~30 Sekunden still. Danach kommen Meldungen.
- **Ehrlich:** Am PC sind Plugin-Logik und das Ändern der plugins.ini getestet. **Auf einer echten
  PS4 wurde das Plugin noch nie geladen.** Es ist möglich, dass es nicht lädt oder im Spiel keine
  Verbindung bekommt. Jeder Schritt wird in **`/data/orbit/plugin-log.txt`** geschrieben – schick
  mir den Inhalt (per FTP), dann sehe ich, wo es hängt.
- Falls ein Spiel damit Probleme macht: in ORBIT „Hintergrund-Plugin entfernen“ oder in GoldHEN
  „Plugins laden“ ausschalten.

**Neu in Test 12: Töne (du hattest in Test 11 keine gehört)**
- ORBIT öffnet den Tonausgang jetzt so, wie andere PS4-Homebrew es tun (System-Benutzer), und
  probiert sonst drei andere Varianten (eigener Benutzer, 16-Bit-Ton).
- Unter **Einstellungen → Audio** steht jetzt z. B. „Audio out: port 3, system user, float“ oder
  „Audio out failed: …“ mit Fehlercodes. Bitte schick mir diese Zeile, falls es still bleibt.
- Prüfe auch Einstellungen → Benachrichtigungen → Töne: nicht auf „Aus“.
- Ehrlich: Ich konnte das ohne PS4 nicht prüfen; der Grund für die Stille ist eine Vermutung.

**Aus Test 11: große Fehlerprüfung** (Details: `Pruefbericht-1.3.0-DE.md`)
- **Sprachchat:** Fehler behoben, durch den die Stimme je nach Compiler zu Rauschen werden konnte
  (wichtig für PS3 ↔ PS4).
- Mehrere Thread-Fehler behoben (Daten, die zwei Programmteile gleichzeitig benutzten).
- Alles am PC geprüft: 105 Tests (auch mit Speicherfehler-Suche), Fuzzing ohne Absturz.

**Aus Test 10: Entdecken findet PS3- und PS4-Server**
- „Entdecken“ zeigt öffentliche Server von **PS3 und PS4** gemischt. Jede Karte hat ein kleines
  **PS3**- oder **PS4**-Schild (wo der Server läuft). Auch in den Details steht „· PS3“ bzw. „· PS4“.
- Beitreten geht genauso wie bisher, egal auf welcher Konsole der Server läuft.
- Texte sagen jetzt „PS3- und PS4-Communitys entdecken“ bzw. „Konsole“ statt nur „PS3“.
- Server von älteren ORBIT-Versionen (1.2) erscheinen weiter, nur ohne Schild.

**Woher „Entdecken“ die Server kennt (ehrlich):**
1. Von Konsolen, mit denen du gerade verbunden bist (Freunde, Server, in denen du bist). Das
   funktioniert sofort, PS3 ↔ PS4.
2. Aus dem **öffentlichen Verzeichnis** (alle Server weltweit, auch von Fremden). Das gibt es erst,
   wenn das Verzeichnis-Programm (`orbit-dird`) auf einem Server im Internet läuft (z. B. dein VPS)
   und auf **beiden** Konsolen dieselbe Datei `directory.cfg` liegt (PS3:
   `/dev_hdd0/game/ORB000001/USRDIR/data/directory.cfg`, PS4: `/data/orbit/data/directory.cfg`).
   Ohne diese Datei funktioniert nur Punkt 1.

**Aus Test 9: Spiel-Cover erst nach deinem „Ja“**
- **Cover, die schon da sind, zeigt ORBIT immer:** Ordner `covers` oder `ORBIT/covers` auf dem
  USB-Stick und `/data/orbit/covers`. Dateien heißen `<TitleID>.JPG` oder `.PNG`
  (z. B. `CUSA00001.PNG`). ORBIT liest sie nur und ändert oder löscht nichts.
- **Frage beim ersten Start:** „Offizielle Spiel-Cover?“
  - **Ja:** fehlende Cover werden von GameTDB geladen (nur Title-IDs werden gesendet, aber
    GameTDB sieht deine IP-Adresse).
  - **Nein:** nichts wird aus dem Internet geladen.
  - **Kreis:** später entscheiden (Frage kommt beim nächsten Start wieder).
- Ändern: Einstellungen → Datenschutz → „Spiel-Cover herunterladen“. Das ist unabhängig vom
  Privacy Shield (in Test 8 hat Shield die Cover pausiert, das ist jetzt nicht mehr so).
- ORBIT liefert selbst keine Cover-Bilder mit. Die Cover gehören den Spiele-Firmen.
- Zum Crossplay-Test gibt es jetzt **ORBIT 1.3.0 für die PS3** (gleiche Funktionen ohne die
  PS4-Effekte): Ordner `Orbit-PS3-1.3.0-Test4` (passt zu Test 12).

**Aus Test 8: Privacy Shield und Sicherheit** (alle Details: **Sicherheitsstatus-DE.md**)
- **Privacy Shield** unter Einstellungen → Datenschutz, standardmäßig **an**: Niemand verbindet
  sich direkt mit deiner IP, auch nicht im Sprachchat. Alles läuft über den Orbit-Tunnel
  (bore.pub) oder einen Vermittler. Klappt das nicht, verbindet ORBIT **nicht** heimlich direkt,
  sondern zeigt einen Hinweis „Privacy Shield: …“.
- Shield an bedeutet auch: keine Router-Freigabe, keine Heimnetz-Suche. Im selben Heimnetz läuft
  die Verbindung jetzt auch über den Tunnel (etwas langsamer). Willst du das nicht, schalte Shield
  dort aus.
- **Forward Secrecy:** Konsolen mit 1.3 benutzen pro Verbindung einen frischen Schlüssel.
- Wichtig für den Test mit der PS3: Eine PS3 mit 1.2 verbindet sich weiter, aber ohne Forward
  Secrecy. Gibt sie dir einen alten Code mit ihrer **direkten** IP, lehnt die PS4 ihn mit Shield
  ab: dann den Code neu erzeugen (der Tunnel-Code geht) oder Shield kurz ausschalten. Mit der
  PS3-Version 1.3.0 mit Shield an passiert das nicht (ihre Codes enthalten dann nie die direkte IP).

**Aus Test 7: PS3 ↔ PS4 (Crossplay über bore.pub)**
- PS4 und PS3 chatten, sind befreundet und treten Servern der jeweils anderen Konsole bei – über
  denselben Orbit-Tunnel (bore.pub) wie PS3 untereinander. Ablauf: **Crossplay-Testplan.md** in
  diesem Ordner.
- **PS3/PS4-Schild** neben Namen in der Freundesliste und im Profil.
- Alle Texte sprechen auf der PS4 von „dieser PS4“ statt „dieser PS3“.
- Wenn der PS4-Namensdienst bore.pub nicht findet, fragt ORBIT direkt einen öffentlichen
  DNS-Server (nur der Name „bore.pub“ wird gesendet).
- `ps4-log.txt` zeigt jetzt, ob bore.pub gefunden wurde und ob der Tunnel online ist.

**Aus Test 6: Premium-Layout (nur PS4)**
- **Glas-Seiten:** Freunde, Server, Nachrichten und Einstellungen liegen jetzt in einer großen,
  abgerundeten Glasscheibe mit Lichtkante statt in einem harten Rahmen. Die linke Liste und die
  Karten darin sind ebenfalls aus Glas.
- **Chat-Blasen:** Nachrichten stehen in weichen Blasen; deine eigenen sind blau getönt.
- **Auswahl:** Die markierte Zeile ist jetzt blau getöntes Glas mit leuchtender Kante (in Listen
  und Menüs), und sie gleitet weiter weich mit.
- **Glas-Dock:** Die Tastenhinweise unten schweben in einer abgerundeten Glasleiste.
- **Eingabefeld** im Chat mit Lichtkante; Dialog-Lichtkante dezenter.

**Aus Test 5: Bewegung (nur PS4)**
1. **Gleitender Fokus:** Der blaue Fokus-Rahmen auf Start, Entdecken und Spiele gleitet weich zum
   nächsten Element, statt zu springen. Auch der Rahmen zwischen den runden Knöpfen gleitet.
2. **Gleitende Auswahl in Listen:** In Freunde, Server, Nachrichten und Einstellungen sowie in allen
   Menüs (Kreis-Menü, Server-Einstellungen …) gleitet die helle Auswahl-Fläche zur neuen Zeile.
3. **Weiches Scrollen:** Lange Listen springen nicht mehr Zeile für Zeile, sondern rollen weich
   (Ränder sauber abgeschnitten).
4. **Benachrichtigungen (Toasts):** schweben weich von oben ein und blenden am Ende aus.
5. **Mehr Glas:** Dialoge sind durchsichtiger und haben oben eine feine Lichtkante; die großen
   Seitenflächen lassen das Leuchten des Hintergrunds stärker durch.

Alles davon gibt es **nur in der PS4-Version**; die PS3-Version bleibt unverändert schnell.
„Bewegung reduzieren“ oder das Thema „Leistung“ schaltet die Bewegungen ab (dann springt der Fokus
wie früher).

**Aus Test 4:** Milchglas mit echtem Blur hinter Dialogen, Menüs und Tastatur.
**Aus Test 3 (auf deiner PS4 bestätigt, danke!):** Tabwechsel mit L1/R1 gleitet seitlich,
Dialoge blenden weich, Tastenhinweise blenden durch, alles in 1920 × 1080.

Aus Test 2 bleibt drin: die Startmodule gegen die Meldung „Anwendung ist beschädigt“.

Test 8 wurde **nur auf dem PC gebaut und geprüft (Tests und Vorschaubilder), nicht auf einer
PS4 gestartet**. Die Bewegungen zeichnen die Seite in jedem Bild neu – bitte achte darauf, ob
das Gleiten flüssig ist oder ruckelt (R3 zeigt die FPS).

**Vorher:** Das alte ORBIT löschen (Icon → OPTIONS → Löschen), dann Test 8
installieren. Deine Daten in `/data/orbit` bleiben dabei erhalten.

## Was du brauchst
- PS4, PS4 Slim oder PS4 Pro mit Firmware 14.02 und aktivem GoldHEN
- Die Datei `ORBIT-PS4-1.3.0-TEST.pkg` aus diesem Ordner
- Einen USB-Stick (FAT32 oder exFAT, Partitionsstil MBR) oder GoldHEN-FTP
- Bitte prüfe kurz, ob andere Homebrew-Apps bei dir starten. Wenn auch die „beschädigt“
  melden, liegt es am Jailbreak/GoldHEN (z. B. nach Ruhemodus neu starten und GoldHEN neu
  laden), nicht an ORBIT.

## Installieren
1. Kopiere `ORBIT-PS4-1.3.0-TEST.pkg` auf den USB-Stick (direkt ins Hauptverzeichnis).
2. Stick an die PS4 stecken.
3. In GoldHEN den **Package Installer** öffnen (Einstellungen → GoldHEN → Package Installer
   bzw. "Debug Settings → Game → Package Installer", je nach Menü).
4. `ORBIT-PS4-1.3.0-TEST.pkg` auswählen und installieren.
5. Auf dem Startbildschirm erscheint **ORBIT** mit dem blauen Planeten-Logo. Starten.

Deinstallieren geht wie bei jedem Spiel: Icon → OPTIONS → Löschen. ORBIT ändert keine
Systemdateien. Deine ORBIT-Daten liegen in `/data/orbit` (bleiben beim Löschen erhalten;
Ordner per FTP löschen, wenn du alles entfernen willst).

## Bedienung
Wie auf der PS3: Steuerkreuz oder linker Stick bewegt den Fokus, **Kreuz** = OK,
**Kreis** = zurück, **L1/R1** = Tabs, **Dreieck/Quadrat** wie auf der PS3,
**OPTIONS** = START, **Touchpad drücken** = SELECT, **R3** = Leistungsanzeige.

## Bitte teste (und schreib mir kurz, was passiert)
0. **Neues Layout:** Wie wirken Freunde, Server-Chat (Blasen), Einstellungen und das Glas-Dock
   unten? Was gefällt dir, was soll anders sein?
   **Bewegung und Glas:** Auf der Startseite mit dem Steuerkreuz zwischen den Karten wechseln –
   gleitet der Rahmen? In Freunde/Server schnell hoch und runter gehen – gleitet die Auswahl,
   rollt die Liste weich? Kreis-Menü und Tastatur öffnen – Glas und Blur ok? Hakt etwas?
   R3 zeigt die FPS.
1. **Startet ORBIT?** Siehst du das Intro und danach die Startseite? Oder bleibt der Bildschirm
   schwarz / stürzt die App ab (Fehlercode notieren, z. B. CE-xxxxx)?
2. **Leuchtet die Lichtleiste** des Controllers blau?
3. **Sprache:** Ist ORBIT in der Sprache deiner PS4? Stimmen Uhrzeit und dein PS4-Name?
4. **Anmelden / Profil:** Profil anlegen, deine Orbit-ID ansehen.
5. **Chat mit der PS3 (wichtigster Test):** Freund per Freundescode hinzufügen (PS3 ↔ PS4),
   Nachricht hin und zurück schicken, einem PS3-Server beitreten, auf der PS4 einen Server
   erstellen und von der PS3 aus beitreten.
6. **Ton:** Hörst du die Menü-Klänge?
7. **Sprachchat (experimentell):** Sprachkanal betreten. Hörst du die PS3-Person? Hört sie dich?
   (Headset am Controller oder USB-Headset.) Unter Einstellungen → Audio steht eine
   technische Zeile zum Mikrofon – bitte abfotografieren.
8. **Leistung:** R3 drücken und den FPS-Wert ablesen (Startseite und Chat).

## Wenn etwas nicht geht: Logdatei
ORBIT schreibt bei jedem Start `/data/orbit/ps4-log.txt` (nur technische Schritte und
Fehlercodes – **keine Nachrichten, keine Schlüssel, keine Codes**). Hol sie per GoldHEN-FTP
(FTP-Programm am PC, Adresse wie von GoldHEN angezeigt, Port 2121) und schick mir den Inhalt.
`/data/orbit/performance.txt` enthält die Leistungswerte.

## Was in Test 8 noch NICHT drin ist
- Größere Schrift und neu aufgeteilte Detail-Seiten (Einstellungen-Details, Profil) – nächster
  Layout-Schritt, je nachdem was dir an Test 6 gefällt
- Deine PS4-Spiele in der Spielebibliothek (kommt als Nächstes)
- PS4-Systembenachrichtigungen (kommt als Nächstes)
- Ende-zu-Ende-Verschlüsselung für Direktnachrichten und Sprache, verschlüsselter Verlauf (siehe
  Sicherheitsstatus-DE.md – der Kern existiert, ist aber noch nicht eingebaut)
- Sprachchat/Benachrichtigungen während eines laufenden Spiels (wird erforscht)
- Eigener ORBIT-Eintrag im PS4-Funktionsbereich (dafür gibt es unter 14.02 bisher keinen
  sicheren, rückgängig machbaren Weg – siehe Bericht)
- Neues 3D-Intro (kommt später, wie besprochen), GPU-Grafik, 4K
- Automatische Updates (auf der PS4 abgeschaltet, sonst würde das PS3-Paket angeboten)
- In den Einstellungen gibt es noch PS3-Punkte (Hintergrund-Plugin, DualSense) – die
  funktionieren auf der PS4 nicht und werden im nächsten Test ausgeblendet.

Nichts davon wurde veröffentlicht. Bitte gib das Paket nur an Leute weiter, wenn du das willst.
