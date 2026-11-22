# Lessons aus dem Pandora-Tomorrow-Port

Was der PT-Port (Offline.xbe, XDK 1.0.5788, Unreal Engine 2) gelehrt hat. Für den nächsten Port (SCDA/SCCT/PT-Familie
oder anderes XDK-/UE2-Spiel) als Nachschlagewerk. Format je Eintrag: **Symptom → Ursache → Fix → Code**.
Ergänzt den allgemeinen Leitfaden `../PORTING_GUIDE.md` (bzw. `~/git/switch/PORTING_GUIDE.md`).

Grundregel unverändert: keine Spieldaten, keine Keys, keine generierten Dateien, keine NROs im Repo oder Release.
Adressen und Offsets (wie unten) sind eigene Analyse und ok.

---

## 1. Symbole / XDK

- PT nutzt XDK **5788**, SCCT/SCDA 5849. Funktionsmuster unterscheiden sich leicht. Symbole deshalb über
  Aufrufer und Strings (`tools/xbe/xbetool.py dis|find|callers`) abgleichen, nicht über rohe Byte-Signaturen.
- Neue HLE-Einträge kommen nach `config/scpt.toml` `[symbols]`. Globale Engine-Daten und Thread-Starts kommen nach `[data]`.
- **D3D-Getter müssen echten Gerätespeicher zurückgeben.** Das Spiel liest Felder direkt aus dem Device-Struct.
  Bei 5788 sind das: Viewport dev+0xEE0, VertexShader dev+0x798, Projektions-Viewport-Matrix dev+0x980. Fehlt das
  Spiegeln, sind Overscan und Ecken falsch oder Draws werden übersprungen.

## 2. Rendering

### Weißes Wasser / Schaum-Quadrate
- **Symptom:** Wasser in einer Ecke weiß bzw. mit eckigen Fragmenten statt Wellen.
- **Ursache:**
  - Eine Fixed-Function-Stage ohne Textur las (1,1,1,1).
  - Die XDK-Combiner-Schleife (0x3460B0) nimmt *alle* Stages bis ColorOp DISABLE, auch die ohne Textur
    (Texture-Mode NONE wird bei 0x342650 gesetzt).
  - Auf echter Hardware liefert so eine Stage **(0,0,0,0)**.
- **Fix:** Eine ungebundene Stage liefert `float4(0,0,0,0)` (`src/gpu/shaders.cpp`).

### Fehlende Vertex-Attribute
- **Ursache:** SetVertexData2f/4f (0x33C3E0 / 0x33C420) setzen Attribute, die im nächsten Draw nicht im
  Vertex-Format stehen. Die Hardware behält den letzten Wert.
- **Fix:** `State::attr` plus `LatchAttributes` (`src/gpu/vertex.cpp`). Fehlende Attribute lesen `attr[n]` aus
  `DrawConstants`. Dazu HLE der beiden SetVertexData-Funktionen.

### Licht flackert je nach Blickwinkel (Depth-EQUAL-Passes)
- **Symptom:** Wandlicht oder Lichtkegel erscheinen und verschwinden je nach Perspektive.
- **Ursache:**
  - Der Basis-Pass läuft per Fixed Function, die Licht-Passes per Vertex-Programm, beide mit `D3DCMP_EQUAL`.
  - Unsere zwei Shaderpfade erzeugen nicht bit-identische Tiefe.
- **Fix:** EQUAL-Passes als LESS_OR_EQUAL zeichnen, mit Bias −32 Units (`kEqualSlack`) **plus Slope −1 × Render-Scale**
  (`kEqualSlopeSlack * scale_`). Ohne den Slope-Anteil bleibt bei flachen Winkeln Restflackern
  (`src/port/gpu_vk.cpp`).

### Schattenflecken nur auf der Switch (Embassy-Innenhof)
- **Symptom:** Am PC korrekt. Auf der Switch blinken Schatten je nach Winkel hell auf.
- **Ursache:** NVK auf Tegra wählte **D24**, der PC D32. Die Präzision reicht für die Shadow-/EQUAL-Vergleiche nicht.
- **Fix:** `depth32 = true` als Default (`src/app/app.h`, `src/port/vk_device.cpp`, D32S8).
- **Lehre:** Wenn etwas nur auf der Switch kaputt ist, zuerst Formate vergleichen (Depth, Farbformate, sRGB).

