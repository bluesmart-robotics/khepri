# Modèles 3D de la pile BSR Flight Power Module

| Fichier | Carte | Source |
|---|---|---|
| `bsr_drive.step` | BSR Drive v3.7 (en bas) | `bsr-drive/hardware/bsr-drive-v3.7/PCB.step` (export Altium de juin 2026, peut ne pas refléter le dernier placement) |
| `carte_controle.step` | carte de contrôle (milieu) | export KiCad 9 de `BSR flight power module/carte_controle/e_dolly.kicad_pcb` |
| `carte_pmp.step` | carte de charge PMP (en haut) | export KiCad 9 de `BSR flight power module/carte_pmp/pmp_khepri.kicad_pcb` (circuit nu, voir son README) |

## Calage

- `carte_controle.step` et `carte_pmp.step` sont dans **le même repère** (repère
  KiCad, carte de x 104 à 184 et y 28,85 à 178,85 mm) : ils se superposent
  directement, seul l'empilement en Z est à régler.
- `bsr_drive.step` est dans le repère Altium de la BSR Drive. Pour la caler,
  aligner ses trous M3 et sa XT60 J9 sur ces positions de la pile (repère KiCad
  des deux autres cartes, Y vers le bas) :
  - XT60 : centre (138,125 ; 156,075), broche P côté x 134,5 ;
  - trous M3 (Ø 3,4) : (124,53 ; 66,30), (148,13 ; 66,30), (124,53 ; 104,80),
    (148,13 ; 104,80), (124,53 ; 147,30), (148,13 ; 147,30), (166,53 ; 157,30),
    plus quatre trous au-dessus du bord haut des cartes (y 21,8 et 26,3).
  - correspondance depuis la BSR Drive importée dans KiCad :
    x_pile = 249,03 − y_BSR ; y_pile = 238,05 − x_BSR.
