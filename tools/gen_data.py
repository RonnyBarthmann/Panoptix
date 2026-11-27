#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Erzeugt alle Datendateien in data/ (TSV) aus der Spezifikation (docs/spec) und
eingebetteten Tabellen. Aufruf aus dem Repo-Wurzelverzeichnis:

    python3 tools/gen_data.py

Das Skript ist reproduzierbar: gleiche Eingaben => gleiche Dateien. Wer Daten
nur anpassen will, kann die erzeugten data/*.tsv auch direkt editieren; wer
Formeln/Strukturen aendert, aendert dieses Skript und generiert neu.
Spaltennamen/Enum-Namen: siehe docs/DATA_FORMAT.md.
"""
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = os.path.join(ROOT, "docs", "spec")
OUT = os.path.join(ROOT, "data")

# ---------------------------------------------------------------------------
# Hilfsfunktionen
# ---------------------------------------------------------------------------
TIERS = ["normal", "magic", "rare", "unique", "boss", "legendary"]
TIER_NAMES = ["Normal", "Magisch", "Selten", "Einzigartig", "Boss", "Legendär"]
TIER_FROM_SPEC = {"Normal": 0, "Magisch": 1, "Selten": 2, "Einzigartig": 3, "Boss": 4, "Legendaer": 5}
TIER_MULT = [1.00, 1.12, 1.25, 1.42, 1.60, 1.85]
CLASSES = ["knight", "warrior", "wizard", "archer", "assassin", "shaman", "priest"]
CLASS_NAMES = ["Ritter", "Krieger", "Magier", "Bogenschütze", "Assassine", "Schamane", "Priester"]
CLASS_FROM_SPEC = {"Ritter": 0, "Krieger": 1, "Magier": 2, "Bogenschuetze": 3, "Assassine": 4,
                   "Schamane": 5, "Priester": 6}

# ae/oe/ue in Namen aus der ASCII-Spezifikation -> Umlaute (nur bekannte Woerter)
UMLAUT_WORDS = {
    "Waechter": "Wächter", "waechter": "wächter", "Zweihaender": "Zweihänder", "Aeltesten": "Ältesten",
    "Aschekoenig": "Aschekönig", "Loewenherz": "Löwenherz", "Koecher": "Köcher", "koecher": "köcher",
    "Waldlaeufer": "Waldläufer", "Holzfaeller": "Holzfäller", "Kuechenmesser": "Küchenmesser",
    "Schattenjaeger": "Schattenjäger", "Schildkroeten": "Schildkröten", "Schlaechter": "Schlächter",
    "Singularitaet": "Singularität", "Uebungs": "Übungs", "Aegis": "Ägis", "Fluegel": "Flügel",
    "Traenke": "Tränke", "Gnadenzepter": "Gnadenzepter", "Goettlich": "Göttlich", "Gluehend": "Glühend",
    "Daemon": "Dämon", "Raeuber": "Räuber", "Hauptling": "Häuptling",
}


def umlaut(s):
    for k, v in UMLAUT_WORDS.items():
        s = s.replace(k, v)
    return s


def fmt(v):
    """Zahl kompakt formatieren."""
    if isinstance(v, float):
        if abs(v - round(v)) < 1e-9:
            return str(int(round(v)))
        return ("%.4f" % v).rstrip("0").rstrip(".")
    return str(v)


LIMITS = {"id": 40, "name": 48, "desc": 200, "text": 200, "desc2": 200, "desc4": 200}


def write_tsv(name, header, rows, comment=None):
    path = os.path.join(OUT, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("# Panoptix %s - erzeugt von tools/gen_data.py (Format: docs/DATA_FORMAT.md)\n" % name)
        if comment:
            for line in comment.splitlines():
                f.write("# " + line + "\n")
        f.write("\t".join(header) + "\n")
        for r in rows:
            cells = []
            for h in header:
                v = r.get(h, "")
                cells.append(fmt(v) if not isinstance(v, str) else v)
            for h, c in zip(header, cells):
                assert "\t" not in c and "\n" not in c, (name, c)
                lim = LIMITS.get(h)
                if lim and not name.startswith("lang/"):
                    assert len(c.encode("utf-8")) < lim, (name, h, c)
            f.write("\t".join(cells) + "\n")
    print("%-18s %4d Zeilen" % (name, len(rows)))


def mods(**kw):
    """Stat-Liste als 'name=wert;name=wert'."""
    return ";".join("%s=%s" % (k, fmt(float(v))) for k, v in kw.items() if v)


def merge_mods(*parts):
    d = {}
    for p in parts:
        for kv in (p or "").split(";"):
            if "=" in kv:
                k, v = kv.split("=")
                d[k] = d.get(k, 0.0) + float(v)
    return ";".join("%s=%s" % (k, fmt(v)) for k, v in d.items() if v)


# ---------------------------------------------------------------------------
# tiers.tsv / classes.tsv
# ---------------------------------------------------------------------------
def gen_tiers():
    cols = ["#9AA0A6", "#4A90E2", "#F2C94C", "#F2994A", "#E5484D", "#FF6FB5"]
    rows = []
    for i, t in enumerate(TIERS):
        rows.append(dict(
            id=t, name=TIER_NAMES[i], color=cols[i],
            drop_one_in=[10, 100, 1000, 10000, 100000, 1000000][i],
            item_mult=TIER_MULT[i], exp_mult=[1, 2.5, 6, 15, 40, 120][i],
            hp_mult=[1, 3, 8, 20, 18, 150][i], dmg_mult=[1, 1.3, 1.7, 2.2, 1.8, 4][i],
            spawn_weight=[85, 11, 3, 0.9, 0.1, 0.01][i],
            chest_items=[3, 4, 5, 6, 8, 10][i], monster_rolls=[1, 3, 10, 30, 100, 300][i],
            potion_charges=[2, 3, 4, 5, 6, 8][i],
            sell_base=[5, 50, 500, 5000, 50000, 500000][i],
            sell_per_level=[1, 10, 100, 1000, 10000, 100000][i], symbol=i))
    write_tsv("tiers.tsv", ["id", "name", "color", "drop_one_in", "item_mult", "exp_mult", "hp_mult",
                            "dmg_mult", "spawn_weight", "chest_items", "monster_rolls",
                            "potion_charges", "sell_base", "sell_per_level", "symbol"], rows)


def gen_classes():
    C = [
        ("knight", "Ritter", "Tank / physischer DPS", "Mensch",
         "Sehr robuster Frontkämpfer mit hoher Basisrüstung. Zieht Gegner an sich.",
         "Schwert", "Schild", "Schwerthieb", "#5E8CA8", "melee", "phys", 0, "str", 1.0, 90, 0, 0.95, 2, 60, 20, 20, 100),
        ("warrior", "Krieger", "Frontkämpfer, Support/DPS", "Ork", "Mehr Schaden, weniger Haltbarkeit. Gruppen-Buffs und Flächenhiebe.",
         "Axt", "Kampfbanner", "Axthieb", "#9E2B25", "melee", "phys", 0, "str", 0.9, 100, 0, 1.0, 1, 60, 20, 20, 100),
        ("wizard", "Magier", "Flächenmagie / Burst", "Mensch", "Verheerende Flächenzauber und Flüche. Zerbrechlich.",
         "Stab", "Orb", "Magiegeschoss", "#7D5BBE", "projectile", "spell", 1, "int", 0.8, 850, 700, 1.0, 1, 15, 10, 75, 100),
        ("archer", "Bogenschütze", "Einzelziel-DPS, hohes Tempo", "Elf", "Höchstes Tempo, aber zerbrechlich. Pfeile fliegen weit.",
         "Bogen", "Köcher", "Pfeil", "#5C7A3A", "projectile", "phys", 1, "dex", 1.3, 1500, 1100, 1.1, 1, 25, 60, 15, 90),
        ("assassin", "Assassine", "Burst, Mobilität, Gift", "Dunkelelf", "Dash-Angriffe, Krit und Gift. Zwei Builds: Angriff oder Gift.",
         "Dolch", "Wurfmessergurt", "Dolchstich", "#4A3A5A", "dash_stab", "hybrid", 0, "dex", 1.6, 70, 0, 1.1, 1, 35, 55, 10, 70),
        ("shaman", "Schamane", "Totems, Debuffs, Support", "Ork", "Beschwört Totems, schwächt Gegner, unterstützt das Team.",
         "Totemstab", "Totem", "Geisterblitz", "#2F8F8A", "projectile", "spell", 1, "int", 0.9, 650, 650, 0.95, 1, 15, 10, 75, 100),
        ("priest", "Priester", "Heiler, Buffer, leichter DPS", "Mensch", "Heilt und stärkt Verbündete. Allein schwach, im Team stark.",
         "Zepter", "Gebetbuch", "Lichtgeschoss", "#F4ECD6", "homing", "spell", 1, "int", 1.0, 700, 500, 1.0, 1, 30, 20, 50, 100),
    ]
    rows = []
    for c in C:
        r = dict(zip(["id", "name", "role", "race", "desc", "weapon_name", "sub_name", "basic_attack", "color",
                      "atk_style", "dmg_type", "ranged", "main_stat", "base_speed", "base_range", "proj_speed",
                      "move_factor", "base_armor", "auto_str", "auto_dex", "auto_int", "basic_dmg_pct"], c))
        rows.append(r)
    write_tsv("classes.tsv", list(rows[0].keys()), rows)


# ---------------------------------------------------------------------------
# skills.tsv
# ---------------------------------------------------------------------------
SKILL_COLS = ["id", "name", "class", "grade", "tags", "kind", "flags", "cooldown", "mana", "range", "radius",
              "cast_time", "dmg_pct", "count", "angle", "pierce", "max_hits", "speed", "dur", "delay", "stat",
              "stat_pct", "stat2", "stat2_pct", "totem", "ai_priority", "ai_tier_mask", "ai_min_targets",
              "ai_max_dist", "ai_self_hp_below", "ai_ally_hp_below", "ai_only_in_combat", "ai_allow_stack", "desc"]

SKILLS = []


def S(id, name, cls, grade, kind, cd, mana, desc, tags="", flags="", rng=0, rad=0, cast=0.4, dmg=0, count=1,
      angle=0, pierce=0, max_hits=0, speed=0, dur=0, delay=0, stat="", pct=0, stat2="", pct2=0, totem="",
      prio=None, minT=1, maxd=0, selfhp=0, allyhp=0, combat=1, stack=0):
    if not tags:
        t = {"strike": "single", "circle": "aoe", "cone": "aoe", "projectile": "single", "ground": "aoe",
             "dash": "utility", "buff_self": "buff", "buff_party": "buff", "debuff": "debuff", "heal": "heal",
             "heal_party": "heal", "hot": "heal", "totem": "totem", "mana": "utility", "cleanse": "heal",
             "pull": "utility", "warp": "utility,heal"}[kind]
        tags = t
    if prio is None:
        prio = {"heal": 0, "heal_party": 0, "hot": 0, "cleanse": 0, "warp": 0, "buff_self": 1, "buff_party": 1,
                "totem": 1, "mana": 1, "debuff": 2, "dash": 2, "pull": 2, "strike": 3, "projectile": 3,
                "circle": 4, "cone": 4, "ground": 4}[kind]
    SKILLS.append(dict(id=id, name=name, **{"class": cls}, grade=grade, tags=tags, kind=kind, flags=flags,
                       cooldown=cd, mana=mana, range=rng, radius=rad, cast_time=cast, dmg_pct=dmg, count=count,
                       angle=angle, pierce=pierce, max_hits=max_hits, speed=speed, dur=dur, delay=delay, stat=stat,
                       stat_pct=pct, stat2=stat2, stat2_pct=pct2, totem=totem or ("none" if kind != "totem" else ""),
                       ai_priority=prio, ai_tier_mask=63, ai_min_targets=minT, ai_max_dist=maxd,
                       ai_self_hp_below=selfhp, ai_ally_hp_below=allyhp, ai_only_in_combat=combat,
                       ai_allow_stack=stack, desc=desc))


def gen_skills():
    SKILLS.clear()
    # ---- Ritter (11) ----
    k = "knight"
    S("knight_double_strike", "Doppelhieb", k, 0, "strike", 3, 10, "Zwei schnelle Schwerthiebe auf ein Ziel.", rng=110, dmg=300, count=2)
    S("knight_vital_strike", "Vitalstoß", k, 1, "strike", 4, 14, "Verwundet das Ziel: Schaden über 4 s (stapelt nicht).", tags="single,dot", flags="dot", rng=110, dmg=220, dur=4)
    S("knight_spin_slash", "Wirbelhieb", k, 0, "circle", 5, 16, "Rundumschlag um den Ritter.", rad=130, dmg=380)
    S("knight_soul_slash", "Seelenschlag", k, 1, "circle", 6, 20, "Größerer Rundumschlag.", rad=160, dmg=450)
    S("knight_hurricane_strike", "Hurrikanhieb", k, 2, "projectile", 6, 22, "Schleudert Klingenwellen im Fächer.", rng=500, dmg=520, count=5, angle=60, pierce=2, speed=600, tags="aoe,multi")
    S("knight_full_moon_slash", "Vollmondschnitt", k, 2, "circle", 7, 26, "Mächtiger Rundumschlag, heilt den Ritter.", tags="aoe,heal", flags="heal_self", rad=190, dmg=641)
    S("knight_reckless_charge", "Rücksichtsloser Ansturm", k, 1, "dash", 9, 12, "Stürmt auf den Gegner zu und spottet kurz.", flags="dash_fwd,taunt", rng=450, rad=90, dur=3, minT=1, maxd=450)
    S("knight_step_strike", "Schrittstoß", k, 2, "dash", 5, 14, "Dash zum Ziel mit Flächenschlag.", tags="utility,aoe", flags="dash_fwd", rng=500, rad=120, dmg=737, minT=2, maxd=500)
    S("knight_moment_enlightenment", "Moment der Erleuchtung", k, 3, "buff_self", 40, 30, "Senkt Abklingzeiten (und erhöht die CDR-Grenze) für 40 s.", dur=40, stat="cdr", pct=20, stat2="cdr_cap", pct2=5, combat=0)
    S("knight_firm_stance", "Standhaftigkeit", k, 1, "buff_self", 40, 20, "Mehr Rüstung für 40 s.", dur=40, stat="armor_pct", pct=30, combat=1)
    S("knight_iron_will", "Eiserner Wille", k, 2, "buff_self", 45, 24, "Erlittener Schaden sinkt um 20 % für 40 s.", dur=40, stat="dmg_reduce", pct=20)
    # ---- Krieger (13) ----
    k = "warrior"
    S("warrior_slash", "Hieb", k, 0, "strike", 3, 8, "Schneller Axthieb auf ein Ziel.", rng=120, dmg=320)
    S("warrior_berserk_crush", "Berserker-Zermalmung", k, 2, "strike", 6, 18, "Wuchtiger Einzelschlag.", rng=120, dmg=560)
    S("warrior_power_slash", "Kraftschlag", k, 1, "cone", 5, 14, "Breiter Hieb im Bogen.", rng=150, angle=120, dmg=400)
    S("warrior_whirlwind", "Wirbelwind", k, 2, "circle", 6, 28, "Dreht sich mit der Axt (3 Treffer), Bewegung möglich.", flags="move_while_cast", rad=150, dmg=450, count=3, cast=0.8)
    S("warrior_earth_crusher", "Erdzerstörer", k, 3, "ground", 9, 30, "Schlägt die Erde: verheerender Flächenschaden.", rng=300, rad=220, dmg=700, delay=0.5)
    S("warrior_swipe", "Seitenhieb", k, 0, "dash", 4, 8, "Dash mit leichtem Treffer.", flags="dash_fwd", rng=300, rad=80, dmg=150, maxd=300)
    S("warrior_heavy_hook", "Schwerer Haken", k, 1, "pull", 7, 12, "Zieht ein Ziel heran.", rng=500, dmg=100, maxd=500)
    S("warrior_small_quake", "Kleines Beben", k, 1, "ground", 9, 16, "Beben mit Verlangsamung.", flags="slow", rng=250, rad=200, dmg=480, delay=0.3, dur=2)
    S("warrior_leap_attack", "Sprungangriff", k, 1, "dash", 6, 14, "Springt zum Ziel und schlägt zu.", tags="utility,aoe", flags="dash_fwd", rng=450, rad=140, dmg=430, maxd=450)
    S("warrior_battle_cry", "Schlachtruf", k, 2, "buff_party", 40, 24, "Gruppe: +25 % mehr Angriffsschaden.", dur=30, stat="more_attack", pct=25, combat=0)
    S("warrior_berserk_cry", "Berserkerruf", k, 2, "buff_party", 40, 24, "Gruppe: +20 % Angriffstempo.", dur=30, stat="atk_speed", pct=20, combat=0)
    S("warrior_last_bastion", "Letzte Bastion", k, 3, "buff_self", 60, 30, "Halbiert erlittenen Schaden, aber der Krieger bleibt stehen (nicht empfohlen).", dur=15, stat="dmg_reduce", pct=50, stat2="armor_pct", pct2=50)
    S("warrior_intimidating_cry", "Einschüchternder Schrei", k, 2, "debuff", 35, 22, "Gegner verursachen 25 % weniger Angriffsschaden.", rad=400, dur=20, stat="enemy_atk_down", pct=25, minT=2)
    # ---- Magier (13) ----
    k = "wizard"
    S("wizard_fireball", "Feuerball", k, 0, "projectile", 4, 14, "Explodierender Feuerball.", flags="spell", tags="aoe", rng=800, rad=100, dmg=380, speed=650)
    S("wizard_magic_missile", "Magisches Geschoss", k, 0, "projectile", 3, 12, "Fünf zielsuchende Geschosse (max. 3 auf dasselbe Ziel).", flags="spell,homing", tags="multi", rng=800, dmg=120, count=5, angle=40, max_hits=3, speed=550)
    S("wizard_inferno", "Inferno", k, 1, "ground", 6, 24, "Flammensäule am Ziel.", flags="spell", rng=700, rad=180, dmg=500, delay=0.4)
    S("wizard_fireburst", "Feuerbrunst", k, 1, "ground", 5, 22, "Feuerexplosion am Ziel.", flags="spell", rng=700, rad=150, dmg=470, delay=0.3)
    S("wizard_aqua_ball", "Wasserball", k, 1, "projectile", 5, 22, "Wasserkugel, verlangsamt Gegner.", flags="spell,slow", tags="aoe", rng=800, rad=110, dmg=460, speed=600, dur=2)
    S("wizard_disintegrate", "Desintegration", k, 2, "strike", 6, 30, "Vernichtet ein einzelnes Ziel.", flags="spell", rng=800, dmg=900)
    S("wizard_meteor_strike", "Meteoreinschlag", k, 2, "ground", 8, 36, "Meteor schlägt verzögert ein.", flags="spell", rng=800, rad=220, dmg=1100, delay=1.0, cast=0.6)
    S("wizard_nuclear_ball", "Nuklearball", k, 3, "projectile", 7, 40, "Riesige Energiekugel mit großem Radius.", flags="spell", tags="aoe", rng=800, rad=300, dmg=1000, speed=400, cast=0.6)
    S("wizard_teleport", "Teleport", k, 1, "dash", 6, 14, "Kurzer Sprung nach vorn.", flags="dash_fwd,teleport", rng=350, combat=0, minT=0)
    S("wizard_focus", "Fokus", k, 1, "buff_self", 40, 20, "+15 % Abklingzeitreduktion für 40 s.", dur=40, stat="cdr", pct=15, combat=0)
    S("wizard_ancient_one", "Der Uralte", k, 3, "buff_self", 60, 40, "+30 % Zauberschaden und +30 % Rüstung für 30 s.", dur=30, stat="spell_pct", pct=30, stat2="armor_pct", pct2=30)
    S("wizard_curse_weakness", "Fluch der Schwäche", k, 1, "debuff", 20, 20, "Gegner verursachen 25 % weniger Angriffsschaden.", rng=700, rad=350, dur=15, stat="enemy_atk_down", pct=25, minT=2)
    S("wizard_curse_frailty", "Fluch der Gebrechlichkeit", k, 2, "debuff", 20, 20, "Gegner verursachen 25 % weniger Zauberschaden.", rng=700, rad=350, dur=15, stat="enemy_spell_down", pct=25, minT=2)
    # ---- Bogenschuetze (12) ----
    k = "archer"
    S("archer_bleeding_arrow", "Blutender Pfeil", k, 1, "projectile", 5, 12, "Pfeil, der 4 s lang blutet (Abklingzeit ≥ 3 s einstellen).", tags="single,dot", flags="dot", rng=1200, dmg=200, dur=4, speed=1100)
    S("archer_rapid_fire", "Schnellfeuer", k, 2, "projectile", 4, 20, "Salve aus vielen Pfeilen in einer Linie.", rng=1200, dmg=250, count=8, angle=6, pierce=2, speed=1200, cast=0.6)
    S("archer_split_arrow", "Spaltpfeil", k, 1, "projectile", 5, 16, "Pfeil teilt sich in drei.", tags="aoe", rng=1200, dmg=300, count=3, angle=30, speed=1100)
    S("archer_explosion_arrow", "Explosionspfeil", k, 2, "projectile", 6, 22, "Pfeil explodiert beim Treffer (mit Durchdringung: Schrotflinte).", tags="aoe", rng=1200, rad=140, dmg=520, pierce=1, speed=1000)
    S("archer_piercing_arrow", "Durchschlagender Pfeil", k, 1, "projectile", 3, 14, "Durchbohrt ganze Reihen.", tags="aoe", rng=1300, dmg=350, pierce=99, speed=1300)
    S("archer_multi_shot", "Mehrfachschuss", k, 1, "projectile", 4, 18, "Fünf Pfeile im Fächer (Winkel einstellbar).", tags="multi", rng=1200, dmg=250, count=5, angle=50, speed=1100)
    S("archer_stun_arrow", "Betäubungspfeil", k, 1, "projectile", 8, 18, "Betäubt und verlangsamt das Ziel.", tags="utility,single", flags="stun,slow", rng=1200, dmg=200, dur=1.5, speed=1100)
    S("archer_step_shot", "Schrittschuss", k, 1, "projectile", 6, 14, "Schuss mit Sprung nach vorn.", tags="utility", flags="dash_fwd", rng=1000, dmg=300, speed=1100)
    S("archer_backstep_shot", "Rückschuss", k, 1, "projectile", 6, 14, "Schuss mit Sprung zurück.", tags="utility", flags="dash_back", rng=1000, dmg=300, speed=1100)
    S("archer_eagle_eye", "Adlerauge", k, 2, "buff_self", 40, 24, "+15 % Krit-Chance, +30 % Krit-Schaden für 39 s.", dur=39, stat="crit", pct=15, stat2="crit_dmg", pct2=30, combat=0)
    S("archer_legendary_archer", "Legendärer Bogenschütze", k, 3, "buff_self", 60, 30, "+2 Projektile für 30 s.", dur=30, stat="projectiles", pct=2, combat=0)
    S("archer_firm_grip", "Fester Griff", k, 2, "buff_self", 40, 24, "+2 Durchdringung, +25 % Krit-Schaden für 39 s.", dur=39, stat="pierce", pct=2, stat2="crit_dmg", pct2=25, combat=0)
    # ---- Priester (12) ----
    k = "priest"
    S("priest_divine_strike", "Göttlicher Schlag", k, 1, "ground", 2, 10, "Lichtstrahl am Ziel.", flags="spell", rng=700, rad=120, dmg=330)
    S("priest_purifying_flame", "Reinigende Flamme", k, 2, "ground", 7, 26, "Flammen am Ziel, heilen die Gruppe.", tags="aoe,heal", flags="spell,party_heal", rng=700, rad=170, dmg=480, delay=0.3)
    S("priest_cure", "Heilung", k, 0, "cleanse", 8, 14, "Entfernt Debuffs und heilt etwas.", dmg=150, allyhp=90, combat=0)
    S("priest_mind_cure", "Geistheilung", k, 1, "mana", 10, 0, "Stellt 30 % Mana des Priesters wieder her.", dmg=30, combat=0)
    S("priest_heal", "Heilen", k, 1, "heal", 5, 16, "Heilt den Verbündeten mit den niedrigsten HP.", dmg=350, allyhp=75, combat=0)
    S("priest_mind_heal", "Geist-Heilen", k, 2, "mana", 8, 12, "Überträgt Mana auf Verbündete (Mana-Batterie).", flags="party_heal", dmg=25, combat=0)
    S("priest_divine_bless", "Göttlicher Segen", k, 2, "hot", 10, 22, "Heilung über 10 s auf ein Ziel.", dur=10, dmg=600, allyhp=100, combat=0)
    S("priest_recovery", "Genesung", k, 1, "heal_party", 6, 24, "Heilt die ganze Gruppe.", dmg=250, allyhp=75, combat=0)
    S("priest_divine_grace", "Göttliche Gnade", k, 2, "buff_party", 40, 24, "Gruppe: 15 % weniger erlittener Schaden.", dur=30, stat="dmg_reduce", pct=15, combat=0)
    S("priest_enchant_vision", "Verzauberte Sicht", k, 2, "buff_self", 40, 22, "+1 Projektil für 30 s.", dur=30, stat="projectiles", pct=1, combat=0)
    S("priest_exaltation_fury", "Erhebung der Wut", k, 3, "buff_party", 60, 36, "Gruppe: +20 % Angriffs- und Zauberschaden.", dur=30, stat="more_attack", pct=20, stat2="more_spell", pct2=20, combat=0)
    S("priest_exaltation_swift", "Erhebung der Eile", k, 3, "buff_party", 60, 36, "Gruppe: +15 % Angriffs- und Lauftempo.", dur=30, stat="atk_speed", pct=15, stat2="move_speed", pct2=15, combat=0)
    # ---- Assassine (14) ----
    k = "assassin"
    S("assassin_vital_thrust", "Vitalstich", k, 2, "strike", 5, 20, "Einzelziel-Burst.", rng=100, dmg=1200)
    S("assassin_dagger_throw", "Dolchwurf", k, 0, "projectile", 2, 8, "Drei Dolche (max. 1 Treffer je Ziel).", tags="aoe", rng=700, dmg=250, count=3, angle=20, max_hits=1, speed=1000)
    S("assassin_knife_trace", "Messerspur", k, 1, "cone", 4, 12, "Kurzer Klingenfächer.", rng=150, angle=90, dmg=400)
    S("assassin_emergency_evasion", "Notausweichen", k, 1, "dash", 3, 10, "Dash zurück, wirft 5 Dolche (max. 2 Treffer je Ziel).", tags="aoe,utility", flags="dash_back", rng=250, rad=200, dmg=200, count=5, max_hits=2)
    S("assassin_poison_attack", "Giftangriff", k, 1, "strike", 6, 14, "Gift über 6 s (Zauberschaden).", tags="single,dot", flags="dot,spell", rng=100, dmg=600, dur=6)
    S("assassin_venom_fang", "Giftzahn", k, 1, "cone", 6, 14, "Giftiger Kegel, wirkt über Zeit.", tags="aoe,dot", flags="dot,spell", rng=140, angle=80, dmg=550, dur=6)
    S("assassin_poison_mist", "Giftnebel", k, 1, "ground", 6, 16, "Giftwolke (Radius 125) für 6 s.", tags="aoe,dot", flags="dot,spell", rng=200, rad=125, dmg=500, dur=6)
    S("assassin_poison_master", "Giftmeister", k, 2, "buff_self", 40, 22, "+25 % mehr Zauberschaden.", dur=30, stat="more_spell", pct=25, combat=0)
    S("assassin_danger_sense", "Gefahrensinn", k, 1, "buff_self", 40, 20, "15 % weniger erlittener Schaden.", dur=30, stat="dmg_reduce", pct=15)
    S("assassin_vital_sense", "Vitalsinn", k, 1, "buff_self", 40, 20, "+15 % Krit-Chance.", dur=30, stat="crit", pct=15, combat=0)
    S("assassin_one_sure_strike", "Sicherer Streich", k, 2, "buff_self", 40, 22, "+40 % Krit-Schaden.", dur=30, stat="crit_dmg", pct=40, combat=0)
    S("assassin_time_assassination", "Zeit der Ermordung", k, 2, "buff_self", 40, 22, "+25 % Angriffstempo.", dur=30, stat="atk_speed", pct=25, combat=0)
    S("assassin_disruption", "Störtaktik", k, 2, "debuff", 25, 20, "Gegner verursachen 25 % weniger Angriffsschaden.", rad=350, dur=15, stat="enemy_atk_down", pct=25, minT=2)
    S("assassin_step", "Assassinenschritt", k, 1, "buff_self", 30, 16, "+30 % Lauftempo für 15 s.", dur=15, stat="move_speed", pct=30, combat=0)
    # ---- Schamane (13) ----
    k = "shaman"
    S("shaman_soul_spike", "Seelenstachel", k, 2, "strike", 6, 20, "Gegner erleidet 15 % mehr Schaden.", tags="single,debuff", flags="spell", rng=650, dmg=500, dur=10, stat="enemy_taken", pct=15)
    S("shaman_lightning_arrow", "Blitzpfeil", k, 1, "projectile", 5, 16, "Blitz durchdringt zwei Gegner.", flags="spell", tags="aoe", rng=700, dmg=400, pierce=2, speed=1100)
    S("shaman_earthpulse", "Erdpuls-Eruption", k, 1, "ground", 6, 20, "Eruption am Ziel.", flags="spell", rng=600, rad=180, dmg=420, delay=0.3)
    S("shaman_corrupted_mist", "Verderbter Nebel", k, 1, "ground", 7, 20, "Giftiger Nebel (6 s).", tags="aoe,dot", flags="dot,spell", rng=550, rad=160, dmg=500, dur=6)
    S("shaman_nature_harmony", "Naturharmonie", k, 2, "buff_self", 40, 24, "+20 % Zauberschaden, +15 % Zaubertempo.", dur=30, stat="spell_pct", pct=20, stat2="cast_speed", pct2=15, combat=0)
    S("shaman_wicked_sorcery", "Böser Zauber", k, 2, "debuff", 20, 20, "Gegner: 20 % weniger Angriffs- und Zauberschaden.", rng=650, rad=350, dur=15, stat="enemy_atk_down", pct=20, stat2="enemy_spell_down", pct2=20, minT=2)
    S("shaman_nightmare_ward", "Albtraumschutz", k, 2, "debuff", 20, 20, "Gegner erleiden 20 % mehr Schaden.", rng=650, rad=350, dur=15, stat="enemy_taken", pct=20, minT=2)
    S("shaman_totem_sea_turtle", "Totem: Seeschildkröte", k, 2, "totem", 15, 26, "Spott (5 s) und stark erhöhte Rüstung im Radius.", tags="totem,buff", flags="taunt", rng=300, rad=300, dur=20, stat="armor_pct", pct=80, totem="turtle", minT=1)
    S("shaman_totem_storm_dragon", "Totem: Sturmdrache", k, 2, "totem", 12, 26, "Ruft periodisch Blitze auf Gegner.", tags="totem,aoe", flags="spell", rng=500, rad=350, dmg=250, dur=20, totem="storm_dragon")
    S("shaman_totem_red_dragon", "Totem: Rotdrache", k, 3, "totem", 15, 34, "Kegelschaden und +15 % Angriffs-/Zauberschaden.", tags="totem,aoe,buff", flags="spell", rng=500, rad=300, dmg=350, dur=25, stat="more_attack", pct=15, stat2="more_spell", pct2=15, totem="red_dragon")
    S("shaman_totem_war_drum", "Totem: Kriegstrommel", k, 1, "totem", 12, 20, "Periodischer Flächenschaden.", tags="totem,aoe", flags="spell", rng=450, rad=250, dmg=200, dur=15, totem="war_drum")
    S("shaman_totem_clairvoyance", "Totem: Hellsicht", k, 2, "totem", 20, 24, "+30 % Krit-Schaden für Verbündete im Radius.", tags="totem,buff", rng=300, rad=300, dur=20, stat="crit_dmg", pct=30, totem="clairvoyance", minT=0, combat=0)
    S("shaman_totem_gale_hawk", "Totem: Windfalke", k, 2, "totem", 20, 24, "+15 % Angriffs-/Zaubertempo und Lauftempo im Radius.", tags="totem,buff", rng=300, rad=300, dur=20, stat="atk_speed", pct=15, stat2="move_speed", pct2=15, totem="gale_hawk", minT=0, combat=0)
    # ---- Allgemein (14) ----
    k = "common"
    S("common_footwork", "Fußarbeit", k, 1, "buff_self", 30, 14, "Stark erhöhtes Lauftempo (+40 %) für 20 s.", dur=20, stat="move_speed", pct=40, combat=0)
    S("common_rapid_movements", "Schnelle Bewegung", k, 0, "buff_self", 30, 10, "+20 % Lauftempo für 20 s.", dur=20, stat="move_speed", pct=20, combat=0)
    S("common_bless_might", "Segen der Macht", k, 1, "buff_self", 40, 18, "+20 % mehr Angriffsschaden für 30 s.", dur=30, stat="more_attack", pct=20, combat=0)
    S("common_bless_magic", "Segen der Magie", k, 1, "buff_self", 40, 18, "+20 % mehr Zauberschaden für 30 s.", dur=30, stat="more_spell", pct=20, combat=0)
    S("common_frenzy", "Raserei", k, 1, "buff_self", 40, 18, "+20 % Angriffstempo für 30 s.", dur=30, stat="atk_speed", pct=20, combat=0)
    S("common_call_healing", "Ruf der Heilung", k, 1, "warp", 25, 20, "Warp nach hinten und Heilung bei niedrigen HP.", flags="dash_back,heal_self,no_combat_req", rng=400, dmg=300, selfhp=35, combat=1, minT=0)
    S("common_backstep", "Rückschritt", k, 0, "dash", 6, 8, "Kurzer Schritt zurück.", flags="dash_back", rng=200, minT=0)
    S("common_step_left", "Seitschritt links", k, 0, "dash", 6, 8, "Ausweichschritt zur Seite.", flags="dash_side", rng=200, minT=0)
    S("common_step_right", "Seitschritt rechts", k, 0, "dash", 6, 8, "Ausweichschritt zur Seite.", flags="dash_side", rng=200, minT=0)
    S("common_swift_exaltation", "Hast", k, 1, "buff_self", 40, 20, "+15 % Lauf- und Angriffstempo.", dur=30, stat="move_speed", pct=15, stat2="atk_speed", pct2=15, combat=0)
    S("common_stone_skin", "Steinhaut", k, 0, "buff_self", 40, 16, "+40 flache Rüstung (nur früh nützlich).", dur=30, stat="armor", pct=40)
    S("common_mana_surge", "Manaschub", k, 1, "mana", 90, 0, "Stellt 40 % Mana wieder her.", dmg=40, combat=0, minT=0)
    S("common_battle_focus", "Kampffokus", k, 1, "buff_self", 40, 16, "+10 % Krit-Chance für 30 s.", dur=30, stat="crit", pct=10, combat=0)
    S("common_vigor", "Lebenskraft", k, 0, "buff_self", 40, 16, "+15 % Max-Leben für 30 s.", dur=30, stat="hp_pct", pct=15, combat=0)
    write_tsv("skills.tsv", SKILL_COLS, SKILLS,
              "Skill-Parameter: siehe SkillDef in src/data/data.h. dmg_pct = Schaden Lv1 in % (Heilung/Mana: Anteil).")


# ---------------------------------------------------------------------------
# Balance-Werte (aus BALANCE_FIELDS in src/data/data.h) und Spezifikations-Parser
# ---------------------------------------------------------------------------
def load_balance_fields():
    text = open(os.path.join(ROOT, "src", "data", "data.h"), encoding="utf-8").read()
    start = text.index("#define BALANCE_FIELDS")
    end = text.index("typedef struct Balance")
    return [(m.group(1), float(m.group(2))) for m in re.finditer(r"BAL\((\w+),\s*([0-9.]+)\)", text[start:end])]


BAL = dict(load_balance_fields())


def rnd(x):
    """Kaufmaennisch runden (nicht Python-Bankers-Rounding)."""
    return int(math.floor(x + 0.5))


def read_spec(fn):
    with open(os.path.join(SPEC, fn), encoding="utf-8") as f:
        return f.read().splitlines()


def weapon_value(level, tier):
    return rnd((BAL["weapon_base"] + BAL["weapon_k"] * level ** BAL["weapon_pow"]) * TIER_MULT[tier])


def sub_value(level, tier):
    return rnd((BAL["sub_base"] + BAL["sub_k"] * level ** BAL["sub_pow"]) * TIER_MULT[tier])


SLOT_FACTOR = {"helm": 1.0, "chest": 1.6, "gloves": 0.7, "boots": 0.7}


def armor_value(level, tier, slot):
    return rnd((BAL["armor_base"] + BAL["armor_k"] * level ** BAL["armor_pow"]) * TIER_MULT[tier] * SLOT_FACTOR[slot])


# 21 Stufen: Tier und Mindestlevel (Spec 07/08)
STEP_TIERS = [0] * 7 + [1] * 5 + [2] * 4 + [3] * 3 + [4, 5]
STEP_LEVELS = [0, 1, 5, 10, 15, 20, 25, 20, 25, 30, 35, 45, 27, 37, 46, 55, 44, 52, 58, 60, 80]

ITEM_COLS = ["id", "name", "kind", "tier", "level", "slot", "class", "value", "mods", "duration", "potion_type",
             "charges", "material_line", "stone_kind", "family", "set", "craftable", "enchantable", "legendary",
             "index"]
ITEMS = []


def add_item(**kw):
    r = dict(id="", name="", kind="", tier="normal", level=0, slot="", **{"class": "any"}, value=0, mods="",
             duration=0, potion_type="", charges=0, material_line="", stone_kind="", family=0, set=0,
             craftable=0, enchantable=0, legendary="", index=0)
    r.update(kw)
    ITEMS.append(r)
    return r


def parse_effect(text):
    """Waffen-Zusatzeffekt-Text der Spec -> mods-String."""
    d = {}
    for pat, key in ((r"Krit \+([\d.]+)%", "crit"), (r"Krit-Schaden \+([\d.]+)%", "crit_dmg"),
                     (r"Zaubertempo \+([\d.]+)%", "cast_speed"), (r"Angriffstempo \+([\d.]+)%", "atk_speed")):
        m = re.search(pat, text)
        if m:
            d[key] = float(m.group(1))
    return mods(**d)


ROW_RE = re.compile(r"^\s+(\d\d) \| (.+?)\s*\| (Normal|Magisch|Selten|Einzigartig|Boss|Legendaer)\s*\|\s*(\d+) \|\s*(\d+) \| (.*)$")


def gen_weapons():
    """147 Waffen: Namen/Level/Zusatzeffekte per Regex aus Spec 07, Schaden per Formel."""
    cls = None
    per_class = {}
    for line in read_spec("07_WAFFEN_UND_SEKUNDAERWAFFEN.txt"):
        h = re.match(r"^--- (\w+) \((\w+)\) ---", line)
        if h:
            cls = CLASS_FROM_SPEC[h.group(1)]
            continue
        m = ROW_RE.match(line)
        if m and cls is not None:
            per_class.setdefault(cls, []).append(m)
    for c in range(7):
        rows = per_class[c]
        assert len(rows) == 21, (c, len(rows))
        for i, m in enumerate(rows):
            tier = TIER_FROM_SPEC[m.group(3)]
            level = int(m.group(4))
            assert tier == STEP_TIERS[i] and level == STEP_LEVELS[i], (c, i)
            add_item(id="%s_weapon_%02d" % (CLASSES[c], i + 1), name=umlaut(m.group(2).strip()), kind="weapon",
                     tier=TIERS[tier], level=level, slot="weapon", **{"class": CLASSES[c]},
                     value=weapon_value(level, tier), mods=parse_effect(m.group(6)),
                     craftable=1, enchantable=int(tier >= 2), index=i + 1)


# Hauptwirkung der Sekundaerwaffen je Klasse (Spec 07), Staerke wächst mit dem Tier
SUB_MODS = {
    0: lambda t: mods(armor_pct=[2, 4, 6, 8, 10, 14][t], hp_pct=[1, 2, 3, 4, 6, 8][t]),
    1: lambda t: mods(attack_pct=[2, 4, 6, 8, 10, 14][t]),
    2: lambda t: mods(spell_pct=[2, 4, 6, 8, 10, 14][t], mana_regen=[0.2, 0.4, 0.6, 0.8, 1.2, 1.6][t]),
    3: lambda t: mods(atk_speed=[1, 2, 3, 4, 6, 8][t], projectiles=1 if t == 5 else 0),
    4: lambda t: mods(crit=[1, 2, 3, 4, 6, 8][t], crit_dmg=[4, 8, 12, 16, 24, 32][t]),
    5: lambda t: mods(cast_speed=[1, 2, 3, 4, 6, 8][t], spell_pct=[1, 2, 3, 4, 5, 7][t]),
    6: lambda t: mods(heal_pct=[2, 4, 6, 8, 10, 14][t], mana_regen=[0.2, 0.4, 0.6, 0.8, 1.2, 1.6][t]),
}


def gen_subs():
    """42 Sekundaerwaffen aus Spec 07 (Namen per Regex, Wert per Formel)."""
    cls = None
    per_class = {}
    sub_re = re.compile(r"^\s+(Normal|Magisch|Selten|Einzigartig|Boss|Legendaer)\s+\| (.+?)\s*\| Lvl\s+(\d+) \| Wert\s+(\d+)")
    for line in read_spec("07_WAFFEN_UND_SEKUNDAERWAFFEN.txt"):
        h = re.match(r"^--- (\w+): (.+?) ---", line)
        if h:
            cls = CLASS_FROM_SPEC[h.group(1)]
            continue
        m = sub_re.match(line)
        if m and cls is not None:
            per_class.setdefault(cls, []).append(m)
    for c in range(7):
        rows = per_class[c]
        assert len(rows) == 6, (c, len(rows))
        for i, m in enumerate(rows):
            tier = TIER_FROM_SPEC[m.group(1)]
            level = int(m.group(3))
            add_item(id="%s_sub_%d" % (CLASSES[c], i + 1), name=umlaut(m.group(2).strip()), kind="sub",
                     tier=TIERS[tier], level=level, slot="sub", **{"class": CLASSES[c]},
                     value=sub_value(level, tier), mods=SUB_MODS[c](tier), craftable=1,
                     enchantable=int(tier >= 2), index=i + 1)


# Astral-Set (Legendaer) und Aschekoenig-Set (Boss): Set-IDs aus sets.tsv
SET_ASCHEKOENIG = 1
SET_ASTRAL = 2
ARMOR_SLOTS = ["helm", "chest", "gloves", "boots"]
ARMOR_SECTION = {"Helm": "helm", "Brustpanzer": "chest", "Handschuhe": "gloves", "Stiefel": "boots"}


def armor_extra(slot, tier, level):
    """Feste Zusatzeffekte nach Tier (Spec 08)."""
    if slot == "helm":
        return {1: mods(mana_regen=5) if level == 35 else "", 2: mods(cdr=4), 3: mods(cdr=7, atk_speed=5),
                4: mods(cdr=8, cast_speed=5), 5: mods(cdr=10, spellres_pct=20)}.get(tier, "")
    if slot == "chest":
        return {1: mods(dr_attack=5, mana_regen=8 if level == 35 else 0), 2: mods(dr_attack=10, dr_spell=5),
                3: mods(dr_attack=15, dr_spell=8 if level == 58 else 7, hp_pct=5),
                4: mods(dr_attack=20, dr_spell=10, hp_pct=8),
                5: mods(dr_attack=25, dr_spell=12, hp_pct=10, spellres_pct=30)}.get(tier, "")
    if slot == "gloves":
        return {1: mods(crit=3), 2: mods(crit=5, crit_dmg=10), 3: mods(crit=5, crit_dmg=25),
                4: mods(crit=6, crit_dmg=30, cast_speed=5), 5: mods(crit=8, crit_dmg=40, cast_speed=8, spellres_pct=10)}.get(tier, "")
    return {1: mods(crit_dmg=10), 2: mods(crit_dmg=25), 3: mods(crit_dmg=35),
            4: mods(crit_dmg=45, cast_speed=5), 5: mods(crit_dmg=60, cast_speed=8, spellres_pct=10)}.get(tier, "")


def armor_base_effect(slot, tier, level, text):
    """Basiseffekt des Slots: Helm per Formel, sonst aus der Spec-Zeile."""
    if slot == "helm":
        return mods(crit=round((1 + 0.07 * level) * TIER_MULT[tier] + 1e-9, 1))
    n = float(re.search(r"\+([\d.]+)", text).group(1))
    key = {"chest": "chest_cap", "gloves": "atk_speed", "boots": "move_speed"}[slot]
    return mods(**{key: n})


def legendary_id(cls, slot):
    return cls * 4 + ARMOR_SLOTS.index(slot) + 1


def gen_armor():
    """84 Ruestungsteile (Spec 08)."""
    slot = None
    per_slot = {}
    for line in read_spec("08_RUESTUNG.txt"):
        h = re.match(r"^--- (Helm|Brustpanzer|Handschuhe|Stiefel) ---", line)
        if h:
            slot = ARMOR_SECTION[h.group(1)]
            continue
        m = ROW_RE.match(line)
        if m and slot:
            per_slot.setdefault(slot, []).append(m)
    for slot in ARMOR_SLOTS:
        rows = per_slot[slot]
        assert len(rows) == 21, (slot, len(rows))
        for i, m in enumerate(rows):
            tier = TIER_FROM_SPEC[m.group(3)]
            level = int(m.group(4))
            assert tier == STEP_TIERS[i] and level == STEP_LEVELS[i], (slot, i)
            legend = ""
            if tier == 5:
                legend = ";".join("%s:%d" % (CLASSES[c], legendary_id(c, slot)) for c in range(7))
            add_item(id="armor_%s_%02d" % (slot, i + 1), name=umlaut(m.group(2).strip()), kind="armor",
                     tier=TIERS[tier], level=level, slot=slot, value=armor_value(level, tier, slot),
                     mods=merge_mods(armor_base_effect(slot, tier, level, m.group(6)), armor_extra(slot, tier, level)),
                     set={4: SET_ASCHEKOENIG, 5: SET_ASTRAL}.get(tier, 0), craftable=1, enchantable=int(tier >= 2),
                     legendary=legend, index=i + 1)


# ---- Schmuck (Spec 10) ----
FAMILY_IDS = ["warrior", "assassin", "elemental", "life"]
FAMILY_NOUN = ["Krieger", "Assassinen", "Elementar", "Lebens"]
NECK_PREFIX = ["Schlichte ", "Silberne ", "Platin-", "Mithril-", "Blutrote ", "Astrale "]
RINGS = {
    0: [("Kupferner Kriegerring", dict(dmg_flat=6, crit=1)), ("Silberner Kriegerring", dict(dmg_flat=14, crit=2)),
        ("Platin-Kriegerring", dict(dmg_flat=28, crit=3)), ("Mithril-Kriegerring", dict(dmg_flat=50, crit=4.5)),
        ("Blutroter Kriegerring", dict(dmg_flat=85, crit=6.5)), ("Astraler Kriegerring", dict(dmg_flat=140, crit=9))],
    1: [("Schlichter Assassinenring", dict(crit=1, atk_speed=2)), ("Silberner Assassinenring", dict(crit=2, atk_speed=4)),
        ("Platin-Assassinenring", dict(crit=3, atk_speed=6)), ("Mithril-Assassinenring", dict(crit=4.5, atk_speed=9)),
        ("Schattenring", dict(crit=6.5, atk_speed=13)), ("Astraler Assassinenring", dict(crit=9, atk_speed=18))],
    2: [("Ring des Funkens", dict(mana=20, mana_regen=0.5)), ("Ring der Elemente", dict(mana=50, mana_regen=1)),
        ("Platin-Elementarring", dict(mana=100, mana_regen=2)), ("Mithril-Elementarring", dict(mana=180, mana_regen=3.5)),
        ("Ring des Sternenlichts", dict(mana=300, mana_regen=5.5)), ("Astraler Elementarring", dict(mana=500, mana_regen=8))],
    3: [("Ring des Lebens", dict(hp=50, hp_regen=0.5)), ("Silberner Lebensring", dict(hp=130, hp_regen=1.2)),
        ("Platin-Lebensring", dict(hp=280, hp_regen=2.5)), ("Mithril-Lebensring", dict(hp=520, hp_regen=4.5)),
        ("Phönixring", dict(hp=900, hp_regen=7)), ("Astraler Lebensring", dict(hp=1500, hp_regen=10))],
}


def gen_jewelry():
    """24 Ringe + 24 Halsketten (Halskette = exakt doppelte Werte)."""
    for fam in range(4):
        for t in range(6):
            name, vals = RINGS[fam][t]
            add_item(id="ring_%s_%s" % (FAMILY_IDS[fam], TIERS[t]), name=name, kind="ring", tier=TIERS[t], level=1,
                     slot="ring", mods=mods(**vals), family=fam)
            nname = "%s%shalskette" % (NECK_PREFIX[t], FAMILY_NOUN[fam])
            add_item(id="neck_%s_%s" % (FAMILY_IDS[fam], TIERS[t]), name=nname, kind="necklace", tier=TIERS[t],
                     level=1, slot="necklace", mods=mods(**{k: v * 2 for k, v in vals.items()}), family=fam)


# ---- Traenke (Spec 09) ----
POTION_NAMES = {
    "heal": ["Kleiner Heiltrank", "Mittlerer Heiltrank", "Großer Heiltrank", "Elixier der Genesung", "Phönixblut",
             "Astrales Lebenselixier"],
    "mana": ["Kleiner Manatrank", "Mittlerer Manatrank", "Großer Manatrank", "Elixier des Geistes", "Sternentau",
             "Astrales Manaelixier"],
    "haste": ["Läuferphiole", "Windtrank", "Falkentrank", "Geisterelixier", "Sturmelixier", "Astrales Eilelixier"],
    "iron": ["Rindentrunk", "Eisenhauttrank", "Solid-Trank", "Bollwerkselixier", "Titanenhaut", "Astrale Ägis-Essenz"],
    "frenzy": ["Funkentrank", "Brenntrank", "Raserei-Elixier", "Rabenwut", "Blutrausch-Elixier", "Astrale Raserei"],
}
POTION_VALUE = {"heal": [15, 22, 30, 38, 48, 60], "mana": [15, 22, 30, 38, 48, 60],
                "haste": [10, 20, 30, 40, 50, 60], "iron": [10, 20, 35, 50, 75, 100],
                "frenzy": [10, 20, 30, 40, 50, 60]}
POTION_DURATION = [8, 10, 14, 18, 24, 30]
POTION_CHARGES = [2, 3, 4, 5, 6, 8]


def gen_potions():
    for pt in ["heal", "mana", "haste", "iron", "frenzy"]:
        for t in range(6):
            add_item(id="potion_%s_%s" % (pt, TIERS[t]), name=POTION_NAMES[pt][t], kind="potion", tier=TIERS[t],
                     level=1, slot="potion", value=POTION_VALUE[pt][t],
                     duration=0 if pt in ("heal", "mana") else POTION_DURATION[t], potion_type=pt,
                     charges=POTION_CHARGES[t], craftable=1)


def gen_wings_bags():
    wings = ["Federflügel", "Windflügel", "Falkenflügel", "Drachenflügel", "Schwingen des Abgrunds",
             "Astrale Himmelsschwingen"]
    speed = [10, 25, 50, 100, 180, 300]
    bags = ["Lederbeutel", "Reisetasche", "Verstärkter Rucksack", "Drachenleder-Rucksack",
            "Rucksack des Plünderers", "Astraler Raumrucksack"]
    cap = [5, 9, 14, 19, 25, 30]
    for t in range(6):
        add_item(id="wings_%s" % TIERS[t], name=wings[t], kind="wings", tier=TIERS[t], level=1, slot="wings",
                 mods=mods(move_speed=speed[t]))
    for t in range(6):
        add_item(id="bag_%s" % TIERS[t], name=bags[t], kind="bag", tier=TIERS[t], level=1, slot="bag",
                 mods=mods(chest_cap=cap[t]), craftable=1)


# ---- Materialien, Steine, Spezial (Spec 11/12) ----
MATERIALS = {
    "metal": ["Kupferbarren", "Silberbarren", "Platinbarren", "Mithrilbarren", "Adamantbarren", "Astralmetall"],
    "wood": ["Eichenholz", "Eschenholz", "Eibenholz", "Ebenholz", "Weltenbaumholz", "Himmelsholz"],
    "cloth": ["Leinen", "Seide", "Spinnenseide", "Schattenleder", "Abgrundgewebe", "Astralgewebe"],
    "crystal": ["Quarz", "Saphir", "Rubin", "Smaragd", "Blutstein", "Astralkristall"],
    "essence": ["Schwache Essenz", "Gute Essenz", "Reine Essenz", "Mächtige Essenz", "Boss-Essenz", "Astrale Essenz"],
}
STONE_NAME = {"enh_weapon": "Waffen-Verbesserungsstein", "enh_armor": "Rüstungs-Verbesserungsstein",
              "enh_jewel": "Schmuck-Verbesserungsstein"}
ENCHANT_STONES = {2: "Schwacher Manastein", 3: "Manastein", 4: "Manakern", 5: "Orpheon"}
ROMAN = ["I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX"]


def magic_stone_tier(act):
    return 1 if act <= 3 else 2 if act <= 6 else 3 if act <= 9 else 4 if act == 10 else 5


def gen_materials_stones():
    for line, names in MATERIALS.items():
        for t in range(6):
            add_item(id="mat_%s_%s" % (line, TIERS[t]), name=names[t], kind="material", tier=TIERS[t],
                     material_line=line)
    for sk, base in STONE_NAME.items():
        for t in range(6):
            add_item(id="stone_%s_%s" % (sk, TIERS[t]), name="%s (%s)" % (base, TIER_NAMES[t]), kind="stone",
                     tier=TIERS[t], stone_kind=sk)
    for t, name in ENCHANT_STONES.items():
        add_item(id="stone_enchant_%s" % TIERS[t], name=name, kind="stone", tier=TIERS[t], stone_kind="enchant")
    for act in range(1, 12):
        name = "Magiestein %s" % ROMAN[act - 1] if act <= 9 else ("Ahel-Magiestein" if act == 10 else "Orpheon-Magiestein")
        add_item(id="stone_magic_act%d" % act, name=name, kind="stone", tier=TIERS[magic_stone_tier(act)],
                 stone_kind="magic", value=act)


def gen_special():
    sp = [("special_relic", "Reliktteil", "boss"), ("special_timeless_stone", "Zeitloser Stein", "unique"),
          ("special_raven_feather", "Rabenfeder", "rare"), ("special_dark_essence", "Essenz der Dunkelheit", "unique"),
          ("special_mystic_ash", "Mystische Asche", "boss"), ("special_glass_vial", "Glasphiole", "normal"),
          ("special_herbs", "Kräuterbündel", "normal"), ("special_sub_blueprint", "Sekundärwaffen-Bauplan", "rare"),
          ("special_temple_ticket", "Tempel-Ticket", "normal")]
    for sid, name, tier in sp:
        add_item(id=sid, name=name, kind="special", tier=tier)


# ---- Legendaere Klasseneffekte (Spec 08): 7 Klassen x 4 Slots = 28 ----
LEGEND = {
    # (Klasse, Slot): (Name, mods, Verhalten, Beschreibung)
    ("knight", "helm"): ("Eiserne Entschlossenheit", dict(armor_pct=15), "none", "Ritter: +15 % Rüstung."),
    ("knight", "chest"): ("Wallender Stahl", dict(hp_pct=10), "rolling_steel", "Ritter: 15 % der erlittenen Treffer werden als Flächenschaden zurückgegeben."),
    ("knight", "gloves"): ("Schildhand", dict(atk_speed=8, armor_pct=10), "none", "Ritter: +8 % Angriffstempo, +10 % Rüstung."),
    ("knight", "boots"): ("Bollwerkschritt", dict(move_speed=10, dmg_reduce=5), "none", "Ritter: +10 % Lauftempo, 5 % weniger Schaden."),
    ("warrior", "helm"): ("Blutrausch", dict(), "bloodrage", "Krieger: Unter 50 % HP +30 % Angriffstempo."),
    ("warrior", "chest"): ("Berserkerpanzer", dict(hp_pct=12, attack_pct=10), "none", "Krieger: +12 % Max-Leben, +10 % Angriffsschaden."),
    ("warrior", "gloves"): ("Axtmeister", dict(atk_speed=10, crit=5), "none", "Krieger: +10 % Angriffstempo, +5 % Krit-Chance."),
    ("warrior", "boots"): ("Kriegspfad", dict(move_speed=12, attack_pct=8), "none", "Krieger: +12 % Lauftempo, +8 % Angriffsschaden."),
    ("wizard", "helm"): ("Krone der Sterne", dict(spell_pct=12, mana_pct=10), "none", "Magier: +12 % Zauberschaden, +10 % Max-Mana."),
    ("wizard", "chest"): ("Arkanes Gewand", dict(mana_pct=15, dmg_reduce=5), "none", "Magier: +15 % Max-Mana, 5 % weniger Schaden."),
    ("wizard", "gloves"): ("Arkane Emission", dict(), "arcane_emission", "Magier: Feuert periodisch Magieprojektile auf nahe Gegner."),
    ("wizard", "boots"): ("Schritt der Leere", dict(cdr=6, cast_speed=8), "none", "Magier: +6 % Abklingzeitreduktion, +8 % Zaubertempo."),
    ("archer", "helm"): ("Adlerblick", dict(crit=8, crit_dmg=20), "none", "Bogenschütze: +8 % Krit-Chance, +20 % Krit-Schaden."),
    ("archer", "chest"): ("Jägerpanzer", dict(projectiles=1, hp_pct=8), "none", "Bogenschütze: +1 Projektil, +8 % Max-Leben."),
    ("archer", "gloves"): ("Sehnenhandschuhe", dict(atk_speed=12, pierce=1), "none", "Bogenschütze: +12 % Angriffstempo, +1 Durchdringung."),
    ("archer", "boots"): ("Explosive Spur", dict(), "explosive_trail", "Bogenschütze: Alle Projektile lösen Explosionen aus."),
    ("assassin", "helm"): ("Henkersmaske", dict(), "executioner", "Assassine: 50 % Splash-Schaden bei Treffer."),
    ("assassin", "chest"): ("Schattenpanzer", dict(crit_dmg=30, dmg_reduce=5), "none", "Assassine: +30 % Krit-Schaden, 5 % weniger Schaden."),
    ("assassin", "gloves"): ("Giftgriff", dict(spell_pct=12, atk_speed=8), "none", "Assassine: +12 % Zauberschaden, +8 % Angriffstempo."),
    ("assassin", "boots"): ("Schattenschritt", dict(move_speed=15, cdr=5), "none", "Assassine: +15 % Lauftempo, +5 % Abklingzeitreduktion."),
    ("shaman", "helm"): ("Ahnenmaske", dict(cast_speed=10, spell_pct=10), "none", "Schamane: +10 % Zaubertempo, +10 % Zauberschaden."),
    ("shaman", "chest"): ("Totem-Echo", dict(), "totem_echo", "Schamane: Beschworene Totems explodieren beim Verschwinden."),
    ("shaman", "gloves"): ("Geisterhände", dict(more_spell=8, cdr=4), "none", "Schamane: +8 % mehr Zauberschaden, +4 % Abklingzeitreduktion."),
    ("shaman", "boots"): ("Windwandler", dict(move_speed=12, cast_speed=6), "none", "Schamane: +12 % Lauftempo, +6 % Zaubertempo."),
    ("priest", "helm"): ("Heiligenschein", dict(heal_pct=15, mana_regen=3), "none", "Priester: +15 % Heilkraft, +3 Manaregeneration."),
    ("priest", "chest"): ("Gewand der Gnade", dict(heal_pct=10, hp_pct=10), "none", "Priester: +10 % Heilkraft, +10 % Max-Leben."),
    ("priest", "gloves"): ("Segenshände", dict(heal_pct=12, cast_speed=8), "none", "Priester: +12 % Heilkraft, +8 % Zaubertempo."),
    ("priest", "boots"): ("Marsch des Lichts", dict(heal_pct=8), "march_of_light", "Priester: +Projektile, höheres Trefferlimit, Homing sucht neues Ziel."),
}


def gen_legendary():
    rows = []
    for c, cid in enumerate(CLASSES):
        for slot in ARMOR_SLOTS:
            name, m, beh, desc = LEGEND[(cid, slot)]
            rows.append(dict(id=legendary_id(c, slot), name=name, **{"class": cid}, slot=slot, mods=mods(**m),
                             behavior=beh, desc=desc))
    write_tsv("legendary.tsv", ["id", "name", "class", "slot", "mods", "behavior", "desc"], rows)


def gen_sets():
    rows = [
        dict(id=SET_ASCHEKOENIG, name="Aschekönig", mods2=mods(attack_pct=10), mods4="", behavior4="chain_lightning",
             desc2="+10 % Angriffsschaden", desc4="Der Basisangriff löst einen Kettenblitz aus."),
        dict(id=SET_ASTRAL, name="Astral", mods2=mods(spellres_pct=10), mods4=mods(cdr_cap=5), behavior4="none",
             desc2="+10 % Zauberwiderstand", desc4="+5 % maximale Abklingzeitreduktion (Obergrenze 85 %)."),
    ]
    write_tsv("sets.tsv", ["id", "name", "mods2", "mods4", "behavior4", "desc2", "desc4"], rows)


def gen_items():
    ITEMS.clear()
    gen_weapons()
    gen_subs()
    gen_armor()
    gen_jewelry()
    gen_potions()
    gen_wings_bags()
    gen_materials_stones()
    gen_special()
    ids = [i["id"] for i in ITEMS]
    assert len(ids) == len(set(ids)), "doppelte Item-IDs"
    write_tsv("items.tsv", ITEM_COLS, ITEMS,
              "value: Waffe=Schaden, Sub=Hauptwert, Ruestung=Ruestung, Trank=Wirkung in %, Magiestein=Akt.")
    gen_legendary()
    gen_sets()


# ---------------------------------------------------------------------------
# monsters.tsv (Spec 15)
# ---------------------------------------------------------------------------
MONSTER_COLS = ["id", "name", "arch", "act_min", "act_max", "hp_f", "dmg_f", "speed", "range", "radius", "attack_cd",
                "proj_speed", "color", "shape", "abilities", "boss", "elite_only", "weight"]
# Archetyp-Vorgaben: hp_f, dmg_f, speed, range, radius, attack_cd, proj_speed, shape, abilities, weight
ARCH_DEF = {
    "melee": (1.0, 1.0, 130, 55, 22, 1.4, 0, 0, "", 1.0),
    "ranged": (0.7, 1.0, 110, 520, 20, 2.0, 420, 2, "", 0.8),
    "swarm": (0.3, 0.3, 170, 40, 14, 1.0, 0, 1, "", 0.8),
    "brute": (3.0, 2.0, 80, 70, 36, 2.4, 0, 5, "slam", 0.5),
    "hunter": (0.8, 1.2, 220, 50, 20, 1.1, 0, 3, "charge", 0.8),
    "summoner": (1.0, 0.8, 100, 450, 24, 3.0, 300, 4, "summon", 0.4),
    "healer": (0.8, 0.6, 100, 400, 22, 2.5, 300, 2, "heal_ally", 0.4),
    "explosive": (0.5, 3.0, 190, 45, 20, 1.0, 0, 4, "", 0.6),
    "elemental": (1.2, 1.1, 120, 350, 26, 2.0, 350, 0, "pools", 0.7),
    "guardian": (2.5, 1.3, 70, 70, 34, 2.2, 0, 1, "shield", 0.4),
    "totem": (2.0, 1.0, 0, 600, 26, 2.5, 380, 5, "rain", 0.5),
    "demon": (1.6, 1.5, 140, 120, 28, 1.6, 0, 3, "nova", 0.8),
}
MONSTERS = []


def MON(id, name, arch, a0, a1, color, elite=0, **kw):
    hp, dmg, spd, rng, rad, cd, proj, shape, abil, w = ARCH_DEF[arch]
    d = dict(hp_f=hp, dmg_f=dmg, speed=spd, range=rng, radius=rad, attack_cd=cd, proj_speed=proj, shape=shape,
             abilities=abil, weight=w)
    d.update(kw)
    MONSTERS.append(dict(id=id, name=name, arch=arch, act_min=a0, act_max=a1, color=color, boss=0, elite_only=elite, **d))


def BOSS(id, name, a0, a1, hp, dmg, abil, color, rad=64, speed=110, rng=140, cd=2.2, proj=0, shape=5):
    MONSTERS.append(dict(id=id, name=name, arch="boss", act_min=a0, act_max=a1, hp_f=hp, dmg_f=dmg, speed=speed,
                         range=rng, radius=rad, attack_cd=cd, proj_speed=proj, color=color, shape=shape,
                         abilities=abil, boss=1, elite_only=0, weight=0))


def gen_monsters():
    MONSTERS.clear()
    # Akt 1
    MON("goblin", "Goblin", "melee", 1, 2, "#6B8E23")
    MON("goblin_archer", "Goblin-Bogenschütze", "ranged", 1, 3, "#8FA33A")
    MON("wolf", "Wolf", "hunter", 1, 3, "#7A7A7A", abilities="")
    MON("giant_spider", "Riesenspinne", "melee", 1, 3, "#4B3B2B", speed=150)
    MON("spiderling", "Spinnenbrut", "swarm", 1, 3, "#5C4A3A")
    MON("giant_rat", "Riesenratte", "swarm", 1, 2, "#8B7765")
    # Akt 2
    MON("frog_warrior", "Froschkrieger", "melee", 2, 3, "#3E8E5A", hp_f=1.3)
    MON("wisp", "Irrlicht", "elemental", 2, 4, "#9FE8C8", abilities="")
    MON("moss_brute", "Moosbrecher", "brute", 2, 4, "#4F6B3A")
    MON("toad_shaman", "Krötenschamane", "healer", 2, 4, "#2F7F6F")
    MON("bog_leech", "Sumpfegel", "swarm", 2, 3, "#5B3A4A")
    # Akt 3
    MON("scorpion", "Skorpion", "hunter", 3, 4, "#B5651D")
    MON("mummy", "Mumie", "melee", 3, 4, "#D8C9A3", hp_f=1.4)
    MON("sand_bomber", "Sandsprenger", "explosive", 3, 5, "#D9A441")
    MON("dune_archer", "Dünenschütze", "ranged", 3, 4, "#C8A15A")
    MON("sand_guardian", "Sandwächter", "guardian", 3, 5, "#B89A5E")
    # Akt 4
    MON("yeti", "Yeti", "brute", 4, 5, "#DCE6F0")
    MON("frost_spirit", "Frostgeist", "elemental", 4, 5, "#8FD3F4")
    MON("ice_archer", "Eisbogenschütze", "ranged", 4, 5, "#A5C8E4")
    MON("snow_wolf", "Schneewolf", "hunter", 4, 5, "#E8EEF4")
    # Akt 5
    MON("fire_elemental", "Feuerelementar", "elemental", 5, 6, "#E8541E")
    MON("lava_golem", "Lavagolem", "brute", 5, 6, "#B8381A")
    MON("ember_imp", "Glutkobold", "swarm", 5, 6, "#F08A3C")
    MON("magma_bomber", "Magmasprenger", "explosive", 5, 6, "#D2461E")
    # Akt 6
    MON("tree_spirit", "Baumgeist", "guardian", 6, 7, "#3F6B35")
    MON("witch", "Hexe", "ranged", 6, 7, "#6B3F8F", hp_f=0.8)
    MON("thorn_swarm", "Dornenschwarm", "swarm", 6, 7, "#5A7A2F")
    MON("curse_hag", "Fluchheilerin", "healer", 6, 8, "#7A4A7A", elite=1)
    # Akt 7
    MON("undead_pirate", "Untoter Pirat", "melee", 7, 8, "#6E8B8B", hp_f=1.3)
    MON("sea_beast", "Seeungeheuer", "brute", 7, 8, "#2F5F7F")
    MON("ghost_gunner", "Geisterkanonier", "ranged", 7, 8, "#8FB8C8", abilities="line")
    MON("drowned", "Ertrunkener", "swarm", 7, 8, "#4F7A7A")
    # Akt 8
    MON("skeleton", "Skelett", "melee", 8, 9, "#D8D8C8")
    MON("ghoul", "Ghul", "hunter", 8, 9, "#6F7F5F")
    MON("necromancer", "Nekromant", "summoner", 8, 9, "#4A2F6B", elite=1)
    MON("bone_bomber", "Knochenbomber", "explosive", 8, 9, "#C8C0A0")
    # Akt 9
    MON("stone_golem", "Steingolem", "brute", 9, 10, "#8A8F94")
    MON("sky_guard", "Himmelswächter", "guardian", 9, 10, "#B8C8E8")
    MON("angel_construct", "Engelskonstrukt", "ranged", 9, 10, "#F4E8B8", abilities="rain")
    MON("storm_construct", "Sturmkonstrukt", "elemental", 9, 10, "#7FA8E8")
    # Akt 10
    MON("imp", "Imp", "swarm", 10, 10, "#B8232F", speed=190)
    MON("hellhound", "Höllenhund", "hunter", 10, 10, "#8F2A1F")
    MON("demon_brute", "Dämonenbrecher", "demon", 10, 10, "#7A1F2A", hp_f=2.4, abilities="slam,charge")
    MON("demon_caster", "Dämonenzauberer", "demon", 10, 11, "#9A2F6B", hp_f=1.0, range=480, proj_speed=420, abilities="rain")
    MON("demon_knight", "Dämonenritter", "demon", 10, 11, "#5A1A2A", elite=1, abilities="nova,shield")
    # Akt 11
    MON("corrupt_orc", "Verderbter Ork", "melee", 11, 11, "#5F7F3F", hp_f=1.5)
    MON("corrupt_orc_archer", "Verderbter Orkschütze", "ranged", 11, 11, "#6F8F4F")
    MON("orc_ghost", "Orkgeist", "elemental", 11, 11, "#B8D8C8", abilities="")
    MON("possessed_totem", "Besessenes Totem", "totem", 11, 11, "#7A5A3A")
    MON("corrupt_brute", "Verderbter Brecher", "brute", 11, 11, "#4F6F2F")
    MON("spirit_healer", "Geisterheiler", "healer", 11, 11, "#9FD8B8", elite=1)
    # Tempel/Ruinen (alle Akte)
    MON("raven_shade", "Rabenschatten", "hunter", 1, 11, "#3A3A4A", hp_f=1.2)
    MON("relic_construct", "Relikt-Konstrukt", "guardian", 1, 11, "#A8A070", abilities="shield,line")
    # Bosse (Spec 15, Namen frei erfunden)
    BOSS("boss_grukk", "Goblinkönig Grukk", 1, 1, 3.0, 1.3, "summon,nova,slam", "#4F7A1F")
    BOSS("boss_giant_frog", "Riesenfrosch", 2, 2, 2.0, 1.2, "charge,slam", "#2F7F4F", rad=52)
    BOSS("boss_morga", "Sumpfhexe Morga", 2, 2, 3.5, 1.4, "pools,summon,slam", "#5F2F7F", rng=450, proj=380)
    BOSS("boss_scorpion_queen", "Skorpionkönigin", 3, 3, 2.2, 1.3, "charge,summon", "#B8621D", rad=56)
    BOSS("boss_ahmet_ra", "Pharao Ahmet-Ra", 3, 3, 4.0, 1.5, "summon,rain,nova", "#D8B84A", rng=400, proj=380)
    BOSS("boss_ice_giant", "Eisriese", 4, 4, 2.4, 1.4, "slam,nova", "#A8D0F0", rad=72)
    BOSS("boss_kryos", "Frostdrache Kryos", 4, 4, 4.5, 1.6, "cone,pools,slam", "#5FA8E8", rad=80)
    BOSS("boss_lava_colossus", "Lavakoloss", 5, 5, 2.6, 1.5, "slam,pools", "#C8441A", rad=80)
    BOSS("boss_pyron", "Glutfürst Pyron", 5, 5, 5.0, 1.7, "pools,enrage,nova", "#E8641E")
    BOSS("boss_ancient_ent", "Uralter Ent", 6, 6, 2.8, 1.4, "slam,summon", "#4F6F2F", rad=84)
    BOSS("boss_vexara", "Waldgeist Vexara", 6, 6, 5.0, 1.6, "heal_ally,summon,shield", "#3F8F5F", rng=420, proj=360)
    BOSS("boss_kraken", "Kraken", 7, 7, 3.0, 1.6, "slam,rain", "#2F6F8F", rad=90, speed=0)
    BOSS("boss_dreadmar", "Geisterkapitän Dreadmar", 7, 7, 5.2, 1.8, "line,summon,rain", "#8FC8D8", rng=500, proj=500)
    BOSS("boss_bone_lord", "Knochenfürst", 8, 8, 3.2, 1.7, "summon,slam", "#D8D0B0")
    BOSS("boss_mortheus", "Lich Mortheus", 8, 8, 5.5, 1.9, "summon,line,shield", "#6F3FA8", rng=500, proj=450)
    BOSS("boss_shield_guard", "Schildwächter", 9, 9, 3.4, 1.8, "shield,nova", "#A8B8D8", rad=76)
    BOSS("boss_auriel", "Himmelsrichter Auriel", 9, 9, 6.0, 2.0, "shield,line,rain,nova", "#F4E8A8", rng=500, proj=500)
    BOSS("boss_gate_a", "Torhüter der ersten Stufe", 10, 10, 3.5, 1.9, "nova,slam", "#9A2F2F")
    BOSS("boss_gate_b", "Torhüter der zweiten Stufe", 10, 10, 4.2, 2.1, "nova,slam,charge", "#8A1F3F")
    BOSS("boss_gate_c", "Torhüter der dritten Stufe", 10, 10, 5.0, 2.3, "nova,line,charge,shield", "#7A1F5F")
    BOSS("boss_gate_warden", "Panoptyca-Torwächter", 10, 10, 6.0, 2.6, "nova,slam,line,summon,enrage,shield", "#5A0F3F", rad=96)
    BOSS("boss_grol_dar", "Orkschamane Grol'Dar", 11, 11, 4.5, 2.0, "summon,pools,heal_ally", "#5F8F3F", rng=420, proj=380)
    BOSS("boss_velkaron", "Velkaron, Gefallener Orkkönig", 11, 11, 6.0, 2.6, "summon,pools,nova,slam,enrage,shield", "#3F5F1F", rad=100)
    BOSS("boss_raven_lord", "Rabenfürst", 1, 11, 4.0, 1.5, "summon,charge,slam,enrage", "#2A2A3A", rad=72)
    BOSS("boss_relic_guardian", "Reliktwächter", 1, 11, 3.5, 2.5, "slam,line,cone,enrage", "#A89860", rad=80)
    write_tsv("monsters.tsv", MONSTER_COLS, MONSTERS,
              "hp_f/dmg_f = Faktoren auf Basiswerte (Spec 15); Tier-Multiplikatoren stehen in tiers.tsv.")


# ---------------------------------------------------------------------------
# maps.tsv (Spec 14): 130 Story + 15 Tempel + 16 Ruinen = 161
# ---------------------------------------------------------------------------
MAP_COLS = ["id", "name", "kind", "act", "index", "chapter", "level", "w", "h", "seed", "boss", "boss_map",
            "max_heroes", "min_player_level", "density", "stone", "stone_count", "tint"]
ACT_NAMES = ["Grünland", "Moorlande", "Heiße Dünen", "Eisgebirge", "Glutkrater", "Fluchwald", "Sturmküste",
             "Katakomben", "Himmelsruinen", "Panoptyca-Tor", "Velkaron"]
ACT_TINTS = ["#7FA84F", "#4F7F5F", "#D2AE6B", "#9FC8E8", "#C8502A", "#4A6B3A", "#4F7F9F", "#8A8A7A", "#C8D4E8",
             "#8A2F3F", "#5A6F3A"]
SPOT_NAMES = ["Eingang", "Pfad", "Lichtung", "Hügel", "Senke", "Ruinen", "Grenzland", "Tiefe", "Hain", "Pass"]
# Zwischenboss (x-5) und Akt-Boss (x-10) je Akt
ACT_BOSSES = {1: (None, "boss_grukk"), 2: ("boss_giant_frog", "boss_morga"), 3: ("boss_scorpion_queen", "boss_ahmet_ra"),
              4: ("boss_ice_giant", "boss_kryos"), 5: ("boss_lava_colossus", "boss_pyron"),
              6: ("boss_ancient_ent", "boss_vexara"), 7: ("boss_kraken", "boss_dreadmar"),
              8: ("boss_bone_lord", "boss_mortheus"), 9: ("boss_shield_guard", "boss_auriel")}
MAPS = []


def chapter_of(act):
    return 1 if act <= 3 else 2 if act <= 6 else 3 if act <= 9 else 4 if act == 10 else 5


def add_map(id, name, kind, act, index, level, boss="", boss_map=0, heroes=1, minlvl=1, density=1.0, stone="",
            stone_count=0, tint="#808080", size=None, seed_base=0):
    n = len(MAPS)
    if size is None:
        size = (min(200, 60 + 12 * act), min(120, 40 + 7 * act)) if act else (80, 60)
        if act == 11:
            size = (240, 140)
    MAPS.append(dict(id=id, name=name, kind=kind, act=act, index=index, chapter=chapter_of(act) if act else 0,
                     level=level, w=size[0], h=size[1], seed=1000 + seed_base + n * 7919, boss=boss,
                     boss_map=int(boss_map), max_heroes=heroes, min_player_level=minlvl, density=density,
                     stone=stone, stone_count=stone_count, tint=tint))


def gen_maps():
    MAPS.clear()
    # Akt 1: 5 Maps, Level 1..5
    for i in range(1, 6):
        boss = "boss_grukk" if i == 5 else ""
        add_map("1-%d" % i, ("Revier von Goblinkönig Grukk" if boss else "%s: %s" % (ACT_NAMES[0], SPOT_NAMES[i - 1])),
                "story", 1, i, i, boss=boss, boss_map=bool(boss), minlvl=1, density=0.8 if boss else 1.0,
                stone="stone_magic_act1" if boss else "", stone_count=1 if boss else 0, tint=ACT_TINTS[0])
    # Akt 2..9: je 10 Maps
    for act in range(2, 10):
        lo, hi = 5 + (act - 2) * 10, 15 + (act - 2) * 10
        mid, big = ACT_BOSSES[act]
        for i in range(1, 11):
            level = lo + rnd((i - 1) * (hi - lo) / 9.0)
            boss = mid if i == 5 else big if i == 10 else ""
            bname = next((m["name"] for m in MONSTERS if m["id"] == boss), "") if boss else ""
            add_map("%d-%d" % (act, i), ("Revier: %s" % bname if boss else "%s: %s" % (ACT_NAMES[act - 1], SPOT_NAMES[i - 1])),
                    "story", act, i, level, boss=boss, boss_map=bool(boss), minlvl=max(1, int(level * 0.4)),
                    density=0.8 if boss else 1.0, stone="stone_magic_act%d" % act if boss else "",
                    stone_count=(1 if i == 5 else 2) if boss else 0, tint=ACT_TINTS[act - 1])
    # Akt 10: A/B/C je 10 Maps (Stufen 85-90, 90-95, 95-100)
    gates = {"A": "boss_gate_a", "B": "boss_gate_b", "C": "boss_gate_c"}
    for si, stage in enumerate("ABC"):
        lo = 85 + si * 5
        for i in range(1, 11):
            level = lo + rnd((i - 1) * 5 / 9.0)
            boss = ""
            if i in (3, 6, 10):
                boss = "boss_gate_warden" if (stage == "C" and i == 10) else gates[stage]
            add_map("10%s-%d" % (stage, i), ("Panoptyca-Tor %s: Boss" % stage if boss else "Panoptyca-Tor %s: %s" % (stage, SPOT_NAMES[i - 1])),
                    "story", 10, i + si * 10, level, boss=boss, boss_map=bool(boss), minlvl=max(1, int(level * 0.4)),
                    density=0.8 if boss else 1.0, stone="stone_magic_act10" if boss else "",
                    stone_count=si + 1 if boss else 0, tint=ACT_TINTS[9])
    # Akt 11: 15 Maps, Level 100-110, bis 2 Helden
    for i in range(1, 16):
        level = 100 + rnd((i - 1) * 10 / 14.0)
        boss = "boss_grol_dar" if i == 5 else "boss_velkaron" if i in (10, 15) else ""
        add_map("11-%d" % i, ("Velkaron: Boss" if boss else "%s: %s" % (ACT_NAMES[10], SPOT_NAMES[(i - 1) % 10])),
                "story", 11, i, level, boss=boss, boss_map=bool(boss), heroes=2, minlvl=max(1, int(level * 0.4)),
                density=0.8 if boss else 1.0, stone="stone_magic_act11" if boss else "",
                stone_count=(1 if i == 5 else 2) if boss else 0, tint=ACT_TINTS[10])
    # Schattentempel: 15 Stufen, 2 Helden, tankige Monster; Boss alle 10 Stufen
    for i in range(1, 16):
        boss = "boss_raven_lord" if i == 10 else ""
        add_map("T-%d" % i, "Schattentempel Stufe %d" % i, "temple", 0, i, 20 + i * 5, boss=boss, boss_map=bool(boss),
                heroes=2, minlvl=int(BAL["temple_unlock_level"]), density=1.2, tint="#3A3A4A", size=(80, 60))
    # Ruinen: 16 Stufen (Alte Ruinen + Overdrive), hohe Angriffswerte
    for i in range(1, 17):
        boss = "boss_relic_guardian" if i in (8, 16) else ""
        add_map("R-%d" % i, ("Ruinen Stufe %d" % i) if i <= 8 else ("Overdrive-Ruinen Stufe %d" % (i - 8)), "ruins", 0, i,
                30 + i * 4, boss=boss, boss_map=bool(boss), heroes=2, minlvl=25, density=1.0, tint="#A89860", size=(100, 70))
    write_tsv("maps.tsv", MAP_COLS, MAPS)


# ---------------------------------------------------------------------------
# shop.tsv (Spec 13)
# ---------------------------------------------------------------------------
SHOP_COLS = ["id", "name", "desc", "tab", "kind", "item", "qty", "currency", "price", "limit", "period",
             "price_growth", "min_player_level"]
SHOP = []


def SH(id, name, desc, tab, kind, item="", qty=1, cur="gold", price=0, limit=0, period="none", growth=0, minlvl=1):
    SHOP.append(dict(id=id, name=name, desc=desc, tab=tab, kind=kind, item=item, qty=qty, currency=cur, price=price,
                     limit=limit, period=period, price_growth=growth, min_player_level=minlvl))


def gen_shop():
    SHOP.clear()
    # Gratis
    SH("free_diamonds_daily", "Tägliche Diamanten", "1000 Diamanten, einmal täglich.", "free", "currency", "diamonds", 1000, limit=1, period="daily")
    SH("free_diamonds_weekly", "Wöchentliche Diamanten", "2000 Diamanten, einmal pro Woche.", "free", "currency", "diamonds", 2000, limit=1, period="weekly")
    SH("free_diamonds_monthly", "Monatliche Diamanten", "5000 Diamanten, einmal im Monat.", "free", "currency", "diamonds", 5000, limit=1, period="monthly")
    SH("free_random_ring", "Gratis-Ring", "Ein zufälliger Ring, täglich gratis.", "free", "random_ring", limit=1, period="daily")
    SH("free_random_necklace", "Gratis-Halskette", "Eine zufällige Halskette, wöchentlich gratis.", "free", "random_necklace", limit=1, period="weekly")
    SH("free_random_book", "Gratis-Skillbuch", "Ein zufälliges Skillbuch, täglich gratis.", "free", "random_book", limit=1, period="daily")
    SH("free_tickets", "Tägliche Rabenfedern", "30 Rabenfedern für Tempel-Tickets.", "free", "currency", "raven_feathers", 30, limit=1, period="daily")
    # Diamanten
    for t, price in ((1, 5000), (2, 15000), (3, 40000), (4, 100000), (5, 250000)):
        SH("dia_wings_%s" % TIERS[t], "Flügel (%s)" % TIER_NAMES[t], "Ein Paar Flügel, begrenzte Menge.", "diamonds", "wings",
           "wings_%s" % TIERS[t], 1, "diamonds", price, 1, "monthly", minlvl=20)
    SH("dia_slots_inv", "Inventar +20", "20 zusätzliche Inventarplätze.", "diamonds", "slots_inv", "", 20, "diamonds", 500, growth=0.25)
    SH("dia_slots_hero", "Heldenplatz-Ticket", "Ein zusätzlicher Heldenplatz (max. 16).", "diamonds", "slots_hero", "", 1, "diamonds", 10000, growth=0.5, minlvl=15)
    SH("dia_mana_stones", "Manasteine x50", "50 Manasteine, einmal täglich.", "diamonds", "item", "stone_enchant_unique", 50, "diamonds", 500, 1, "daily")
    SH("dia_mana_stones_10", "Manasteine x10", "10 Manasteine für Verzauberungen.", "diamonds", "item", "stone_enchant_unique", 10, "diamonds", 100, 5, "daily")
    SH("dia_mana_core", "Manakern x10", "10 Manakerne (Boss-Verzauberung).", "diamonds", "item", "stone_enchant_boss", 10, "diamonds", 400, 3, "daily", minlvl=40)
    # Zufallskisten
    SH("rnd_ring", "Zufalls-Ring", "Ein Ring, Tier-Verteilung wird angezeigt.", "random", "random_ring", "", 1, "diamonds", 200)
    SH("rnd_necklace", "Zufalls-Halskette", "Eine Halskette, Tier-Verteilung wird angezeigt.", "random", "random_necklace", "", 1, "diamonds", 200)
    SH("rnd_book", "Zufalls-Skillbuch", "Ein Skillbuch, Grad-Verteilung wird angezeigt.", "random", "random_book", "", 1, "diamonds", 150, 10, "daily")
    # Allgemein
    for kind, label in (("weapon", "Waffen"), ("armor", "Rüstungs"), ("jewel", "Schmuck")):
        for t, price in ((1, 20000), (2, 80000), (3, 300000)):
            SH("gen_enh_%s_%s" % (kind, TIERS[t]), "%s-Verbesserungsstein x10 (%s)" % (label, TIER_NAMES[t]),
               "Verbesserungssteine für Gold, Tageslimit.", "general", "item", "stone_enh_%s_%s" % (kind, TIERS[t]), 10,
               "gold", price, 5, "daily", minlvl=5 * t)
    SH("gen_slots_inv", "Inventar +20", "20 zusätzliche Inventarplätze (Gold).", "general", "slots_inv", "", 20, "gold", 50000, growth=0.25)
    SH("gen_slots_hero", "Heldenplatz-Ticket", "Ein zusätzlicher Heldenplatz (max. 16).", "general", "slots_hero", "", 1, "gold", 5000000, growth=1.0, minlvl=15)
    SH("gen_bag_normal", "Lederbeutel", "Tragetasche mit +5 Kistenplätzen.", "general", "item", "bag_normal", 1, "gold", 5000)
    SH("gen_bag_magic", "Reisetasche", "Tragetasche mit +9 Kistenplätzen.", "general", "item", "bag_magic", 1, "gold", 50000, minlvl=10)
    SH("gen_weak_mana", "Schwacher Manastein", "Für Selten-Verzauberungen, max. 5 pro Tag.", "general", "item", "stone_enchant_rare", 1, "gold", 25000, 5, "daily", minlvl=10)
    SH("gen_essence_magic", "Gute Essenz", "Trank-Essenz, absichtlich teuer.", "general", "item", "mat_essence_magic", 1, "gold", 100000, 5, "daily", minlvl=5)
    SH("gen_essence_rare", "Reine Essenz", "Trank-Essenz, absichtlich teuer.", "general", "item", "mat_essence_rare", 1, "gold", 1000000, 3, "daily", minlvl=20)
    SH("gen_glass_vial", "Glasphiole x10", "Zutat für Tränke.", "general", "item", "special_glass_vial", 10, "gold", 1000)
    # Skillbuecher
    for c, cid in enumerate(CLASSES):
        SH("book_normal_%s" % cid, "Normal-Buch (%s)" % CLASS_NAMES[c], "Skillbuch Grad Normal für %s." % CLASS_NAMES[c],
           "books", "book_class", cid, 1, "gold", 30000, 30, "daily")
    SH("book_random_rare", "Zufälliges Seltenes Buch", "Wöchentlich bis zu 20.", "books", "random_book", "", 1, "gold", 5000000, 20, "weekly", minlvl=20)
    SH("book_random_unique", "Zufälliges Einzigartiges Buch", "Wöchentlich bis zu 5.", "books", "random_book", "", 1, "gold", 100000000, 5, "weekly", minlvl=40)
    for c, cid in enumerate(CLASSES):
        SH("book_unique_%s" % cid, "Einzigartiges Buch (%s)" % CLASS_NAMES[c], "Gewähltes Einzigartig-Buch, wöchentlich bis zu 3.",
           "books", "book_class", cid, 1, "gold", 500000000, 3, "weekly", minlvl=40)
    # Rabenhaendler
    SH("raven_bag_unique", "Drachenleder-Rucksack", "Tragetasche mit +19 Kistenplätzen.", "raven", "item", "bag_unique", 1, "raven_coins", 500, minlvl=15)
    SH("raven_bag_boss", "Rucksack des Plünderers", "Tragetasche mit +25 Kistenplätzen.", "raven", "item", "bag_boss", 1, "raven_coins", 2500, minlvl=30)
    SH("raven_platinum", "Platinbarren x10", "Material für Selten-Ausrüstung.", "raven", "item", "mat_metal_rare", 10, "raven_coins", 100, 10, "daily", minlvl=15)
    SH("raven_mithril", "Mithrilbarren x10", "Material für Einzigartig-Ausrüstung.", "raven", "item", "mat_metal_unique", 10, "raven_coins", 400, 5, "daily", minlvl=30)
    SH("raven_boss_jewel", "Boss-Schmuckstein x3", "Boss-Schmuck-Verbesserungssteine.", "raven", "item", "stone_enh_jewel_boss", 3, "raven_coins", 1500, 3, "weekly", minlvl=40)
    SH("raven_unique_armor", "Einzigartig-Rüstungssteine x10", "Rüstungs-Verbesserungssteine.", "raven", "item", "stone_enh_armor_unique", 10, "raven_coins", 300, 5, "weekly", minlvl=30)
    SH("raven_book", "Zufallsbuch", "Ein zufälliges Skillbuch.", "raven", "random_book", "", 1, "raven_coins", 800, 3, "weekly", minlvl=20)
    SH("raven_chest_unique", "Einzigartig-Kiste", "Eine Kiste mit Einzigartig-Inhalt.", "raven", "chest", "unique", 1, "raven_coins", 2500, 3, "weekly", minlvl=30)
    SH("raven_chest_boss", "Boss-Kiste", "Eine Kiste mit Boss-Inhalt.", "raven", "chest", "boss", 1, "raven_coins", 25000, 1, "weekly", minlvl=50)
    SH("raven_gold", "Goldpaket (5 Mio.)", "5 Millionen Gold.", "raven", "currency", "gold", 5000000, "raven_coins", 5000, 1, "weekly", minlvl=30)
    write_tsv("shop.tsv", SHOP_COLS, SHOP,
              "Spalte item: Item-ID, Klasse (book_class), Waehrung (currency) oder Tier (chest) je nach kind.")


# ---------------------------------------------------------------------------
# traits.tsv (Spec 20): Effekt = Feldname aus CommanderMods (src/rules/rules.h)
# ---------------------------------------------------------------------------
TRAIT_COLS = ["id", "name", "desc", "tree", "max_points", "effect", "per_point", "row", "requires"]
TRAITS = []


def TR(id, name, desc, tree, maxp, effect, per, row, req=""):
    TRAITS.append(dict(id=id, name=name, desc=desc, tree=tree, max_points=maxp, effect=effect, per_point=per, row=row,
                       requires=req))


def commander_fields():
    text = open(os.path.join(ROOT, "src", "rules", "rules.h"), encoding="utf-8").read()
    body = text[text.index("typedef struct CommanderMods"):]
    body = body[:body.index("} CommanderMods;")]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    names = set()
    for m in re.finditer(r"\b(?:float|int)\s+([\w\s,]+);", body):
        names.update(n.strip() for n in m.group(1).split(","))
    return names


def gen_traits():
    TRAITS.clear()
    # Baum 0: Exploration
    TR("exp_quantity", "Kistenmenge", "Mehr Kistenwürfe pro Kill (+2 % je Punkt).", 0, 10, "drop_qty_pct", 2, 0)
    TR("exp_quality", "Kistenqualität", "Höhere Kisten-Raritäten (+1,5 % je Punkt).", 0, 10, "drop_quality_pct", 1.5, 1, "exp_quantity")
    TR("exp_density", "Schlachtfeld", "Mehr Monster je Map (+4 % je Punkt, bis +56 %).", 0, 14, "density_pct", 4, 1)
    TR("exp_dupe", "Doppelkisten", "Chance auf Doppelkisten (+0,2 % je Punkt, bis 1 %).", 0, 5, "dupe_chest_pct", 0.2, 2, "exp_quality")
    TR("exp_unique", "Einzigartigen-Jäger", "Chance auf 2 statt 1 Einzigartigen (+5 % je Punkt).", 0, 4, "unique_double_pct", 5, 2, "exp_density")
    TR("exp_magic_stone", "Steinsucher", "Mehr Magiesteine (+5 % je Punkt).", 0, 4, "magic_stone_pct", 5, 3, "exp_unique")
    # Baum 1: Economy
    TR("eco_craft", "Handwerkskunst", "Herstellung wird günstiger (-2 % je Punkt).", 1, 10, "craft_cost_pct", -2, 0)
    TR("eco_shop", "Verhandlungsgeschick", "Shop-Rabatt (+1 % je Punkt).", 1, 10, "shop_discount_pct", 1, 1, "eco_craft")
    TR("eco_gold", "Goldnase", "Mehr Gold (+3 % je Punkt).", 1, 10, "gold_pct", 3, 1)
    TR("eco_exp", "Lehrmeister", "Mehr Erfahrung (+3 % je Punkt).", 1, 10, "exp_pct", 3, 2, "eco_gold")
    TR("eco_bounty", "Auftragsbrett", "Zusätzlicher Kopfgeld-Slot je Punkt.", 1, 5, "bounty_slots", 1, 3, "eco_exp")
    # Baum 2: Faith
    TR("faith_death", "Rasche Wiederkehr", "Kürzere Todeszeit (-6 % je Punkt).", 2, 10, "death_time_pct", -6, 0)
    TR("faith_retry", "Schneller Neustart", "Kürzere Wartezeit vor dem Retry (-4 % je Punkt).", 2, 6, "retry_time_pct", -4, 1, "faith_death")
    TR("faith_cap", "Große Taschen", "Mehr Kistenkapazität (+1 je Punkt).", 2, 10, "chest_cap_flat", 1, 1)
    TR("faith_storage", "Kistenlager", "Mehr freies Lager für Normal-/Magisch-Kisten (+100 je Punkt).", 2, 9, "storage_limit_bonus", 100, 2, "faith_cap")
    # Baum 3: Leadership
    TR("lead_move", "Antreiber", "Mehr Lauftempo (+0,6 % je Punkt).", 3, 10, "move_speed_pct", 0.6, 0)
    TR("lead_attack", "Schlachtruf", "Flacher Schaden (+6 je Punkt).", 3, 11, "flat_attack", 6, 1, "lead_move")
    TR("lead_armor", "Eiserne Disziplin", "Mehr Rüstung (+0,7 % je Punkt).", 3, 10, "armor_pct", 0.7, 1)
    TR("lead_spellres", "Geistige Stärke", "Mehr Zauberwiderstand (+0,7 % je Punkt).", 3, 10, "spellres_pct", 0.7, 2, "lead_armor")
    TR("lead_cdr", "Taktische Planung", "Kürzere Abklingzeiten (+0,7 % je Punkt).", 3, 10, "cdr_pct", 0.7, 2, "lead_attack")
    TR("lead_potion", "Trankvorrat", "Tränke füllen sich passiv (+0,1 % pro Sekunde je Punkt).", 3, 10, "potion_regen", 0.1, 3, "lead_cdr")
    fields = commander_fields()
    for t in TRAITS:
        assert t["effect"] in fields, ("Trait-Effekt nicht in CommanderMods", t["effect"])
    write_tsv("traits.tsv", TRAIT_COLS, TRAITS, "effect = Feldname in CommanderMods (src/rules/rules.h).")


# ---------------------------------------------------------------------------
# quests.tsv (Spec 20)
# ---------------------------------------------------------------------------
QUEST_COLS = ["id", "text", "kind", "param", "count_min", "count_max", "main", "reward_cmd_exp", "reward_gold",
              "reward_diamonds"]
QUESTS = []


def Q(id, text, kind, param="", cmin=1, cmax=None, main=0, exp=0, gold=0, dia=0):
    QUESTS.append(dict(id=id, text=text, kind=kind, param=param, count_min=cmin, count_max=cmin if cmax is None else cmax,
                       main=main, reward_cmd_exp=exp, reward_gold=gold, reward_diamonds=dia))


def gen_quests():
    QUESTS.clear()
    # Kopfgelder (Bounty)
    Q("bounty_kill_any", "Besiege {n} Monster.", "kill_any", "", 100, 400, 0, 200, 20000, 20)
    Q("bounty_kill_magic", "Besiege {n} Magische Monster.", "kill_tier", "magic", 20, 80, 0, 300, 30000, 30)
    Q("bounty_kill_rare", "Besiege {n} Seltene Monster.", "kill_tier", "rare", 5, 20, 0, 500, 60000, 50)
    Q("bounty_kill_unique", "Besiege {n} Einzigartige Monster.", "kill_tier", "unique", 1, 3, 0, 800, 150000, 100)
    for arch, label, lo, hi in (("melee", "Nahkämpfer", 40, 120), ("ranged", "Fernkämpfer", 30, 100), ("swarm", "Schwarmmonster", 60, 200),
                                ("brute", "Brecher", 15, 50), ("hunter", "Jäger", 25, 80), ("summoner", "Beschwörer", 10, 30),
                                ("healer", "Heiler", 10, 30), ("explosive", "Sprenger", 20, 60), ("elemental", "Elementare", 20, 60),
                                ("guardian", "Wächter", 15, 40), ("demon", "Dämonen", 15, 50)):
        Q("bounty_arch_%s" % arch, "Besiege {n} %s." % label, "kill_arch", arch, lo, hi, 0, 250, 25000, 25)
    Q("bounty_clear_map", "Schließe {n} Maps ab.", "clear_map", "", 1, 3, 0, 250, 30000, 30)
    Q("bounty_craft", "Stelle {n} Gegenstände her.", "craft", "", 1, 5, 0, 200, 25000, 20)
    Q("bounty_chests", "Öffne {n} Kisten.", "open_chests", "", 10, 60, 0, 200, 20000, 20)
    Q("bounty_enhance", "Verbessere Gegenstände {n} Mal.", "enhance", "", 3, 10, 0, 250, 30000, 30)
    # Hauptquests (linear)
    Q("main_magic_100", "Besiege {n} Magische Monster.", "kill_tier", "magic", 100, 100, 1, 1000, 100000, 100)
    Q("main_mystery_box", "Stelle {n} Mysteriöse Kiste her.", "craft", "", 1, 1, 1, 1500, 150000, 150)
    Q("main_chests_500", "Öffne {n} Kisten.", "open_chests", "", 500, 500, 1, 2000, 200000, 150)
    Q("main_rare_1000", "Besiege {n} Seltene Monster.", "kill_tier", "rare", 1000, 1000, 1, 5000, 500000, 300)
    Q("main_level_20", "Erreiche Kommandeur-Level {n}.", "reach_level", "", 20, 20, 1, 8000, 800000, 300)
    Q("main_clear_50", "Schließe {n} Maps ab.", "clear_map", "", 50, 50, 1, 10000, 1000000, 400)
    Q("main_unique_100", "Besiege {n} Einzigartige Monster.", "kill_tier", "unique", 100, 100, 1, 20000, 2000000, 500)
    Q("main_rare_17600", "Besiege {n} Seltene Monster.", "kill_tier", "rare", 17600, 17600, 1, 100000, 20000000, 2000)
    write_tsv("quests.tsv", QUEST_COLS, QUESTS, "text: {n} = Zielzahl; param: Tier-ID bzw. Archetyp-ID.")


# ---------------------------------------------------------------------------
# balance.cfg
# ---------------------------------------------------------------------------
def gen_balance():
    path = os.path.join(OUT, "balance.cfg")
    os.makedirs(OUT, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("# Panoptix balance.cfg - erzeugt von tools/gen_data.py\n")
        f.write("# Zeilen 'schluessel = wert'; Schluessel siehe BALANCE_FIELDS in src/data/data.h\n")
        for k, v in load_balance_fields():
            f.write("%s = %s\n" % (k, fmt(v)))
    print("%-18s %4d Zeilen" % ("balance.cfg", len(load_balance_fields())))


# ---------------------------------------------------------------------------
# lang/en.tsv (deutscher Quelltext -> Englisch, siehe tr() in src/core/i18n.h)
# ---------------------------------------------------------------------------
LANG_PAIRS = """
Normal|Normal
Magisch|Magic
Selten|Rare
Einzigartig|Unique
Boss|Boss
Legendär|Legendary
Ritter|Knight
Krieger|Warrior
Magier|Wizard
Bogenschütze|Archer
Assassine|Assassin
Schamane|Shaman
Priester|Priest
Allgemein|General
Alle Klassen|All classes
Klasse|Class
Tank / physischer DPS|Tank / physical DPS
Frontkämpfer, Support/DPS|Frontliner, support/DPS
Flächenmagie / Burst|Area magic / burst
Einzelziel-DPS, hohes Tempo|Single-target DPS, high speed
Burst, Mobilität, Gift|Burst, mobility, poison
Totems, Debuffs, Support|Totems, debuffs, support
Heiler, Buffer, leichter DPS|Healer, buffer, light DPS
Mensch|Human
Ork|Orc
Elf|Elf
Dunkelelf|Dark elf
Schwert|Sword
Axt|Axe
Stab|Staff
Bogen|Bow
Dolch|Dagger
Totemstab|Totem staff
Zepter|Scepter
Schild|Shield
Kampfbanner|War banner
Orb|Orb
Köcher|Quiver
Wurfmessergurt|Throwing-knife belt
Totem|Totem
Gebetbuch|Prayer book
Waffe|Weapon
Sekundärwaffe|Secondary weapon
Helm|Helmet
Brustpanzer|Chestplate
Handschuhe|Gloves
Stiefel|Boots
Tragetasche|Carry bag
Halskette|Necklace
Ring|Ring
Ring 1|Ring 1
Ring 2|Ring 2
Trank 1|Potion 1
Trank 2|Potion 2
Trank 3|Potion 3
Flügel|Wings
Rüstung|Armor
Trank|Potion
Material|Material
Stein|Stone
Spezial|Special
Heiltrank|Healing potion
Manatrank|Mana potion
Eile|Haste
Eisenhaut|Iron skin
Raserei|Frenzy
Gold|Gold
Diamanten|Diamonds
Kommando-Credits|Command credits
Raben-Münzen|Raven coins
Rabenfedern|Raven feathers
Angriff & Zauber (flach)|Attack & spell (flat)
Angriffsschaden (flach)|Attack damage (flat)
Zauberschaden (flach)|Spell damage (flat)
Angriffsschaden|Attack damage
Zauberschaden|Spell damage
Mehr Angriffsschaden|More attack damage
Mehr Zauberschaden|More spell damage
Krit-Chance|Crit chance
Krit-Schaden|Crit damage
Angriffstempo|Attack speed
Zaubertempo|Cast speed
Lauftempo|Movement speed
Abklingzeit-Red.|Cooldown red.
Zauberwiderstand|Spell resistance
Max-Leben|Max life
Max-Mana|Max mana
Lebensregeneration|Life regeneration
Manaregeneration|Mana regeneration
Kistenkapazität|Chest capacity
Angriffs-Abzug|Attack reduction
Zauber-Abzug|Spell reduction
Schadensminderung|Damage reduction
Projektile|Projectiles
Durchdringung|Pierce
Heilkraft|Healing power
Gegner erleiden mehr|Enemies take more
Gegner: weniger Angriff|Enemies: less attack
Gegner: weniger Zauber|Enemies: less spell
Manakosten|Mana cost
Max. CDR|Max. CDR
Abklingzeit|Cooldown
Fuellung|Fill
Füllung|Fill
Grad|Grade
Hauptwert|Main value
Ja|Yes
Nein|No
Kein Held vorhanden - in der Heldenliste (F1) erstellen.|No hero available - create one in the hero list (F1).
Ladungen|Charges
Level|Level
Lv|Lv
Mana|Mana
Mindestlevel|Minimum level
Nicht möglich|Not possible
Reichweite|Range
Schaden|Damage
Set|Set
Slot ist frei|Slot is empty
Verbessert|Enhanced
Verkaufswert|Sell value
Verzauberung|Enchantment
Wirkung|Effect
besser als angelegt:|better than equipped:
schlechter als angelegt:|worse than equipped:
Heldenliste|Hero list
Kommandoansicht|Command view
Weltkarte|World map
Kisten einsammeln|Collect chests
Menü|Menu
Inventar|Inventory
Herstellung|Crafting
Skills|Skills
Auto|Auto
Manuell|Manual
Einstellungen|Settings
Hilfe|Help
Beenden|Quit
Fortsetzen|Resume
Neues Spiel|New game
Spiel laden|Load game
Speichern|Save
Laden|Load
Zurück|Back
Weiter|Continue
Abbrechen|Cancel
Bestätigen|Confirm
Schließen|Close
Übernehmen|Apply
Zurücksetzen|Reset
Ok|OK
Held erstellen|Create hero
Name|Name
Aussehen|Appearance
Rolle|Role
Beschreibung|Description
Held|Hero
Helden|Heroes
Heldenplätze|Hero slots
Status|Status
Tot|Dead
Lebendig|Alive
Auf Map|On map
Im Lager|In camp
Todes-Timer|Death timer
Wiederbeleben|Revive
Stärke|Strength
Geschicklichkeit|Dexterity
Intelligenz|Intelligence
Punkte verteilen|Assign points
Verfügbare Punkte|Available points
Leben|Health
Erfahrung|Experience
EXP-Rate|EXP rate
Kisten|Chests
Materialien|Materials
Waffen|Weapons
Sekundärwaffen|Secondary weapons
Tränke|Potions
Schmuck|Jewelry
Flügel/Taschen|Wings/bags
Steine/Spezial|Stones/special
Suche|Search
Filter|Filter
Sortierung|Sorting
Tier|Tier
Anlegen|Equip
Ablegen|Unequip
Zerlegen|Dismantle
Verkaufen|Sell
Sperren|Lock
Entsperren|Unlock
Verbessern|Enhance
Verzaubern|Enchant
Umwandeln|Convert
Öffnen|Open
Alle Kisten öffnen|Open all chests
Einsammeln|Collect
Alle einsammeln|Collect all
Kapazität|Capacity
Lager erweitern|Expand storage
Herstellen|Craft
Zutaten|Ingredients
Kosten|Cost
Menge|Quantity
Erfolgschance|Success chance
Neu würfeln|Reroll
Siegel|Seal
Gesperrt|Locked
Besser als ausgerüstet|Better than equipped
Schlechter als ausgerüstet|Worse than equipped
Shop|Shop
Gratis|Free
Zufallskisten|Random chests
Allgemein|General
Skillbücher|Skill books
Rabenhändler|Raven merchant
Kaufen|Buy
Preis|Price
Tageslimit|Daily limit
Wöchentlich|Weekly
Täglich|Daily
Monatlich|Monthly
Gekauft|Purchased
Nicht genug Gold|Not enough gold
Nicht genug Diamanten|Not enough diamonds
Wirklich kaufen?|Really buy?
Kapitel|Chapter
Akt|Act
Map|Map
Boss-Map|Boss map
Schattentempel|Shadow temple
Ruinen|Ruins
Map-Info|Map info
Held zuweisen|Assign hero
Wiederholen|Repeat
Dichte|Density
Erstabschluss|First clear
Map abgeschlossen|Map completed
Held ist gestorben|Hero has died
Kisten voll|Chests full
Eigenschaften|Traits
Kommandeur|Commander
Kommandeur-Skills|Commander skills
Kommandoposten|Command post
Kopfgeld|Bounty
Kopfgeldquests|Bounty quests
Hauptquests|Main quests
Erfolge|Achievements
Rangliste|Leaderboard
Post|Mail
Mailbox|Mailbox
Exploration|Exploration
Wirtschaft|Economy
Glaube|Faith
Führung|Leadership
Punkte zurücksetzen|Reset points
Entdeckersicht|Explorer's sight
Verborgener Berater|Hidden advisor
Kommandeurs-Glück|Commander's luck
Augen der Fülle|Eyes of plenty
Adjutantenbefehl|Adjutant order
Belohnung|Reward
Abholen|Claim
Aktualisieren|Refresh
Priorität|Priority
Mindestgegner|Minimum enemies
Entfernung|Distance
Schwellenwert|Threshold
Bewegung|Movement
Ziele|Targets
Tränke (KI)|Potions (AI)
Grafik|Graphics
Ton|Sound
Sprache|Language
Deutsch|German
Englisch|English
Farbenblind-Modus|Colorblind mode
Tastenbelegung|Key bindings
Autosave|Autosave
Lautstärke|Volume
Vollbild|Fullscreen
UI-Skalierung|UI scale
Schriftgröße|Font size
Kein Held ausgewählt|No hero selected
Nicht genug Platz|Not enough space
Nicht genug Material|Not enough material
Nicht genug Mana|Not enough mana
Abklingzeit läuft|On cooldown
Kein Ziel|No target
Gespeichert|Saved
Spielstand geladen|Game loaded
Spielstand nicht gefunden|Save not found
Fehler beim Speichern|Error while saving
Daten konnten nicht geladen werden|Data could not be loaded
Willkommen bei Panoptix|Welcome to Panoptix
Alle Angebote sind erspielbar - es gibt keine Echtgeld-Käufe.|All offers can be earned in play - there are no real-money purchases.
Dieses Spiel ist freie Software (GPL-3.0-or-later).|This game is free software (GPL-3.0-or-later).
Schaden pro Sekunde|Damage per second
Kampfkraft|Combat power
Rüstungsminderung|Armor reduction
Treffer|Hits
Heilung|Healing
Gift|Poison
Sofort|Instant
Dauer|Duration
Radius|Radius
Winkel|Angle
Verlangsamung|Slow
Betäubung|Stun
Spott|Taunt
Totem-Dauer|Totem duration
Skill-Buch|Skill book
Skill-Level|Skill level
Skill-Slots|Skill slots
Aktive Skills|Active skills
Ausrüstung|Equipment
Statistiken|Stats
Basislager|Base camp
Quest-Brett|Quest board
Tempel-Tickets|Temple tickets
Stufe|Stage
Rabenmünzen|Raven coins
Nächster Held|Next hero
Layout|Layout
Pause|Pause
Spielgeschwindigkeit|Game speed
""".strip().splitlines()

ASCII_MAP = str.maketrans({"ä": "ae", "ö": "oe", "ü": "ue", "ß": "ss", "Ä": "Ae", "Ö": "Oe", "Ü": "Ue"})


def source_tr_literals():
    """Alle tr("...")-Literale im Quelltext (ohne Escapes)."""
    found = set()
    for dp, _, fns in os.walk(os.path.join(ROOT, "src")):
        for fn in fns:
            if fn.endswith((".c", ".h")):
                text = open(os.path.join(dp, fn), encoding="utf-8").read()
                for m in re.finditer(r'\btr\("((?:[^"\\]|\\.)*)"\)', text):
                    if "\\" not in m.group(1) and m.group(1) != "Deutscher Text":
                        found.add(m.group(1))
    return found


def gen_lang():
    pairs = {}
    for line in LANG_PAIRS:
        de, en = line.split("|", 1)
        pairs.setdefault(de, en)
        ascii_de = de.translate(ASCII_MAP)
        pairs.setdefault(ascii_de, en)
    # Daten-Namen: Tiers, Klassen, Stat-Labels stehen oben; fehlende Quelltext-Literale melden
    missing = [s for s in sorted(source_tr_literals()) if s not in pairs]
    for s in missing:
        print("  Hinweis: keine englische Uebersetzung fuer tr(\"%s\")" % s)
    rows = [dict(de=k, en=v) for k, v in pairs.items()]
    write_tsv("lang/en.tsv", ["de", "en"], rows, "Spalten: deutscher Quelltext -> englische Uebersetzung.")


# ---------------------------------------------------------------------------
# data/maps/1-1.map: handgebaute Start-Karte (60x40, ASCII-Raster)
# ---------------------------------------------------------------------------
def gen_map_1_1():
    w, h = 60, 40
    g = [["#"] * w for _ in range(h)]

    def room(x0, y0, x1, y1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                g[y][x] = "."

    def corridor(a, b):
        (x0, y0), (x1, y1) = a, b
        x, y = x0, y0
        while x != x1:
            g[y][x] = "."
            g[y + 1][x] = "."
            x += 1 if x1 > x else -1
        while y != y1:
            g[y][x] = "."
            g[y][x + 1] = "."
            y += 1 if y1 > y else -1
        g[y1][x1] = "."

    # Hauptpfad: Start links unten -> Ausgang rechts oben, 9 Wegpunkte
    rooms = [(2, 30, 10, 37), (14, 28, 22, 36), (14, 16, 24, 24), (6, 5, 16, 12), (22, 4, 32, 11),
             (28, 16, 38, 24), (36, 28, 46, 36), (42, 14, 52, 22), (46, 3, 57, 10)]
    for r in rooms:
        room(*r)
    centers = [((r[0] + r[2]) // 2, (r[1] + r[3]) // 2) for r in rooms]
    for a, b in zip(centers, centers[1:]):
        corridor(a, b)
    # Nebenpfad a (Sackgasse mit Heilquelle) und b (Nebenraum mit Dämpfungsboden und Wasser)
    room(2, 14, 9, 21)
    corridor((9, 18), centers[2])
    room(50, 28, 57, 36)
    corridor((46, 32), (50, 32))
    for x in range(52, 56):
        for y in range(30, 34):
            g[y][x] = ":"
    for x in range(26, 31):
        g[20][x] = "~"
    g[17][5] = "h"
    # Wegpunkte, Start, Ausgang, Monsterpacks
    for i, c in enumerate(centers):
        g[c[1]][c[0]] = str(i + 1)
    g[centers[0][1]][centers[0][0] - 2] = "S"
    g[centers[-1][1]][centers[-1][0] + 3] = "X"
    g[18][6] = "a"
    g[32][54] = "b"
    for (x, y) in ((18, 32), (18, 20), (11, 8), (27, 7), (33, 20), (41, 32), (47, 18), (53, 31), (6, 19)):
        if g[y][x] == ".":
            g[y][x] = "M"
    os.makedirs(os.path.join(OUT, "maps"), exist_ok=True)
    with open(os.path.join(OUT, "maps", "1-1.map"), "w", encoding="utf-8", newline="\n") as f:
        for row in g:
            f.write("".join(row) + "\n")
    print("%-18s %4d Zeilen" % ("maps/1-1.map", h))


# ---------------------------------------------------------------------------
# Querpruefung und main
# ---------------------------------------------------------------------------
def validate():
    ids = {i["id"] for i in ITEMS}
    mons = {m["id"] for m in MONSTERS}
    for m in MAPS:
        assert not m["boss"] or m["boss"] in mons, m
        assert not m["stone"] or m["stone"] in ids, m
    for o in SHOP:
        if o["kind"] in ("item", "wings"):
            assert o["item"] in ids, o
    for k in ("weapon", "sub", "armor", "ring", "necklace", "potion", "wings", "bag", "material", "stone"):
        print("  %-9s %3d" % (k, sum(1 for i in ITEMS if i["kind"] == k)))
    assert sum(1 for i in ITEMS if i["kind"] == "weapon") == 147
    assert sum(1 for i in ITEMS if i["kind"] == "sub") == 42
    assert sum(1 for i in ITEMS if i["kind"] == "armor") == 84


def main():
    os.chdir(ROOT)
    gen_tiers()
    gen_classes()
    gen_skills()
    gen_items()
    gen_monsters()
    gen_maps()
    gen_shop()
    gen_traits()
    gen_quests()
    gen_balance()
    gen_lang()
    gen_map_1_1()
    validate()


if __name__ == "__main__":
    main()