### Sam flackert anfangs
- **Ursache:** Asynchrone Pipeline-Builds. Der Draw wurde übersprungen, bis der Shader fertig war.
- **Fix:** `shader_wait_ms = 50`. Bis zu 50 ms auf den Build warten statt den Draw zu verwerfen (`AwaitBudget`).
  Kurzes Ruckeln am Anfang ist normal, weil der Shader-Cache sich aufbaut.

### Visibility-Tests
- Liefen über Occlusion Queries, das war ok. Nicht durch Konstanten ersetzen, sonst verschwinden Lens Flares und Licht.

## 3. Audio

### Sprachfetzen (Untertitel-Kommentare abgehackt)
- **Symptom:** Gesprochene Sätze bestehen nur aus Anfängen und brechen abrupt ab. Tritt auch unter Linux auf.
- **Ursache:**
  - Der Stream-Thread des Spiels (0x2239F0) ruft `Sleep(1)` auf, rechnet aber fest mit 10-ms-Runden
    (rate×0.01 bei 0x2225B5).
  - Alle Streams (LS2/SS2, Ubi-ADPCM) werden vom Spiel in **einen** 36-kHz-Loop-DSound-Buffer gemischt.
  - Mit echten 1 ms läuft das Mischen zu schnell und überschreibt sich.
- **Fix:** In `KeDelayExecutionThread` schläft genau dieser Thread mindestens 10 ms (Start über `[data]
  Sound_StreamThread`, `Thread::start_routine`). Code: `src/kernel/xboxkrnl.cpp`.
- **Lehre:** Bei Timing-Bugs im Audio zuerst prüfen, ob ein Thread eine Sleep-Granularität annimmt.
  `SCPT_DSOUND_TRACE` zeigt Buffer-Positionen.

## 4. Sprache / Region

- **Symptom:** Deutsch ist eingestellt, das Spiel bleibt trotzdem Englisch.
- **Ursache:** PT spielt DE/IT nur auf einer **PAL-Konsole**. Das Flag 0x44FAAC kommt aus XGetVideoStandard (0x20CB08).
  XGetLanguage liegt bei 0x20CB31.
- **Fix:** Der Kernel meldet für deu/ita PAL-I `0x00800300` und das Video-Flag PAL60 `0x00400000`
  (`PalConsole`/`AvRegion`/`VideoFlags` in `src/kernel/xboxkrnl.cpp`).
- **Lehre:** Wenn die Sprache nicht greift, auch den Videostandard bzw. die Region prüfen, nicht nur XGetLanguage.

## 5. Unreal Engine 2 Interop (In-Game-Menü ohne Overlay)

Ziel war, dass die Recomp-Settings im echten Pausemenü des Spiels sitzen, wie bei SCCT. Ein Overlay wollte der User nicht.

### Adressen (PT)

| Was | Adresse | Hinweis |
|---|---|---|
| GObjects (TArray) | 0x4D9018 | |
| GNames (TArray) | 0x4D4F54 | FNameEntry: idx, flags, hashnext, ANSI-Name ab +0xC |
| StaticConstructObject | 0x51E30 | cdecl (Class, Outer, Name, Flags, Template, Error, 0) |
| UObject::ProcessEvent | 0x382A0 | thiscall (Function, Parms, Result); vtable-Slot 4 |
| FArray::Realloc | 0x42A90 | thiscall (ElementSize); nutzt Max bei +8 |
| GMalloc | 0x42BAC0 | |
| Localize (C++) | 0x43420 | cdecl (Section, Key, Package, 0, 0); als `Engine_Localize` gehookt |
| GNatives | 0x483A40 | |
| execNew | 0x3AB00 | |
| ProcessInternal | 0x321A0 | |

Natives liegen als Paare `{L"int<Class>exec<Name>", fn}` im Binary. Darüber findet man sie schnell per String-Suche.

### Layouts

| Struktur | Feld | Offset |
|---|---|---|
| UObject | StateFrame | +0xC |
| | Outer | +0x18 |
| | Flags | +0x1C |
| | Name | +0x20 |
| | Class | +0x24 |
| UField | Next | +0x30 |
| UStruct | SuperField | +0x2C |
| | Children | +0x3C |
| | PropertiesSize | +0x40 |
| UFunction | Script-TArray | +0x44 |
| | ParmsSize \| RetOff<<16 | +0x64 |
| | Func | +0x68 |
| UProperty | ArrayDim/ElemSize | +0x38 |
| | Offset | +0x44 |
| ObjectProperty | Class | +0x54 |
| FStateFrame | StateNode | +0x1C (Name des aktiven States) |

