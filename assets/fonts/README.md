# Barlow für beide Plattformen

Die Oberfläche verwendet **Barlow Regular**, fest als LVGL-Schrift eingebettet. Ein normaler CMake- oder ESP-IDF-Build benötigt weder Node.js noch eine lokal installierte Schrift oder den Konverter. Alle generierten C-Dateien unter `ui/fonts` werden mitgebaut.

- Original: [The Barlow Project](https://github.com/jpt/barlow), Revision `dc2940e2e04ef4ec96c07e23e0f02aefbddd343b`, Datei `fonts/ttf/Barlow-Regular.ttf`.
- Lizenz: SIL Open Font License 1.1, vollständig unter [OFL.txt](OFL.txt) beigefügt.
- Konverter: [LVGL Font Converter](https://github.com/lvgl/lv_font_conv), Version 1.5.3, Revision `2107ab79b615a6ff7051796fa9f7ae52f042437b`.
- Größen: 14, 18 bis 38 in Ein-Pixel-Schritten, 43 und 48 Pixel; 4-Bit-Antialiasing, unkomprimiert, Kerning enthalten. Zeichenvorrat: ASCII und Latin-1 (`0x20–0x7e`, `0xa0–0xff`), bei 33–38 ausschließlich Ziffern für das vergrößerte MIDI-Kanal-Wheel.
- Deklarationen: `include/ui_fonts.hpp`, mit C-Linkage für die aus C++ verwendeten C-Daten.

## Optional neu generieren

Node.js mit npm und den Fontkonverter installieren; dessen Engine-Vorgabe ist Node 24.11.1/npm 11.6.2. Der vorhandene lokale Node-20-Konverterlauf hat die Dateien ebenfalls erfolgreich erzeugt; die erzeugten Dateien wurden anschließend mit LVGL 9.2.2 kompiliert und im Simulator geprüft.

```powershell
git clone https://github.com/lvgl/lv_font_conv.git .tools/lv_font_conv
git -C .tools/lv_font_conv checkout 2107ab79b615a6ff7051796fa9f7ae52f042437b
cd .tools/lv_font_conv
npm install --omit=dev --ignore-scripts --no-audit --no-fund
cd ../..
powershell -NoProfile -ExecutionPolicy Bypass -File .\GenerateFonts.ps1
```

Die eingebettete Original-TTF muss für die Regeneration nicht erneut heruntergeladen werden. `GenerateFonts.ps1` verändert ausschließlich die generierten Schriftquellen im Projekt.

Die Größen 18–32 werden für die scrollenden Auswahlzeilen direkt gerendert, 33–38 ergänzen das größere Kanal-Wheel. Dadurch bleiben die Nachbareinträge kleiner, ohne die Texte pro Bild in eine Transformationsfläche zu zeichnen und anschließend zu skalieren. Mit `GenerateFonts.ps1 -Sizes (33..38)` lassen sich gezielt nur die Ziffernschriften neu erzeugen.
