#!/usr/bin/env python3
"""Generates src/clothing_items.inc from third_party/clothing-expanded (ClothingExpanded by TonisGaming):
the 28 items' stats (items/*.json, ModComponent's auto-mapped format), what each is made of, and their names
(English from Localization.json, Polish ours). Run from the repo root: python3 tools/clothing/gen_items.py"""
import json, os, sys

ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
SRC = os.path.join(ROOT, 'third_party', 'clothing-expanded')
OUT = os.path.join(ROOT, 'src', 'clothing_items.inc')

# item -> (TLD's base item, the accessory, kind)  (ClothingExpanded's TBUtils / TBFunctionalities)
BELT, CRAMPONS, IMPROVISED, SCABBARD = 'GEAR_ToolBelt', 'GEAR_Crampons', 'GEAR_ImprovisedCrampons', 'GEAR_RifleScabbardA'
MADE = {
    'JeansToolbelt': ('GEAR_Jeans', BELT), 'CargoToolbelt': ('GEAR_CargoPants', BELT),
    'MinerToolbelt': ('GEAR_MinersPants', BELT), 'CombatToolbelt': ('GEAR_CombatPants', BELT),
    'DeerskinToolbelt': ('GEAR_DeerSkinPants', BELT), 'InsulatedToolbelt': ('GEAR_InsulatedPants', BELT),
    'WorkToolbelt': ('GEAR_WorkPants', BELT),
    'MooseBagPlusScabbard': ('GEAR_MooseHideBag', SCABBARD),
}
BOOTS = {'Work': 'GEAR_WorkBoots', 'Ski': 'GEAR_SkiBoots', 'Combat': 'GEAR_CombatBoots', 'Deerskin': 'GEAR_DeerSkinBoots',
         'Dressing': 'GEAR_LeatherShoes', 'Insulated': 'GEAR_InsulatedBoots', 'Mukluk': 'GEAR_MuklukBoots',
         'Trail': 'GEAR_BasicBoots', 'Running': 'GEAR_BasicShoes', 'Chemical': 'GEAR_MinersBoots'}
for b, base in BOOTS.items():
    MADE[b + 'NCrampons'] = (base, CRAMPONS)
    MADE[b + 'ICrampons'] = (base, IMPROVISED)

PL_BASE = {'GEAR_Jeans': 'Dżinsy', 'GEAR_CargoPants': 'Bojówki', 'GEAR_MinersPants': 'Spodnie górnicze',
           'GEAR_CombatPants': 'Spodnie wojskowe', 'GEAR_DeerSkinPants': 'Spodnie z jeleniej skóry',
           'GEAR_InsulatedPants': 'Spodnie narciarskie', 'GEAR_WorkPants': 'Spodnie robocze',
           'GEAR_MooseHideBag': 'Torba z łosiej skóry',
           'GEAR_WorkBoots': 'Buty robocze', 'GEAR_SkiBoots': 'Buty narciarskie', 'GEAR_CombatBoots': 'Buty wojskowe',
           'GEAR_DeerSkinBoots': 'Buty z jeleniej skóry', 'GEAR_LeatherShoes': 'Skórzane półbuty',
           'GEAR_InsulatedBoots': 'Buty ocieplane', 'GEAR_MuklukBoots': 'Mukluki', 'GEAR_BasicBoots': 'Buty trekkingowe',
           'GEAR_BasicShoes': 'Buty do biegania', 'GEAR_MinersBoots': 'Buty chemoodporne'}
PL_WITH = {BELT: 'z pasem na narzędzia', CRAMPONS: 'z rakami', IMPROVISED: 'z prowizorycznymi rakami',
           SCABBARD: 'z pochwą na karabin'}
PL_DESC = {BELT: 'Pas na narzędzia przypięty do spodni: trzy najcięższe narzędzia w plecaku ważą o połowę mniej.',
           CRAMPONS: 'Raki przypięte do butów: lepsza przyczepność przy wspinaczce i na lodzie.',
           IMPROVISED: 'Prowizoryczne raki przypięte do butów: trochę lepsza przyczepność przy wspinaczce i na lodzie.',
           SCABBARD: 'Pochwa na karabin przypięta do torby: karabin w plecaku waży o połowę mniej, a torba dalej '
                     'zwiększa udźwig.'}

def F(x):
    return repr(float(x))

def c(s):
    return json.dumps(s, ensure_ascii=False)

