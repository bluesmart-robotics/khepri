#!/usr/bin/env python3
"""
Publie une nouvelle version des écrans KHÉPRI pour la mise à jour OTA depuis GitHub.

  python tools/publier_maj.py rond        écran rond 1.75 (régime moteur)
  python tools/publier_maj.py energie     écran rectangulaire 1.64 (énergie), cartes V1 et V1.1
  python tools/publier_maj.py passerelle  passerelle de simulation (flash USB seulement)
  python tools/publier_maj.py tout
  ajouter --compiler pour lancer idf.py avant (environnement ESP-IDF chargé)

Avant : augmenter PROJECT_VER dans ecran_xxx/CMakeLists.txt, sinon les écrans ne voient rien.
Après : git add release && git commit && git push. Les écrans connectés au Wi-Fi lisent leur
manifeste toutes les minutes et se mettent à jour dès que la version diffère de la leur.

Arborescence produite :
  release/version.json                   ancien manifeste (écrans ronds ≤ 1.4.1) → écran rond
  release/ecran_rond/version.json        manifeste de l'écran rond, + image OTA et image USB 0x0
  release/ecran_energie/version.json     un seul manifeste, un champ par carte : url_V1, url_V1.1
  release/passerelle/                    image USB 0x0 de la passerelle de simulation
  release/SHA256SUMS
"""
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
REL = ROOT / "release"
RAW = "https://raw.githubusercontent.com/bluesmart-robotics/khepri/main/IHM/code/release/"
ENERGIE_CARTES = ["V1", "V1.1"]


def project_ver(proj: pathlib.Path) -> str:
    return re.search(r'set\(PROJECT_VER "([^"]+)"\)', (proj / "CMakeLists.txt").read_text()).group(1)


def built_ver(app: pathlib.Path) -> str:
    return app.read_bytes()[48:80].split(b"\0")[0].decode(errors="replace")  # esp_app_desc_t.version


def idf(proj: pathlib.Path, build: str = "build", defaults: str = "") -> None:
    cmd = ["idf.py", "-B", build]
    if defaults:
        cmd += ["-D", f"SDKCONFIG={build}/sdkconfig", "-D", f"SDKCONFIG_DEFAULTS={defaults}"]
    subprocess.run(cmd + ["build"], cwd=proj, check=True)


def merge_0x0(build: pathlib.Path, out: pathlib.Path) -> None:
    """Image complète pour un premier flash par USB à l'adresse 0x0."""
    subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge-bin", "-o", str(out), "@flash_args"],
                   cwd=build, check=True)


def check(app: pathlib.Path, ver: str) -> bool:
    if not app.exists():
        print(f"{app.relative_to(ROOT)} absent : compiler d'abord (ou --compiler)")
        return False
    if built_ver(app) != ver:
        print(f"{app.relative_to(ROOT)} est en version {built_ver(app)}, CMakeLists.txt annonce {ver} : recompiler")
        return False
    return True


def entry(path: pathlib.Path) -> dict:
    data = path.read_bytes()
    return {"url": RAW + path.relative_to(REL).as_posix(), "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def rond(compile_first: bool) -> bool:
    proj = ROOT / "ecran_rond_1.75"
    ver = project_ver(proj)
    if compile_first:
        idf(proj)
    app = proj / "build" / "khepri_display_175.bin"
    if not check(app, ver):
        return False
    out = REL / "ecran_rond"
    out.mkdir(parents=True, exist_ok=True)
    shutil.copy(app, out / "khepri_ecran_rond_app.bin")
    merge_0x0(proj / "build", out / "khepri_ecran_rond_1.75_0x0.bin")
    e = entry(out / "khepri_ecran_rond_app.bin")
    manifest = {"version": ver, **e}
    text = json.dumps(manifest, indent=2) + "\n"
    (out / "version.json").write_text(text)
    (REL / "version.json").write_text(text)  # les écrans ronds ≤ 1.4.1 lisent encore cette adresse
    print("écran rond :", text)
    return True


def energie(compile_first: bool) -> bool:
    proj = ROOT / "ecran_energie_1.64"
    ver = project_ver(proj)
    out = REL / "ecran_energie"
    out.mkdir(parents=True, exist_ok=True)
    manifest = {"version": ver}
    for carte in ENERGIE_CARTES:
        build = f"build_{carte}"
        if compile_first:
            idf(proj, build, f"sdkconfig.defaults;sdkconfig.{carte}")
        app = proj / build / "khepri_energie_164.bin"
        if not check(app, ver):
            return False
        dst = out / f"khepri_ecran_energie_{carte}_app.bin"
        shutil.copy(app, dst)
        merge_0x0(proj / build, out / f"khepri_ecran_energie_1.64_{carte}_0x0.bin")
        e = entry(dst)
        manifest[f"url_{carte}"] = e["url"]
        manifest[f"size_{carte}"] = e["size"]
        manifest[f"sha256_{carte}"] = e["sha256"]
    text = json.dumps(manifest, indent=2) + "\n"
    (out / "version.json").write_text(text)
    print("écran énergie :", text)
    return True


def passerelle(compile_first: bool) -> bool:
    proj = ROOT / "gateway_sim"
    if compile_first:
        idf(proj)
    if not (proj / "build" / "flash_args").exists():
        print("gateway_sim/build absent : compiler d'abord (ou --compiler)")
        return False
    out = REL / "passerelle"
    out.mkdir(parents=True, exist_ok=True)
    merge_0x0(proj / "build", out / "khepri_passerelle_simulation_0x0.bin")
    return True


def sums() -> None:
    lines = []
    for f in sorted(REL.rglob("*.bin")):
        lines.append(f"{hashlib.sha256(f.read_bytes()).hexdigest()}  {f.relative_to(REL).as_posix()}")
    (REL / "SHA256SUMS").write_text("\n".join(lines) + "\n")


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    compile_first = "--compiler" in sys.argv
    what = args[0] if args else "tout"
    todo = {"rond": [rond], "energie": [energie], "passerelle": [passerelle], "tout": [rond, energie, passerelle]}.get(what)
    if not todo:
        print(__doc__)
        return 1
    ok = all(f(compile_first) for f in todo)
    sums()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
