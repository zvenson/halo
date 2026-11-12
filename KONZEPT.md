# HALO – hörende Drone für öffentliche Räume (Konzept, 12.11.2026)

Sven, 12.11.2026: „Drone auf Basis von Mic-Input – für öffentliche Räume. Sound kommt über ein Mikrofon rein und
triggert etwas … wählbar, gut klingend und sphärisch, mit Bezug zum Mic-Input oder anderem Sound-Input.“
HALO ist Arbeitstitel (ID `halo`), Name offen.

Mikro an **IN 1 (MIC)**, wahlweise IN 2 oder beide (LISTEN → SOURCE). Ohne Mikro **singen die Tasten eine Test-Stimme
in den Eingang** (ein „ah“ mit Vibrato) – alles lässt sich so im Emulator ausprobieren.

## Was HALO hört (listen.c)

| Größe | wie | wofür |
|---|---|---|
| FLOOR | Grundpegel des Raums, lernt ständig: steigt langsam (ADAPT, Standard 60 s), fällt schnell (2 s) | alles wird **über** dem Grundpegel gemessen – ein dauerlauter Bahnhof wird zur neuen Stille, und die Drone, die sich selbst im Raum hört, schaukelt sich nicht auf |
| THRESHOLD | FLOOR + 24…6 dB (SENSE) | ab hier „passiert etwas“: Gate auf, Anregung fließt |
| PRESENCE | wie weit über der Schwelle, 0…1, geglättet mit RISE / FALL | die Lautstärke der Drone (MOTION → RESPOND) |
| ONSETS | schnelle Hüllkurve springt über die mittlere (×2), mind. 120 ms Abstand | CHIMES: jeder Laut schlägt eine Glocke an |
| HEARD | YIN bei sr/4, 60 Hz – 1 kHz, alle ~21 ms | FOLLOW: ein gehaltener Ton zieht den Akkord auf seine Stufe; CLOUD stimmt Körner um |
| BRIGHT | Anteil über 2,5 kHz | CHOIR: Vokal, Helligkeit |
| GUARD | ein **starrer** Ton (< 8 Cent Abweichung, 1,5 s, laut) = Rückkopplung im Entstehen – keine Stimme hält so still (Vibrato, Jitter) | Mikro-Zufuhr −18 dB für 8 s, Kopfzeile zeigt GUARD |

Die Anregung (für BLOOM, CLOUD und den MIC-Send) ist das Eingangssignal, hochpassgefiltert (90 Hz), automatisch
gepegelt (höchstens +18 dB, mit GUARD +6 dB) und vom Gate geöffnet.

## Die vier Engines (engines.c, K2 ENGINE)

| Engine | Bezug zum Eingang | Encoder |
|---|---|---|
| **BLOOM** | sechs Karplus-Saiten auf dem Akkord, **der Raumklang selbst regt sie an** (wie Resonanzsaiten einer Sitar, Klavier mit gedrücktem Pedal). Ein leises Rauschen hält sie summend | DECAY · COLOR · STRINGS · DETUNE |
| **CLOUD** | 12 s Gedächtnis aus dem, was das Mikro hörte (nur wenn etwas zu hören ist – im leeren Raum spielt die Wolke die letzten Stimmen weiter), bis 24 Körner, **TUNE stimmt jedes Korn vom gehörten Ton auf den nächsten Akkordton** – Gespräche werden ein Chor in der Tonart | SIZE · DENSITY · TUNE · SPREAD |
| **CHOIR** | Akkord als sechs Stimmen aus je drei verstimmten Sägezähnen, Tiefpass + Formanten (oo…ee); **FOLLOW zieht den Akkord zum gesummten Ton, die Helligkeit des Raums verschiebt den Vokal** | VOWEL · BRIGHT · DETUNE · BREATH |
| **CHIMES** | **jeder Onset schlägt eine FM-Glocke aus dem Akkord an** (nahe am gerade gehörten Ton), dazu zufällige nach PRESENCE; leises Sinus-Pad darunter | DENSITY · DECAY · METAL · PAD |

Engine-Wechsel blendet 2,5 s über (beide laufen solange). MORE: 4. Wert, LEVEL, DRIFT (langsame Zufallsverstimmung).

## Harmonie (K3 TONE)

