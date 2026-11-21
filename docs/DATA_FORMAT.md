# Datenformat (data/*.tsv)

Alle Spielinhalte liegen als **TSV** (Tab-getrennt, UTF-8) in `data/`. Das ist der
einfachste Einstieg für Mitwirkende: eine Tabelle in LibreOffice/Excel/Editor öffnen,
Zeile ändern oder ergänzen, `make data-check` ausführen – fertig, kein C-Wissen nötig.

Regeln:

* Zeilen mit `#` und Leerzeilen werden ignoriert. Die erste übrige Zeile ist die Kopfzeile.
* Spalten werden **über ihren Namen** gefunden (Reihenfolge egal, fehlende optionale Spalten = leer/0).
* Verweise auf andere Tabellen laufen über die `id` (String). Der Lader löst sie in Array-Indizes auf.
* Listenfelder: Komma getrennt (`flags`, `tags`, `abilities`); Stat-Listen: `name=wert;name=wert`.
* Zahlen mit Punkt als Dezimaltrenner. Prozentwerte als Prozentzahl (`5.0` = 5 %).

## Enum-Namen

| Feld | Werte |
|------|-------|
| tier | `normal, magic, rare, unique, boss, legendary` |
| class | `knight, warrior, wizard, archer, assassin, shaman, priest`, zusätzlich `common` (Skills) / `any` (Items) |
| item kind | `weapon, sub, armor, ring, necklace, wings, bag, potion, material, stone, special` |
| slot | `weapon, sub, helm, chest, gloves, boots, bag, necklace, ring, potion, wings` (`ring` = Ring-Slots, `potion` = Trank-Slots) |
| potion_type | `heal, mana, haste, iron, frenzy` |
| material_line | `metal, wood, cloth, crystal, essence` |
| stone_kind | `enh_weapon, enh_armor, enh_jewel, enchant, magic` (bei `magic`: `value` = Aktnummer 1..11) |
| skill kind | `strike, circle, cone, projectile, ground, dash, buff_self, buff_party, debuff, heal, heal_party, hot, totem, mana, cleanse, pull, warp` |
| skill flags | `dash_fwd, dash_back, dash_side, slow, stun, taunt, heal_self, homing, move_while_cast, spell, dot, party_heal, mana_back, teleport, no_combat_req` |
| skill tags | `single, aoe, multi, dot, utility, buff, debuff, heal, totem` |
| totem | `none, turtle, storm_dragon, red_dragon, war_drum, clairvoyance, gale_hawk` |
| atk_style | `melee, projectile, homing, dash_stab` |
| dmg_type | `phys, spell, hybrid` |
| main_stat | `str, dex, int` |
| monster arch | `melee, ranged, swarm, brute, hunter, summoner, healer, explosive, elemental, guardian, totem, demon, boss` |
| monster abilities | `nova, slam, cone, line, summon, pools, charge, heal_ally, shield, enrage, rain` |
| map kind | `story, temple, ruins` |
| shop tab | `free, diamonds, random, general, books, raven` |
| shop kind | `item, random_ring, random_necklace, random_book, book_class, slots_inv, slots_hero, currency, wings, chest` |
| currency | `gold, diamonds, credits, raven_coins, raven_feathers` |
| period | `none, daily, weekly, monthly` |
| quest kind | `kill_any, kill_tier, clear_map, craft, open_chests, kill_arch, enhance, reach_level` |
| legend behavior | `none, explosive_trail, arcane_emission, march_of_light, rolling_steel, bloodrage, executioner, totem_echo, chain_lightning` |

### Stat-Namen (für `mods`, `stat`, `stat2`)

`dmg_flat, attack_flat, spell_flat, attack_pct, spell_pct, more_attack, more_spell, crit, crit_dmg,
atk_speed, cast_speed, move_speed, cdr, armor, armor_pct, spellres_pct, hp, hp_pct, mana, mana_pct,
hp_regen, mana_regen, chest_cap, dr_attack, dr_spell, dmg_reduce, projectiles, pierce, heal_pct,
enemy_taken, enemy_atk_down, enemy_spell_down, mana_cost, cdr_cap`  (Bedeutung: `src/core/types.h`, enum `Stat`)

## Dateien und Spalten

### tiers.tsv (genau 6 Zeilen, Reihenfolge = Tier-Enum)
`id name color drop_one_in item_mult exp_mult hp_mult dmg_mult spawn_weight chest_items monster_rolls potion_charges sell_base sell_per_level symbol`
(`color` als `#RRGGBB`)

### classes.tsv (genau 7 Zeilen, Reihenfolge = ClassId)
`id name role race desc weapon_name sub_name basic_attack color atk_style dmg_type ranged main_stat base_speed base_range proj_speed move_factor base_armor auto_str auto_dex auto_int basic_dmg_pct`

### skills.tsv
`id name class grade tags kind flags cooldown mana range radius cast_time dmg_pct count angle pierce max_hits speed dur delay stat stat_pct stat2 stat2_pct totem ai_priority ai_tier_mask ai_min_targets ai_max_dist ai_self_hp_below ai_ally_hp_below ai_only_in_combat ai_allow_stack desc`
(`ai_tier_mask` als Dezimalzahl, 63 = alle Tiers)

### items.tsv (alle Gegenstände in EINER Tabelle)
`id name kind tier level slot class value mods duration potion_type charges material_line stone_kind family set craftable enchantable legendary index`
* `value`: Waffe = Waffenschaden, Sub = Hauptwert, Rüstung = Rüstung, Trank = Wirkung in %, sonst 0.
* `duration`: Trank-Effektdauer in s.
* `set`: Set-ID (Zahl aus sets.tsv) oder leer. `index`: Position 1..21 in der Slot-Reihe.
* `legendary`: `knight:3;warrior:7;...` = Klasse:Effekt-ID (nur Astral-Teile).

### legendary.tsv (28 Zeilen)
`id name class slot mods behavior desc`

### sets.tsv
`id name mods2 mods4 behavior4 desc2 desc4`

### monsters.tsv
`id name arch act_min act_max hp_f dmg_f speed range radius attack_cd proj_speed color shape abilities boss elite_only weight`

### maps.tsv
`id name kind act index chapter level w h seed boss boss_map max_heroes min_player_level density stone stone_count tint`
(`boss` = Monster-ID oder leer, `stone` = Item-ID oder leer)

### shop.tsv
`id name desc tab kind item qty currency price limit period price_growth min_player_level`

### traits.tsv
`id name desc tree max_points effect per_point row requires`
(`effect` = Feldname aus `CommanderMods` in `src/rules/rules.h`)

### quests.tsv
`id text kind param count_min count_max main reward_cmd_exp reward_gold reward_diamonds`
(`text` enthält `{n}` als Platzhalter für die Zielzahl)

### balance.cfg
Zeilen `schluessel = wert`, `#` = Kommentar. Schlüssel siehe `BALANCE_FIELDS` in `src/data/data.h`.

### lang/<code>.tsv
Spalten `de` und `en`: deutscher Quelltext → Übersetzung (siehe `tr()` in `src/core/i18n.h`).

### maps/<map-id>.map
Handgebaute Karten (optional, überschreibt den Generator) – ASCII-Raster:
`#` Wand · `.` Boden · `~` Wasser · `:` Dämpfungsboden · `h` Heilquelle · `S` Start · `B` Boss-Arena-Mitte ·
`X` Ausgang · `M` Monsterpack-Punkt · `1`–`9` Wegpunkte Hauptpfad · `a`–`f` Wegpunkte Nebenpfade.
