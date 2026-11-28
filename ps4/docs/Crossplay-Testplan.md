# ORBIT Crossplay-Testplan: PS3 ↔ PS4

Stand: Test 7 (PS4 1.3.0). Alles hier wurde **nur am PC** geprüft (Unit-Tests, Build). Ob PS3 und
PS4 sich wirklich finden, zeigt erst dieser Test auf deinen Konsolen.

## Wie die Verbindung funktioniert
- PS3 und PS4 benutzen **denselben Code** für Chat, Freunde, Server und Verschlüsselung
  (Protokoll 11, Orbit-ID mit Ed25519, TLS-PSK). Es gibt keinen „PS4-Modus“: eine PS4 ist für die
  PS3 einfach ein weiteres ORBIT.
- Erreichbar von außen wird eine Konsole automatisch über den **Orbit-Tunnel (bore.pub)**, wenn der
  Router keine Portfreigabe erlaubt. Die Konsole baut dazu selbst eine ausgehende Verbindung zu
  bore.pub auf; am Router musst du nichts einstellen.
- Neu in 1.3: Jede Konsole schickt mit ihrem Profil mit, ob sie eine **PS3 oder PS4** ist. Neben
  Namen erscheint dann ein kleines Schild „PS3“ oder „PS4“. Ältere ORBIT-Versionen ignorieren
  das einfach (dann fehlt das Schild, sonst funktioniert alles).
- Neu auf der PS4: Findet der PS4-Namensdienst bore.pub nicht, fragt ORBIT direkt einen
  öffentlichen DNS-Server (1.1.1.1, dann 8.8.8.8). Dabei wird nur der Name „bore.pub“ gesendet.

## Vorbereitung
1. PS3 mit ORBIT, PS4 mit ORBIT Test 7. Beide mit dem Internet verbunden.
2. Auf beiden: **Einstellungen → Verbindung & Status** öffnen. Notiere, was dort steht
   (z. B. „Internet bereit“, „Tunnel“, eine Adresse wie `bore.pub:12345`).
3. Auf der PS4 schreibt ORBIT zusätzlich Zeilen nach `/data/orbit/ps4-log.txt`:
   - `dns bore.pub -> …` (Name gefunden) bzw. `dns bore.pub: system resolver …, fallback …`
   - `tunnel online port …` / `tunnel offline …` (Tunnel-Zustand)

## Tests (bitte jeweils notieren: klappt / klappt nicht / Fehlermeldung)
| Nr. | Test | Erwartung |
|----|------|-----------|
| 1 | PS4: Verbindung & Status | „Internet bereit“ oder Tunnel online mit Adresse |
| 2 | PS4 zeigt Freundescode, PS3 fügt ihn hinzu | Anfrage kommt auf der PS4 an |
| 3 | PS4 nimmt an, beide sehen sich als Freunde | PS3 zeigt bei dir „PS4“ (wenn PS3 1.3 hat) |
| 4 | Umgekehrt: PS3-Code auf der PS4 hinzufügen | Anfrage kommt auf der PS3 an |
| 5 | Direktnachricht PS4 → PS3 und PS3 → PS4 | kommt in beide Richtungen an |
| 6 | PS4 erstellt einen Server, schickt Einladungscode, PS3 tritt bei | PS3 sieht Kanäle und schreibt |
| 7 | PS3-Server: PS4 tritt per Einladungscode bei | PS4 sieht Kanäle und schreibt |
| 8 | Eine Konsole aus, Nachricht schreiben, wieder an | Nachricht wird nachgeliefert („offline gespeichert“) |
| 9 | Profil auf der PS4 ansehen | Schild „PS4“ neben deinem Namen bei der PS3 (PS3 1.3) |
| 10 | Sprachkanal (experimentell) | Ton in beide Richtungen? |

## Wenn etwas nicht klappt
- Fehlermeldung abfotografieren (ORBIT zeigt Gründe wie „Nicht erreichbar …“).
- `ps4-log.txt` per GoldHEN-FTP holen und mir den Inhalt schicken (enthält keine Nachrichten,
  keine Schlüssel, keine Codes).
- Notieren: Welche Konsole war Gastgeber? Gleiches Heimnetz oder verschiedene Internetanschlüsse?

## Bekannte Grenzen
- Das PS3/PS4-Schild erscheint nur, wenn der **Gastgeber** (die Konsole mit dem Server bzw. dem
  Freundes-Treffpunkt) ORBIT 1.3 hat; ältere Gastgeber geben das Feld nicht weiter.
- bore.pub ist ein öffentlicher, kostenloser Dienst ohne Garantie. Ist er ausgelastet oder
  offline, klappt die Verbindung über den Tunnel nicht; im selben Heimnetz geht es trotzdem.
