==============================================================================
ANFORDERUNGSKATALOG - PANOPTIX (Idle-RPG-Manager im Stil von PANOPTYCA, Open-Source-Nachbau)
Stand: 20.11.2026 | Sprache: Deutsch | Format: TXT (UTF-8)
==============================================================================

PROJEKT-ECKDATEN (vom Auftraggeber entschieden)
 Spielname: Panoptix | Lizenz: GPL-3.0-or-later | Tech-Stack: C (C11) + SDL3 (SDL2 als Fallback) | reines Offline-Spiel (kein Netzwerk, keine Konten, keine APIs)
 Offline-Fortschritt: ausschliesslich ueber den Kommandoposten (20) | Legendaere Klasseneffekte: universelle Astral-Teile mit klassenabhaengigem Effekt (08)
 Kein Echtgeld-Shop. Kein Treffergenauigkeits-/Ausweichsystem; stattdessen kritische Trefferchance (03).

ZWECK
 Dieses Dateiset beschreibt das Spiel so, dass es (per Vibe-Coding und spaeter community-getrieben) komplett nachgebaut werden kann.
 Die Stichpunkte des Auftraggebers haben VORRANG vor dem Original; wo beide abweichen, steht es in 22 (Abschnitt 3).

LESEREIHENFOLGE / DATEIEN
 00 Uebersicht (diese Datei)
 01 Spielkonzept & Kernschleife
 02 Tier-System & Farbschema (roter Faden)
 03 Progression, Level, Stats, Formeln
 04 Heldenklassen (7)
 05 Skills - allgemeine Funktionsweise & KI-Bedingungen
 06 Skills pro Klasse (~97)
 07 Waffen (147) & Sekundaerwaffen (42)         [generierte Tabellen]
 08 Ruestung (84 Teile) & legendaere Effekte      [generierte Tabellen]
 09 Traenke (30, wiederbefuellbar)
 10 Ringe, Halsketten, Fluegel, Tragetaschen
 11 Kisten, Loot, Materialien, Inventar
 12 Crafting, Verbessern (+0..+20), Verzaubern, Umwandeln
 13 Shop & Waehrungen
 14 Maps, Kapitel, Akte, Tempel, Ruinen
 15 Gegner, Monster-Tiers, Bosse
 16 Helden-KI
 17 Gegner-KI
 18 UI / Benutzeroberflaechen (Wireframes)
 19 Grafikstil, Assets, Audio
 20 Kommandeur-System (Traits, Buffs, Kommandoposten, Quests)
 21 Technik, Architektur, Datenmodell, Lizenz
 22 Roadmap, Abweichungen, Quellenlage, offene Entscheidungen

SCHNELLUEBERSICHT KERNZAHLEN
 Helden: 4 Start-Slots -> 16 (Spieler-Level) | Klassen: 7 | Slots je Held: 14 (Waffe, Sekundaer, Helm, Brust, Handschuhe, Stiefel, Tasche, Halskette, 2 Ringe, 3 Traenke, Fluegel)
 Tiers: Normal(grau) / Magisch(blau) / Selten(gelb) / Einzigartig(orange) / Boss(rot) / Legendaer(rosa); Kistenchancen 1/10 .. 1/1.000.000
 Waffen 7x21=147 | Ruestung 4x21=84 | Traenke 5x6=30 | Ringe 24 | Halsketten 24 | Fluegel 6 | Taschen 6 | Sekundaerwaffen 42
 Kistenkapazitaet 10 -> 70 | Verbessern +0..+20 | Skill-Level 1..20, 4 Skill-Slots | Heldenlevel 1..100

KENNZEICHNUNG
 [ANNAHME]   = von mir ergaenzt, weil Vorgabe/Quelle fehlte.   Zahlen = Startwerte, balancierbar.
 Quellen: Steam-Store, Steam-News/Patchnotes, Steam-Community-Guides, Community-Wiki-Snippets (Details & Luecken in 22).

WICHTIGE EHRLICHE HINWEISE
 - Fandom-Wiki und Community-Archiv waren nicht vollstaendig abrufbar; Zahlen zu Items/Monstern/Maps des Originals sind daher eigene Startwerte.
 - Keine Screenshots/Videos ausgewertet; UI-Layouts sind aus Text abgeleitet. Eigene Screenshots verbessern Kapitel 18.
 - Namen/Assets des Originals fuer die Veroeffentlichung ersetzen (Recht, 21 Abschnitt 8).