Weitere Eigenheiten:
- Ein FString in Parametern ist 16 Byte groß.
- Im Bytecode sind Objekte 4-Byte-Pointer und Names 4-Byte-Indizes.
- Delegate-Tokens: 0x44 (Delegate-Property) und 0x45 (LetDelegate).

### Vorgehen
1. **Speicher dumpen:** Unter Linux F11 drücken, das schreibt `guest_00000000.bin` per pwrite je 4K-Seite. Der Dump ist
   nur unter Linux verfügbar (`#ifndef __SWITCH__`, die Switch hat kein pwrite).
2. **Indexieren:** `tools/ue/index.py DUMPDIR` erzeugt names.pkl und objs.pkl.
3. **Erkunden:** Mit `tools/ue/ue.py` (classes, props, instances, fstring) und `tools/ue/dis.py CLASS FUNCTION`
   (UnrealScript-Bytecode). So wurden die GUI-Klassen und OnClick-Handler gefunden.
4. **Im Port:** `src/scpt/unreal.cpp` sucht Properties zur Laufzeit **über den Namen** (FindField/PropertyOffset).
   Feste Offsets kommen nur als Fallback zum Einsatz. Ein nicht gefundener Offset liefert `~0u` und muss geprüft
   werden (Absturz bei der Property `TCc`).

### GUI-Technik
- Menüs sind UE2-GUI-Pages. Die Properties des GUIController sind MenuStack 0x50, ActivePage 0x5C und
  FocusedControl 0xF8.
- **Pausemenü** `DemoMenuInGamePause`:
  - Controls-TArray bei +0x400, Buttons btnLoadGame/btnOptions/btnProfileController/btnExit (0x490–0x49C).
  - Die Buttons stehen bei WinTop 170/200/230/260.
  - Neuer Button: btnExit per StaticConstructObject mit Template klonen, WinTop + 30 setzen, per Realloc an das
    Controls-Array anhängen.
- **Eigene Seite:** Mit `OpenMenu("GUI.GUIPageSC1")` öffnen, einem leeren Rahmen, dessen B-Taste ihn selbst schließt.
  Darin den Titel und 8 Slots × 2 GUIButtonSC2 klonen (Label links, Wert rechtsbündig).
  - Labels als Klone von *Buttons*, nicht von Labels. Label-Klassen zeigten Keys mit Strichen.
  - Den Wert breit genug machen, sonst wird er abgeschnitten.
- **Texte:** Die Caption (+0x3C4) ist ein *Lokalisierungs-Key*. Der Localize-Hook beantwortet eigene Keys
  (RECOMPOPTIONS, RCPL<n>, RCPV<n>) mit `app::Translate`-Text in Großbuchstaben. So erscheint alles in der Spielschrift
  und folgt der Sprache.
- **Eingabe auf der Seite:** `game::MenuPad` aus XInputGetState übernimmt D-Pad, Stick und A mit Repeat.
  `game::FrameTick` aus D3DDevice_Swap aktualisiert Werte und den Aim-Status.
- **Zeilen:** Die Zeilen kommen aus `app::rows` (Seiten 0 und 2). Unpassende Zeilen werden in `GameHides` für `kScpt`
  ausgeblendet.
  - Die Zeile **PLAYER 1/2 nie anzeigen.** Ein Klick auf `keyboard+controller2` hat den User ausgesperrt.

## 6. Eingabe

- **Maus unter Linux:** Hat das Spiel keinen Maus-Hook, wird die Maus auf den rechten Stick gemappt (`MouseStick`,
  voller Ausschlag bei 24 Counts).
- **Gyro:** Gyro wird auf den rechten Stick gelegt. Gemessen für PT: voller Stick bei 147°/s, Deadzone 0.25, linear.
- **Gyro nur beim Zielen:** `scpt::UpdateAiming` liest den StateFrame-State des EPlayerController.
  - Ein State mit „Targeting“ oder „Sniping“ oder `s_Zooming` gilt als Zielen.
  - Die Controller-Suche läuft gedrosselt alle 30 Frames.
- **Lehre:** Spielzustände lassen sich über den Namen des aktiven UE-States sauber abfragen, ohne Hooks im Spielcode.

## 7. Performance

