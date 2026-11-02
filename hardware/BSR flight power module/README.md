# BSR Flight Power Module

Instrument de vol de KHEPRI : un module de 80 mm de diamètre qui réunit la
commande moteur, le bus CAN et l'interface pilote dans un seul boîtier à
dissipateur.

## Architecture

- **Façade** — smart knob et écran rond Waveshare ESP32-S3-Touch-AMOLED 1,75".
  Le code des écrans pilote vit dans [`../../IHM/`](../../IHM/) à la racine de
  ce dépôt, et se met à jour en OTA depuis là.
- **Contrôleur moteur BSR Drive v3.7** — dans son propre dépôt,
  [bluesmart-robotics/Bsr-drive](https://github.com/bluesmart-robotics/Bsr-drive) :
  firmware (fork ODrive), GUI et conception de la carte
  (`hardware/bsr-drive-v3.7/`). Le sous-module `bsr-drive/` est
  **temporairement retiré** de ce dépôt pour que BoardRepo n'y voie qu'une
  seule carte (voir plus bas).
- **`carte_controle/`** — carte qui pilote le bus CAN entre la commande moteur
  et la façade : ESP32-WROOM-32UE, transceiver CAN SN65HVD230, centrale
  inertielle ICM-42688-P, multiplexeur I²C TCA9548A, deux abaisseurs MP2565 et
  TPS54560. Projet KiCad 9 avec sa nomenclature, ses gerbers et sa
  bibliothèque.
- **Alimentation de secours** — une ou deux cellules 18650 LiFePO4, pour
  continuer à afficher les erreurs et à journaliser si la batterie du planeur
  défaille.
- **Ventilation pilotée** et surveillance des températures.

## Rattachement des cartes

`carte_controle/` est une **copie**, prise du dépôt E-dolly (`hardware/pcb_controle/`)
au commit `20ab2e3`. E-dolly étant privé et ce dépôt public, un sous-module
n'était pas possible. Depuis novembre 2026, les deux copies ont **volontairement
divergé** : la version KHEPRI a perdu l'E-stop, les contacteurs et les entrées
de puissance, et gagné le bloc capteurs de pression (détails dans
[`carte_controle/README.md`](carte_controle/README.md)). Ne pas reporter ces
changements dans E-dolly, qui a besoin de son E-stop.

La BSR Drive était rattachée ici en sous-module (`bsr-drive/`, commit
`b21a2b1` du dépôt Bsr-drive). Il a été retiré temporairement le 02/11/2026
pour tester l'import de ce dépôt sur BoardRepo avec une seule carte. Pour le
remettre :

```
git submodule add https://github.com/bluesmart-robotics/bsr-drive "hardware/BSR flight power module/bsr-drive"
git -C "hardware/BSR flight power module/bsr-drive" checkout b21a2b1
```
