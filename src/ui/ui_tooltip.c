/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Beschreibungstexte fuer Items/Skills und die Tooltips dazu. */
#include <string.h>

#include "core/util.h"
#include "rules/rules.h"
#include "ui/ui.h"
#include "ui/ui_internal.h"

/* Ist der Stat ein flacher Wert (kein Prozent)? */
static bool stat_is_flat(int stat) {
    switch (stat) {
    case STAT_ATTACK: case STAT_ATTACK_PHYS: case STAT_SPELL_FLAT: case STAT_ARMOR_FLAT:
    case STAT_HP_FLAT: case STAT_MANA_FLAT: case STAT_HP_REGEN: case STAT_MANA_REGEN:
    case STAT_CHEST_CAP: case STAT_DR_ATTACK: case STAT_DR_SPELL: case STAT_PROJECTILES:
    case STAT_PIERCE:
        return true;
    default:
        return false;
    }
}

void stat_line(int stat, float value, char *buf, size_t size) {
    if (stat < 0 || stat >= STAT_COUNT) {
        str_copy(buf, size, "");
        return;
    }
    const char *label = tr(g_stat_labels[stat]);
    if (stat_is_flat(stat)) {
        if (value == (float)(int)value) {
            str_format(buf, size, "%s %+d", label, (int)value);
        } else {
            str_format(buf, size, "%s %+.1f", label, value);
        }
    } else {
        str_format(buf, size, "%s %+.1f %%", label, value);
    }
}

/* Haengt alle von 0 verschiedenen Stats an. */
static void append_mods(const StatMods *m, char *buf, size_t size) {
    char line[96];
    for (int s = 0; s < STAT_COUNT; s++) {
        if (m->v[s] != 0.0f) {
            stat_line(s, m->v[s], line, sizeof line);
            str_append(buf, size, line);
            str_append(buf, size, "\n");
        }
    }
}

static const char *class_label(int cls) {
    if (cls >= 0 && cls < CLASS_COUNT) {
        return g_classes[cls].name;
    }
    return tr("Alle Klassen");
}

void itemdef_describe(const ItemDef *d, char *buf, size_t size) {
    char line[160];
    buf[0] = 0;
    str_format(line, size < sizeof line ? size : sizeof line, "%s - %s\n", tr(g_item_kind_names[d->kind]),
               g_tiers[d->tier].name);
    str_append(buf, size, line);
    if (d->kind == IK_WEAPON || d->kind == IK_SUB) {
        str_format(line, sizeof line, "%s: %s\n", tr("Klasse"), class_label(d->cls));
        str_append(buf, size, line);
    }
    if (d->min_level > 0) {
        str_format(line, sizeof line, "%s %d\n", tr("Mindestlevel"), d->min_level);
        str_append(buf, size, line);
    }
    switch (d->kind) {
    case IK_WEAPON: str_format(line, sizeof line, "%s: %.0f\n", tr("Schaden"), d->value); break;
    case IK_ARMOR: str_format(line, sizeof line, "%s: %.0f\n", tr("Ruestung"), d->value); break;
    case IK_SUB: str_format(line, sizeof line, "%s: %.0f\n", tr("Hauptwert"), d->value); break;
    case IK_POTION:
        str_format(line, sizeof line, "%s: %s, %d %s\n", tr("Wirkung"), tr(g_potion_type_names[d->potion_type]),
                   d->charges, tr("Ladungen"));
        break;
    default: line[0] = 0; break;
    }
    str_append(buf, size, line);
    append_mods(&d->mods, buf, size);
    if (d->set_id > 0 && d->set_id < 8 && g_sets[d->set_id].name[0]) {
        str_format(line, sizeof line, "%s: %s\n", tr("Set"), g_sets[d->set_id].name);
        str_append(buf, size, line);
    }
    if (d->tier == TIER_LEGENDARY) {
        for (int c = 0; c < CLASS_COUNT; c++) {
            int e = d->legendary_effect[c];
            if (e > 0 && e < 32 && g_legend[e].name[0]) {
                str_format(line, sizeof line, "%s: %s\n", g_classes[c].name, g_legend[e].name);
                str_append(buf, size, line);
            }
        }
    }
}

