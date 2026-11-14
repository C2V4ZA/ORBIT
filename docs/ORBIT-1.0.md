# ORBIT 1.0.3 – Architektur, Sicherheit, Tests (technische Notizen, Deutsch)

## 1. Ausgangslage (Analyse von 0.9.5)

| Baustein | Datei | Rolle |
|---|---|---|
| Treffpunkt (Host) | `src/store.c` | Daten jeder PS3 (Benutzer, Gruppen, Nachrichten) + JSON-Anfragen, atomar gespeichert in `data.json` |
| Netzwerk | `src/network.c` | TLS 1.2 PSK (mbedTLS), Peer-Threads, Sprach-Relay, LAN-Rundruf |
| App-Kern | `src/app.c` | eine verschlüsselte Verbindung („Link“) pro fremder PS3, Polling, Einladungen, Sprachchat-Thread |
| Erreichbarkeit | `src/nat.c`, `src/bore.c`, `src/relay.c` | UPnP/PCP, eingebauter bore-Tunnel, optionaler Vermittler |
| Oberfläche | `src/ui.c`, `src/main_ps3.c` | Software-Renderer 1280×720, eigene Tastatur, Eingabe |
| Sprache | `src/voice_core.c`, `src/voice_ps3.c` | IMA-ADPCM 16 kHz, VAD, Jitter-Puffer, 48-kHz-Mischer, cellMic/cellAudio |

Prinzip, das 1.0 beibehält: **Jede PS3 ist ihr eigener Server.** Was du erstellst, liegt bei dir.
Wer eingeladen wird, verbindet sich mit deiner PS3. Es gibt keinen Pflicht-Server.

## 2. Was 1.0 daraus macht

| Feature | Umsetzung |
|---|---|
| **Systemsprache** | `src/i18n.c`, `include/i18n_keys*.h`: 500+ Schlüssel, DE/EN/FR/ES, `SYSUTIL_SYSTEMPARAM_ID_LANG` bei jedem Start, Englisch als Fallback, manuelle Wahl in den Einstellungen. Fehler des Hosts kommen mit stabilem Schlüssel (`ekey`) und werden beim Leser übersetzt. |
| **Identität** | Ed25519-Schlüssel aus dem vorhandenen `device.key` abgeleitet (`orbit_identity`). Orbit-ID = 60 Bit BLAKE2b des öffentlichen Schlüssels (`ABCD-EFGH-JKLM`). Bei jeder Anmeldung signiert die PS3 „orbit-bind-v1 + Treffpunkt-ID“ → keine Wiederverwendung auf einer anderen PS3. Name änderbar, ID bleibt. |
| **Freunde** | Freundescode = 20-Zeichen-Einladung zum versteckten „Home“-Eingang der PS3. Anfrage (`friend`), Annehmen/Ablehnen, Entfernen, Blockieren, Freundesliste nur für den Besitzer (`local`-Sitzung über den Server-Schlüssel). Beidseitige Anfrage = Freundschaft. |
| **Private Nachrichten** | Gruppen der Art `GK_DM` auf der PS3 des Erstellers, nur mit Freunden, auch Gruppen-DMs. Antworten, Bearbeiten (nur eigene), Löschen, 6 Reaktionen, Suche, Stummschalten, Ungelesen pro Kanal. |
| **Offline-Nachrichten** | Lokale Warteschlange (max. 32, gespeichert in `profile.json`). Angezeigt als „Wartet auf Zustellung“, nie als zugestellt. Es gibt **keinen** Offline-Speicher auf einem Relay. |
| **Community-Server** | Bisherige Gruppen werden Server: 8 Kategorien, 16 Text-/Sprachkanäle, Beschreibung, Willkommensnachricht, Icon, Farbe, Banner, öffentlich/privat, Vorlage „Information / Community / Sprachkanäle“. |
| **Rollen** | Owner, Admin, Moderator, Member + eigene (max. 8) mit Name, Farbe, Rang, 10 Rechten. Kanäle: „wer darf sehen / schreiben“. Alles prüft der Host (`orbit_perms`, Rang-Prüfung). |
| **Profil** | Anzeigename, Orbit-ID, Farbe, Profilbild-Symbol, Über mich, Lieblingsspiel, eigener Status, Status (Online/Abwesend/Beschäftigt/Unsichtbar/Offline), Erstellungsdatum. |
| **Sprachkanäle** | Bestehender Codec/VAD/Jitter-Puffer unverändert. Neu: Kanal-Trennung im Host-Relay, Server-Stummschaltung (Host verwirft Pakete), lokale Stummschaltung und Lautstärke 0–200 % pro Person (`voice_set_gain`), Berechtigungen. |
| **Benachrichtigungen** | Anfragen, Annahmen, DMs, Servernachrichten, Einladungen, Anrufe, @Erwähnungen, Moderation – einzeln abschaltbar, in der Systemsprache, Nutzertext unverändert. |
| **UI** | 5 Bereiche (◀ ▶), Listen (▲ ▼), Nachrichtenmodus (L2), generische Dialoge, 4 Themes. Bestehende Tasten unverändert. |
| **PS5-Controller** | Mikrofon über die USB-Audio-Verarbeitung der PS3. Der experimentelle eigene Treiber `src/dualsense.c` ist in 1.0.3 abgeschaltet. Wiedergabe über den Controller-Lautsprecher wird nicht unterstützt. |

