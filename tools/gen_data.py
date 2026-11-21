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
    "Waechter": "Wächter", "Zweihaender": "Zweihänder", "Aeltesten": "Ältesten", "Koenig": "König",
    "Loewenherz": "Löwenherz", "Koecher": "Köcher", "Fuehrer": "Führer", "Gnadenzepter": "Gnadenzepter",
    "Staerke": "Stärke", "Ruestung": "Rüstung", "Schuetze": "Schütze", "Saebel": "Säbel",
    "Goettlich": "Göttlich", "Haeuptling": "Häuptling", "Rueckschuss": "Rückschuss", "Waechters": "Wächters",
    "Plünderers": "Plünderers", "Sekundaer": "Sekundär", "Gluehend": "Glühend", "Gluh": "Glüh",
    "Schaedel": "Schädel", "Maechtig": "Mächtig", "Daemon": "Dämon", "Raeuber": "Räuber",
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
            for c in cells:
                assert "\t" not in c and "\n" not in c, (name, c)
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
            hp_mult=[1, 3, 8, 20, 60, 150][i], dmg_mult=[1, 1.3, 1.7, 2.2, 3, 4][i],
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
