#!/usr/bin/env python3
"""
Publie une nouvelle version de l'écran pilote pour la mise à jour OTA.

  1. augmenter PROJECT_VER dans IHM/code/CMakeLists.txt (par ex. 1.1.0 → 1.1.1)
  2. idf.py build
  3. python tools/publier_maj.py
  4. git add release && git commit -m "écran v1.1.1" && git push

Les écrans connectés au Wi-Fi lisent release/version.json toutes les minutes ; dès que la version
diffère de la leur, ils téléchargent release/khepri_ecran_app.bin et redémarrent dessus.
"""
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
REL = ROOT / "release"
RAW = "https://raw.githubusercontent.com/bluesmart-robotics/khepri/main/IHM/code/release/"


def main() -> int:
    ver = re.search(r'set\(PROJECT_VER "([^"]+)"\)', (ROOT / "CMakeLists.txt").read_text()).group(1)
    app = BUILD / "khepri_display_175.bin"
    if not app.exists():
        print("build/khepri_display_175.bin absent : lancer d'abord idf.py build")
        return 1
    built = app.read_bytes()[48:80].split(b"\0")[0].decode(errors="replace")  # esp_app_desc_t.version
    if built != ver:
        print(f"build/ contient la version {built}, CMakeLists.txt annonce {ver} : relancer idf.py build")
        return 1
    REL.mkdir(exist_ok=True)
    shutil.copy(app, REL / "khepri_ecran_app.bin")
    # image complète pour un premier flash par USB à l'adresse 0x0
    subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge-bin", "-o",
                    str(REL / "khepri_ecran_1.75_0x0.bin"), "@flash_args"], cwd=BUILD, check=True)
    data = (REL / "khepri_ecran_app.bin").read_bytes()
    manifest = {
        "version": ver,
        "url": RAW + "khepri_ecran_app.bin",
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }
    (REL / "version.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