void item_describe(const Game *game, const Item *it, char *buf, size_t size) {
    (void)game;
    buf[0] = 0;
    if (item_is_empty(it) || it->def >= g_item_count) {
        return;
    }
    const ItemDef *d = &g_items[it->def];
    char line[200];
    str_format(line, sizeof line, "%s - %s\n", tr(g_item_kind_names[d->kind]), g_tiers[d->tier].name);
    str_append(buf, size, line);
    if (d->kind == IK_WEAPON || d->kind == IK_SUB) {
        str_format(line, sizeof line, "%s: %s\n", tr("Klasse"), class_label(d->cls));
        str_append(buf, size, line);
    }
    if (d->min_level > 0) {
        str_format(line, sizeof line, "%s %d\n", tr("Mindestlevel"), d->min_level);
        str_append(buf, size, line);
    }
    float main = item_main_value(it);
    if (d->kind == IK_WEAPON) {
        str_format(line, sizeof line, "%s: %.0f\n", tr("Schaden"), main);
        str_append(buf, size, line);
    } else if (d->kind == IK_ARMOR) {
        str_format(line, sizeof line, "%s: %.0f\n", tr("Ruestung"), main);
        str_append(buf, size, line);
    } else if (d->kind == IK_SUB) {
        str_format(line, sizeof line, "%s: %.0f\n", tr("Hauptwert"), main);
        str_append(buf, size, line);
    } else if (d->kind == IK_POTION) {
        str_format(line, sizeof line, "%s: %s\n%s: %d / 100\n", tr("Wirkung"), tr(g_potion_type_names[d->potion_type]),
                   tr("Fuellung"), (int)it->fill);
        str_append(buf, size, line);
    }
    append_mods(&d->mods, buf, size);
    if (it->plus > 0) {
        str_format(line, sizeof line, "%s +%d\n", tr("Verbessert"), (int)it->plus);
        str_append(buf, size, line);
    }
    if (it->ench_opt != ENCH_NONE) {
        char e[120];
        enchant_describe(it, e, sizeof e);
        str_format(line, sizeof line, "%s: %s%s\n", tr("Verzauberung"), e, (it->flags & ITEMF_SEALED) ? " [Sealed]" : "");
        str_append(buf, size, line);
    }
    if (d->set_id > 0 && d->set_id < 8 && g_sets[d->set_id].name[0]) {
        str_format(line, sizeof line, "%s: %s\n", tr("Set"), g_sets[d->set_id].name);
        str_append(buf, size, line);
    }
    if (d->tier == TIER_LEGENDARY) {
        for (int c = 0; c < CLASS_COUNT; c++) {
            int e = d->legendary_effect[c];
            if (e > 0 && e < 32 && g_legend[e].name[0]) {
                str_format(line, sizeof line, "%s: %s\n", g_classes[c].name, g_legend[e].name);
                str_append(buf, size, line);
            }
        }
    }
    if (it->flags & ITEMF_LOCKED) {
        str_append(buf, size, tr("[gesperrt]\n"));
    }
    str_format(line, sizeof line, "%s: %lld", tr("Verkaufswert"), (long long)item_sell_value(it));
    str_append(buf, size, line);
}

void ui_tooltip_item(Ui *ui, const Game *game, const Hero *compare_to, const Item *it) {
    if (item_is_empty(it) || it->def >= g_item_count) {
        return;
    }
    const ItemDef *d = &g_items[it->def];
    char title[96];
    char body[900];
    item_display_name(it, title, sizeof title);
    item_describe(game, it, body, sizeof body);
    /* Vergleich mit dem Angelegten */
    if (compare_to && d->slot != SLOT_NONE && (d->kind == IK_WEAPON || d->kind == IK_ARMOR || d->kind == IK_SUB)) {
        const Item *cur = &compare_to->eq[d->slot];
        char line[96];
        if (item_is_empty(cur)) {
            str_format(line, sizeof line, "\n\x01^ %s", tr("Slot ist frei"));
        } else if (cur != it) {
            float diff = item_main_value(it) - item_main_value(cur);
            if (diff >= 0) {
                str_format(line, sizeof line, "\n\x01^ %s %+.0f", tr("besser als angelegt:"), diff);
            } else {
                str_format(line, sizeof line, "\n\x02v %s %.0f", tr("schlechter als angelegt:"), diff);
            }
        } else {
            line[0] = 0;
        }
        str_append(body, sizeof body, line);
    }
    ui_tooltip(ui, title, g_tiers[d->tier].color, body);
}

void skill_describe(const Game *game, int skill, char *buf, size_t size) {
    buf[0] = 0;
    if (skill < 0 || skill >= g_skill_count) {
        return;
    }
    const SkillDef *s = &g_skills[skill];
    int lv = game ? game->player.skill_level[skill] : 0;
    char line[200];
    str_format(line, sizeof line, "%s: %s | %s %d\n", tr("Klasse"),
               s->cls < CLASS_COUNT ? g_classes[s->cls].name : tr("Allgemein"), tr("Grad"), s->grade);
    str_append(buf, size, line);
    str_format(line, sizeof line, "%s: %.1f s | %s: %.0f\n", tr("Abklingzeit"), s->cooldown, tr("Mana"), s->mana);
    str_append(buf, size, line);
    if (s->range > 0) {
        str_format(line, sizeof line, "%s: %.0f\n", tr("Reichweite"), s->range);
        str_append(buf, size, line);
    }
    if (s->dmg_pct > 0) {
        str_format(line, sizeof line, "%s: %.0f %%\n", tr("Schaden"), skill_dmg_pct(s, lv > 0 ? lv : 1));
        str_append(buf, size, line);
    }
    if (s->stat >= 0 && s->stat < STAT_COUNT) {
        char st[96];
        stat_line(s->stat, skill_stat_value(s, lv > 0 ? lv : 1, false), st, sizeof st);
        str_append(buf, size, st);
        str_append(buf, size, "\n");
    }
    if (lv > 0) {
        str_format(line, sizeof line, "%s %d\n", tr("Level"), lv);
        str_append(buf, size, line);
    }
    str_append(buf, size, s->desc);
}

void ui_tooltip_skill(Ui *ui, const Game *game, int skill) {
    if (skill < 0 || skill >= g_skill_count) {
        return;
    }
    char body[700];
    skill_describe(game, skill, body, sizeof body);
    int grade = clampi(g_skills[skill].grade, 0, TIER_COUNT - 1);
    ui_tooltip(ui, g_skills[skill].name, g_tiers[grade].color, body);
}
