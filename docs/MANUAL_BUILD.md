# Manuals aktualisieren / Rebuild the manuals

Stand / edition: **7. November / 7 November 2026**.

| Sprache / language | Text | Offline HTML | PDF | Starter |
| --- | --- | --- | --- | --- |
| Deutsch | [MANUAL_DE.md](MANUAL_DE.md) | [HTML](BAX-KI-ZL_Manual_DE.html) | [PDF](BAX-KI-ZL_Manual_DE.pdf) | `Handbuch.cmd` |
| English | [MANUAL_EN.md](MANUAL_EN.md) | [HTML](BAX-KI-ZL_Manual_EN.html) | [PDF](BAX-KI-ZL_Manual_EN.pdf) | `Manual_EN.cmd` |

Beide Fassungen verwenden schwarzen Hintergrund, weißen Text, Cyan-Akzente,
Barlow und dieselben 39 Simulator-Abbildungen. Der Transparenzhinweis zur
Zusammenarbeit mit OpenAI Codex steht in beiden Fassungen am Anfang.

Both editions use black pages, white text, cyan accents, embedded Barlow fonts
and the same 39 simulator illustrations. Both include the project contribution
and AI-assistance disclosure near the beginning.

## Screenshots

Build the Release simulator first, then run from the project folder:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/capture_manual.ps1
```

The simulator's `--manual-screenshots` mode captures its real UI using isolated
settings in `build/manual-captures`. It does not change the user's configuration.
Screenshots are converted to PNG in `docs/manual-assets`. Clock and battery
values are simulated examples. For headless capture, set `SDL_VIDEODRIVER=dummy`
and `SDL_AUDIODRIVER=dummy` in the calling environment.

## HTML and PDF

Install the local Markdown dependency once if needed:

```powershell
python -m pip install --target .tools/manual-python Markdown==3.7
```

Generate both languages:

```powershell
python tools/build_manual.py
python tools/print_manual.py
```

For one language, add `--lang DE` or `--lang EN` to either command. The PowerShell
wrapper also accepts `tools/print_manual.ps1 -Lang EN`. It calls the same Python
exporter, which waits for a headless Microsoft Edge process using an isolated
profile under `build`. Diagnostics are saved as `build/manual-render-DE.log`
and `build/manual-render-EN.log`.

PDF export needs Microsoft Edge. Alternatively, print either HTML from a browser
with background graphics enabled. All pictures and fonts are embedded in the HTML;
no internet connection is needed to view it. Neither generation step flashes
hardware or changes firmware settings.

After text-only changes, regenerate HTML/PDF without taking new screenshots.
Maintain equivalent feature coverage in both language sources. Check page breaks,
matching illustration references, internal links, dates and exported PDF text.
The Companion ZIP includes both PDFs under `manuals` when it is rebuilt.

To update an existing ZIP after editing only the manuals, without rebuilding the
EXE, run `python tools/update_manual_package.py` after the PDF export. It also
refreshes the manual sources in the included source ZIP.

Barlow is distributed under the SIL Open Font License; a copy is included in
[manual-assets/Barlow-OFL.txt](manual-assets/Barlow-OFL.txt).