def main():
    loc = json.load(open(os.path.join(SRC, 'Localization.json'), encoding='utf-8'))
    def en(key):
        for k, v in loc.items():
            if k.lower() == key.lower():
                return v.get('English') or ''
        return ''
    rows, texts = [], []
    for name in sorted(MADE):
        base, extra = MADE[name]
        d = json.load(open(os.path.join(SRC, 'items', name + '.json'), encoding='utf-8'))
        m = d['ModClothingComponent']
        r, h = d.get('ModRepairableBehaviour'), d.get('ModHarvestableBehaviour')
        def names(xs): return '|'.join(xs or [])
        def counts(xs): return '|'.join(str(x) for x in (xs or []))
        v3 = lambda a: '{%sf, %sf, %sf}' % tuple(repr(float(x)) for x in a)
        rows.append(
            '    {%s, %s, %s, %s, %s, %sf, %sf, %sf, %s, %s, %s, %s, %sf, %sf, %sf, %sf, %sf, %sf, %sf, %sf, %sf, %sf, '
            '%sf, %s, %s, %s, %s, %sf, %s, %s, %s, %d, %d, %s, %s, %s, %d, %s, %s, %s},' % (
                c('GEAR_' + name), c(base), c(extra), c(m['DisplayNameLocalizationId']), c(m['DescriptionLocalizatonId']),
                F(m['WeightKG']), F(m['DaysToDecay']), F(m['MaxHP']), c(m['InitialCondition']), c(m['Region']),
                c(m['MinLayer']), c(m['MaxLayer']), F(m['DaysToDecayWornOutside']), F(m['DaysToDecayWornInside']),
                F(m['Warmth']), F(m['WarmthWhenWet']), F(m['Windproof']), F(m['Waterproofness']), F(m['Toughness']),
                F(m['SprintBarReduction']), F(m['HoursToDryNearFire']), F(m['HoursToDryWithoutFire']),
                F(m['HoursToFreeze']), c(m['MainTexture']), c(m['BlendTexture']), c(m['NormalModel']), c(m['InspectModel']),
                F(m['InspectDistance']), v3(m['InspectAngles']), v3(m['InspectOffset']),
                v3(m['InspectScale']),
                1 if r else 0, (r or {}).get('Minutes', 0), c((r or {}).get('Audio', '')),
                c(names((r or {}).get('MaterialNames'))), c(counts((r or {}).get('MaterialCounts'))),
                (r or {}).get('Condition', 0), c(names((r or {}).get('RequiredTools'))),
                c((h or {}).get('Audio', '')) if h else '""',
                c('%d;%s;%s' % (h.get('Minutes', 0), names(h.get('YieldNames')), counts(h.get('YieldCounts')))) if h else '""'))
        texts.append((m['DisplayNameLocalizationId'], en(m['DisplayNameLocalizationId']),
                      '%s %s' % (PL_BASE[base], PL_WITH[extra])))
        texts.append((m['DescriptionLocalizatonId'], en(m['DescriptionLocalizatonId']), PL_DESC[extra]))
    ui = [('TLDCOOP_CE_AttachBelt', 'Attach Belt', 'Przypnij pas'),
          ('TLDCOOP_CE_DetachBelt', 'Detach Belt', 'Odepnij pas'),
          ('TLDCOOP_CE_AttachCrampons', 'Attach Crampons', 'Przypnij raki'),
          ('TLDCOOP_CE_DetachCrampons', 'Detach Crampons', 'Odepnij raki'),
          ('TLDCOOP_CE_AttachScabbard', 'Attach Scabbard', 'Przypnij pochwę'),
          ('TLDCOOP_CE_DetachScabbard', 'Detach Scabbard', 'Odepnij pochwę'),
          ('TLDCOOP_CE_NoBelt', 'You are missing a toolbelt', 'Nie masz pasa na narzędzia'),
          ('TLDCOOP_CE_NoCrampons', 'You are missing crampons', 'Nie masz raków'),
          ('TLDCOOP_CE_NoScabbard', 'You are missing a rifle scabbard', 'Nie masz pochwy na karabin'),
          ('TLDCOOP_CE_Attaching', 'Attaching...', 'Przypinanie...'),
          ('TLDCOOP_CE_Detaching', 'Detaching...', 'Odpinanie...')]
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('// Generated by tools/clothing/gen_items.py from third_party/clothing-expanded (ClothingExpanded by '
                'TonisGaming). Do not edit.\n')
        f.write('static const Item kItems[] = {\n' + '\n'.join(rows) + '\n};\n')
        f.write('static const Text kTexts[] = {\n')
        for k, e, p in texts + ui:
            f.write('    {%s, %s, %s},\n' % (c(k), c(e), c(p)))
        f.write('};\n')
    print('wrote', OUT, len(rows), 'items', len(texts) + len(ui), 'texts')

if __name__ == '__main__':
    sys.exit(main())
