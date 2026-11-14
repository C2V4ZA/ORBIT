# ORBIT 1.0.3 – Anleitung

[← Zurück zum README](../README-DE.md)

## Installieren / Update
1. `Orbit-PS3-1.0.3.pkg` von der [Release-Seite](https://github.com/C2V4ZA/ORBIT/releases/tag/v1.0.3) laden, auf einen FAT32-USB-Stick kopieren und über den **Package Manager** installieren (PS3 mit CFW oder HEN).
2. Das Update ersetzt 0.9.5. **Gruppen, Nachrichten und dein Name bleiben erhalten.** Jede alte Gruppe wird ein Server mit Textkanal „allgemein“ (alte Nachrichten) und einem Sprachkanal.
3. Zurück auf 0.9.5 geht danach nicht mehr. Wer sichergehen will, sichert vorher per FTP den Ordner `/dev_hdd0/game/ORB000001/USRDIR/data`.

## Sprache
ORBIT übernimmt die PS3-Systemsprache (Deutsch, Englisch, Französisch, Spanisch, sonst Englisch). Ändern: Einstellungen › Sprache. Nachrichten, Namen, Server und Kanäle werden nie übersetzt.

## Bedienung
- **◀ ▶** wechselt zwischen HOME · FREUNDE · NACHRICHTEN · SERVER · EINSTELLUNGEN
- **▲ ▼** Liste · **✕** öffnen/schreiben · **○** Menü
- **SELECT** Sprachchat / anrufen · **R2** stumm · **△** einladen · **□** Code eingeben
- **L2** Nachrichten auswählen (antworten, reagieren, bearbeiten, löschen) · **L1/R1** blättern
- Tastatur: **R1** wechselt das Layout (DE/EN/FR/ES)

## Freunde hinzufügen (ohne Router, ohne PC)
1. Du: FREUNDE › **Dein Freundescode** – den 20-Zeichen-Code an deinen Freund schicken.
2. Freund: FREUNDE › **Freund hinzufügen** › Code eintippen.
3. Du: Die Anfrage erscheint oben in FREUNDE › **✕ annehmen**.
4. Vergleicht die **Orbit-ID** (z. B. `PQ96-D2FM-5DBS`) – sie gehört fest zu jeder PS3.

Danach: Freund auswählen › ✕ Nachricht · SELECT anrufen · △ in deinen Server holen.

## Server
SERVER › **Server erstellen** › „Mit Vorlage“ (Information, Community, Sprachkanäle) oder „Einfach“.
○ Menü › **Server-Einstellungen**: Name, Beschreibung, Willkommensnachricht, Icon, Farbe, Banner,
öffentlich, Kategorien & Kanäle, Rollen & Rechte, Mitglieder, Gebannte, neuer Einladungscode.

## Ton & Mikrofon
- Headset oder **PS5-Controller per USB-Kabel** anstecken. Welches Gerät Mikrofon und Ton bekommt: PS3-Einstellungen › Zubehör › Audiogeräte.
- Der PS5-Controller kann in ORBIT als Mikrofon zum Sprechen verwendet werden. Die Stimmen anderer Teilnehmer können jedoch nicht über den Lautsprecher des Controllers wiedergegeben werden. Verwende dafür den Fernseher oder ein unterstütztes Audiogerät.
- Einstellungen › Audio › **Mikrofon testen**: Du hörst dich selbst so, wie deine Freunde dich hören. Das Grundrauschen sollte in einem ruhigen Raum deutlich unter 300 liegen.

## Was du wissen solltest
- Ein Server läuft auf der PS3, die ihn erstellt hat. Ist ORBIT dort aus, steht „Gastgeber offline“.
- Offline geschriebene Nachrichten warten auf deiner PS3 („Wartet auf Zustellung“) und gehen raus, sobald die andere PS3 erreichbar ist.
- Die Verbindung zwischen den PS3 ist verschlüsselt (TLS). Die Gastgeber-PS3 selbst kann die Nachrichten ihrer Server lesen.
- Kein Sprachchat während eines Spiels – die PS3 führt nur ein Programm gleichzeitig aus.