- Der Game-Thread ist CPU-gebunden: auf der Switch ca. 22 ms + 0.04 ms pro Draw. Der größte Hebel ist `cpu_clock = 1785`.
- Auf schnellen PCs spinnt das Spiel in seinem 1/60-Cap (0x146B1). Hohe CPU-Last dort ist kein Bug.
- Profiling am Desktop mit radv machen, **nicht unter Xvfb** (dort läuft llvmpipe, und die Zahlen sind wertlos).
- Am Ende war die Performance ok. Nicht weiter optimiert.

## 8. Diagnose-Werkzeuge

| Werkzeug | Zweck |
|---|---|
| F9 | Frame-Trace nach `dumps-<frame>`: pro Draw `step_`-Bilder, `samp_`, `tex_` (DXT als DDS), `vs_`, `vsc_`. Damit wurden Wasser und Licht gefunden. |
| F11 | Guest-Speicherdump, nur unter Linux. Grundlage für `tools/ue`. |
| `SCPT_TRACE_FILE` | Datei-Lesezugriffe mit Stack. Zeigt, welcher Code welches Asset lädt. |
| `SCPT_DSOUND_TRACE` | DSound-Buffer-Positionen und -Aufrufe. |
| `tools/xbe/vpad.py` | Virtuelles Pad: buttons, LT, RT, LS, RS, STICK. So lässt sich die Kamera ohne den User drehen. |
| `tools/xbe/xbetool.py` | Kommandos `dis`, `find`, `callers`. |
| `tools/xbe/embassy_shot.sh` | Reproduzierbarer Screenshot. |
| `tools/ue/` | UE2-Dump-Analyse (siehe Abschnitt 5). |

Screenshots macht man mit Xvfb :99 und `magick import`, **nie mit grim** (das erfasst den Desktop des Users).
Für die Switch: Der User legt Screenshots per FTP ab (ftp://192.168.178.30:5000/), Paare aus „richtig“ und „falsch“
sind am hilfreichsten.

## 9. Umgebung und Stolperfallen

- `pkill -f` immer verankern (`pkill -f '^build/linux-vk/scpt'`), sonst trifft es Fremdprozesse.
- zsh-Globs ohne Treffer brechen das ganze Kommando ab. Quoten oder `setopt nonomatch` nutzen.
- `ls` ist ein Alias mit Icons. In Skripten `\ls` nehmen.
- Der Scratchpad wird geleert. Analyse-Tools gehören sofort ins Repo (so sind `tools/ue/` neu entstanden).
- Nach Umbenennungen von CMake-Optionen (SCDA_GPU → SCPT_GPU, Default null) zeigt ein alter Build-Cache kein Bild.
  Abhilfe: `cmake -DSCPT_GPU=vk .`.
- Beim Switch-Album ist das Datum falsch (Mai 2026). Die Game-ID beim Title-Takeover ändert sich.
- Auf der Switch nur NVK/Vulkan bauen (`switch/build.sh` defaultet auf vk).

## 10. CI / Release

- **Auf scpt-nx starten Push- und Tag-Events keine Actions** (bei scct-nx schon, Ursache unbekannt). Abhilfe:
  `gh workflow run ci.yml` bzw. `gh workflow run release.yml -f tag=vX.Y.Z`.
- Das Builder-Docker-Image (Packages) bei Bedarf über den Workflow manuell bauen.
- Der Release enthält nur Quellen und Builder, **kein NRO**.
- Gearbeitet wird auf dev. Nach main nur, wenn der User es freigibt. `git push origin dev:main` hat nach Freigabe
  funktioniert.

## 11. Gesamtbild: Warum PT härter war als SCCT/SCDA

- Älteres XDK (5788): mehr Unterschiede bei Gettern und Device-Offsets.
- Die meisten Bugs waren **stilles Hardware-Verhalten**, das wir anders emuliert hatten: ungebundene Stage = 0,
  persistente Vertex-Attribute, bit-genaue EQUAL-Tiefe, Sleep-Granularität, PAL-Abhängigkeit der Sprache.
  Immer fragen: *Was hätte die echte Xbox hier still getan?*
- Das Menü ohne Overlay brauchte echtes UE2-Reverse-Engineering (Abschnitt 5). Diese Methode ist auf jedes UE2-Spiel
  übertragbar; nur die Adressen müssen neu gesucht werden (GNames/GObjects über das Muster der TArrays mit
  „None“ als Name 0).