KEY, SCALE (MAJOR, LYDIAN, MIXO, DORIAN, MINOR, PHRYG, PENTA, PENTA m, HIRA, WHOLE), OCTAVE (LOW/MID/HIGH/TOP: Grundton
um C2…C5, Bass eine Oktave darunter), FOLLOW (0 = fest; sonst muss ein Ton 3,75…0,25 s stehen). CHORD: VOICING
(OPEN, FIFTHS, CLOSE, CLUSTER – Intervalle über dem Akkordgrundton, in die Skala gerastet), GLIDE (wie langsam die Töne
wandern), MOVE (Akkord wechselt von selbst alle 20…120 s, bei 7-stufigen Skalen bevorzugt I IV V vi).
Bild: Quintenzirkel – Skala hell, Akkord gelb, Grundton hellblau, gehörter Ton grüner Ring.

## Raum und Reaktion

- **K4 SPACE** (space.c): FDN-Hall mit 8 Leitungen (Hadamard, Dämpfung, langsam schwankende Lesepunkte), T60 2…60 s.
  SHIMMER = Hallfahne eine Oktave hoch zurückgeführt. SIZE, MOD, WIDTH. Der **MIC-Send** (LISTEN → ROOM) schickt die
  Stimmen des Raums selbst in den Hall.
- **K5 MOTION:** RESPOND = **SWELL** (mehr los = voller) · **CALM** (mehr los = leiser, im stillen Raum voll – für
  belebte Orte) · **STEADY** (immer da, der Raum färbt nur). RISE, FALL, IDLE (Pegel im stillen Raum).
- **K6 WANDER:** für Installationen – alle EVERY Minuten ein anderes Programm aus FROM…TO, mit MORPH (GLOBAL) überblendet.

## Bedienung

K1–K6 = LISTEN · ENGINE · TONE · SPACE · MOTION · WANDER, **dieselbe Taste nochmal = nächste Unterseite** (Chips in der
zweiten Kopfzeile, anklickbar). Max. vier Werte auf Encoder 1–4. **A = LEVEL, B = SPACE (Mix)**, DATA = Programm.
F1 / REC / Pedal = **FREEZE** (tippen = an/aus, halten = solange: Akkord, Wolke und Pegel bleiben stehen, nichts hört
mehr zu) · F2 PRESETS · F3/F4 PRG −/+ · F5 SAVE (halten + Pad = dorthin, SHIFT = Werksklang) · F6 GLOBAL (OUTPUT, PADS,
MORPH, METERS). **STOP = HUSH** (langsam ausblenden, z. B. Ladenschluss), PLAY = zurück. Expression-Pedal = LEVEL.
Pads 1–16 = Programme, wechseln mit MORPH.

## Presets

BLOOM: SYMPATHY · TANPURA (STEADY, Quinten) · GLASS (hoch, Shimmer) · DEEP · CLOUD: MURMUR · FLOCK · TIDE (CALM) ·
STATION (CALM, weit, für belebte Orte) · CHOIR: CATHEDRAL · BREATH (CALM, „oo“) · AURORA (Shimmer) · CHANT (FOLLOW 90 %)
· CHIMES: WIND CHIMES · TEMPLE · RAIN · NIGHT (STEADY).

## Ruhiges Display

Wie bei VALVE (das SPI des Displays streut hörbar in einen Mikrofon-Eingang): Live-Werte nur 4× je Sekunde in groben Stufen, keine Zeit-Animationen.
GLOBAL → METERS OFF = Standbild. Bei einem Mikro am Eingang besonders wichtig.

## Stand

**0.1 (12.11.2026)** – gebaut, vibe-check BESTANDEN, Rechenzeit (Node) Mittel 4,5 %, 99,9 % 13,8 % – zwischen VALVE und
VOX. Tests in `build/test-halo/` mit der Sprachaufnahme aus test-vox als Mikro (`room.wav`): bloom, cloud, choir,
chimes, tanpura, station (je 40 s, Stimme + gesungene Test-Töne), guard (Stimme → kein Alarm, starrer 700-Hz-Sinus →
GUARD), wander (WANDER nach 1 min, FREEZE, HUSH, Engine-Überblendung), pages (alle Seiten).
`node tools/wasmtest.mjs build/halo.wasm build/test-halo/cloud.json build/test-halo/out-cloud`

Offen: mit echtem Mikro im Raum hören (Lautsprecher-Abstand, GUARD-Schwelle, SENSE-Standard), Klang der Engines nach
Gehör abstimmen, Name.