## 3. Protokoll und Versionen

- `ORBIT_PROTOCOL` 9 → 10. Der Client sendet `{"op":"hello","protocol":10}`.
- Ein **0.9.x-Client** an einem 1.0-Host bekommt die alte Sicht: nur Server, `send`/`history` ohne Kanal
  landen im ersten Textkanal. Getestet in `tests/test_social.c`.
- Ein **1.0-Client** an einem 0.9-Host funktioniert mit Basisfunktionen (ein Kanal) und zeigt das an.
- `data.json` Version 1 (0.9.x) wird beim Start in Version 2 umgewandelt. **Kein Rückweg auf 0.9.5.**

## 4. Sicherheit – ehrliche Vertrauensgrenzen

- **TLS ≠ Ende-zu-Ende.** Die Verbindung zur Gastgeber-PS3 ist mit TLS 1.2 (PSK aus dem Gruppencode,
  Argon2) verschlüsselt. Tunnel (bore.pub) und Vermittler sehen nur verschlüsselte Bytes.
  **Die Gastgeber-PS3 selbst sieht und speichert alle Nachrichten ihrer Server und DMs im Klartext.**
  Das ist kein E2E im strengen Sinn; die Oberfläche sagt das unter Datenschutz.
- Wer einen Einladungs- oder Freundescode kennt, kann sich mit der PS3 verbinden (nicht automatisch
  Mitglied). Codes sind 40 Bit, Versuche sind pro Person begrenzt.
- Orbit-IDs sind an den Schlüssel gebunden. Ein Gastgeber könnte jedoch falsche Mitgliederlisten
  anzeigen; Freundschaften speichern den vollen öffentlichen Schlüssel (TOFU).
- Keine geheimen Schlüssel in Logs; Tokens werden nach Gebrauch überschrieben (`crypto_wipe`).
- Spam-Schutz: Anfragen max. 6/Minute pro Person, Anmeldungen 30/Minute, Codeversuche 12/Minute.
- Blockieren wird auf der eigenen PS3 durchgesetzt (keine Anfragen, Entfernung aus DMs, kein Schreiben
  in DMs); in fremden Servern werden Nachrichten Blockierter nur ausgeblendet.

## 5. Grenzen

- 48 Plätze pro PS3 (Server + DMs + Freundes-Eingang), 32 Personen pro PS3, 16 Kanäle pro Server,
  512 gespeicherte Nachrichten pro PS3 insgesamt (älteste fallen raus), 16 verbundene fremde PS3.
- Server sind nur erreichbar, solange Orbit auf der Gastgeber-PS3 läuft.
- Suche nur über die Orbit-ID ohne Code bräuchte einen Verzeichnisdienst – nicht gebaut.
- „Spielt gerade …“: unter Homebrew nicht möglich (nur ein Programm gleichzeitig) – nicht vorgetäuscht.
- Schrift: Latein, Kyrillisch, einige Symbole. Japanisch/Koreanisch/Chinesisch fehlen → Oberfläche Englisch,
  solche Zeichen in Nachrichten erscheinen als Kästchen.
- PS5-Controller: Sprechen über sein Mikrofon geht (die PS3 behandelt ihn als USB-Audiogerät). Ton über den
  Controller-Lautsprecher wird nicht unterstützt.

## 6. Tests (Desktop, automatisch)

| Test | Inhalt |
|---|---|
| `test-core`, `test-network`, `test-sessions`, `test-app` | alle 0.9.5-Tests unverändert grün (Rückwärtskompatibilität) |
| `test-social` | Migration 0.9.5, Protokoll 9, Identität/Fälschung, Freunde, DMs, Blockieren, Server/Kanäle/Kategorien, Rollen/Rechte/Rang, private Kanäle, Mute/Bann, Antworten/Bearbeiten/Reaktionen/Suche, Präsenz/Profil, Persistenz |
| `test-friends` | zwei komplette Apps: Freundescode → Anfrage → Annahme → Präsenz → DM → Benachrichtigung → Server mit Vorlage → Freund hinzufügen → Offline-Warteschlange nach Neustart des Hosts |
| `test_relay/bore/tunnel/nat.sh` | Vermittler, eingebauter Tunnel, Router-Freigabe |
| `tools/verify_package.py` | PKG-Header, SELF, SFO 01.00, Importtabellen inkl. cellUsbd |

**Hardware:** ORBIT 1.0.3 wurde vom Entwickler auf echter PS3-Hardware getestet. Die internen Stände 1.0 bis
1.0.2 wurden nie veröffentlicht. Die Desktop-Tests nutzen denselben C-Code für Host, Protokoll und App-Kern;
Renderer, Tasteneingabe, Audiogeräte und Leistung lassen sich nur auf der Konsole prüfen.
