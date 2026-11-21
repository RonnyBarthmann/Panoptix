# Arbeitsregeln für Modul-Implementierer (intern, für den Aufbau)

* Repo: /home/claude/Panoptix. Spezifikation: docs/spec/*.txt (Anforderungskatalog, Deutsch). Datenformat: docs/DATA_FORMAT.md.
* Header unter src/**/*.h sind der VERTRAG zwischen den Modulen: bestehende Deklarationen nicht ändern; nur additiv ergänzen und im Bericht melden.
* Fasse nur Deine eigenen Dateien an. KEIN git commit/push (macht der Hauptagent).
* Code: C11, gut lesbar, strukturiert (kurze Funktionen, sprechende Namen), deutsche Kommentare, `/* SPDX-License-Identifier: GPL-3.0-or-later */` in jeder Datei, kein strcpy/strcat/sprintf (nutze core/util.h), warnungsfrei mit `-std=c11 -Wall -Wextra -Wpedantic -Isrc`.
* Kompiliere einzeln: `gcc -std=c11 -Wall -Wextra -Wpedantic -Isrc -c datei.c -o /dev/null` (Frontend: zusätzlich `$(pkg-config --cflags sdl2)`). Gesamtbuild: `make` / `make test`.
* Andere Agenten arbeiten parallel an anderen Modulen. Fehlt etwas aus fremden Modulen, kompiliere gegen den Header und prüfe später erneut.
* Bericht am Ende: maximal 15 Zeilen (Dateien, Stand, bekannte Lücken, additive Header-Änderungen).
