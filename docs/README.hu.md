# mnkord dokumentáció

[English](README.md) · **Magyar**

A **mnKord** egy MIDI-effekt: megnyomsz egy billentyűt, és egy egész akkordot küld a szintetizátorodnak. Hangot
nem ad ki magától, ezért egy hangszersávon a szintetizátor elé kell tenni.

## Gyors kezdés

1. **Telepítsd.** Futtasd a `mnKord-<verzió>-win-x64-setup.exe` fájlt (Windows). A VST3 a
   `C:\Program Files\Common Files\VST3\` mappába kerül. Indíts új pluginkeresést a DAW-ban.
2. **Tedd egy sávra.** A mnKordot egy hangszersávra tedd, **a szintetizátor elé**, amelyik az akkordokat játssza.
3. **Válassz presetet.** Kattints a preset nevére a fejlécben, és válassz egyet, például a *Pop Four Chords*-ot.
4. **Játssz.** Minden billentyű, amelyiknek van akkordja, azt játssza. A többi MIDI (pitch bend, szusztain, ...) átmegy.

Saját akkord készítése: kattints egy billentyűre az **INPUT** billentyűzeten, majd a hangokra a **CHORD**
billentyűzeten (vagy írj be egy akkordszimbólumot, például `Dm9/G`).

## A felhasználói útmutató

| | |
|---|---|
| **Felhasználói útmutató (magyar)** | [Olvasd itt](USER_GUIDE.hu.md) · [PDF](mnKord-Felhasznaloi-utmutato.pdf) |
| **User guide (English)** | [Read it here](USER_GUIDE.md) · [PDF](mnKord-User-Guide.pdf) |

Mi van benne:

- [Telepítés](USER_GUIDE.hu.md#telepítés) és [használat DAW-ban](USER_GUIDE.hu.md#használat-daw-ban)
- [Az ablak](USER_GUIDE.hu.md#az-ablak) és az [akkord készítése](USER_GUIDE.hu.md#akkord-készítése)
- [Velocity-rétegek](USER_GUIDE.hu.md#velocity-rétegek)
- [Presetek](USER_GUIDE.hu.md#presetek) és [projektek](USER_GUIDE.hu.md#projektek)
- [A Standalone program](USER_GUIDE.hu.md#standalone)
- [Tippek és hibák](USER_GUIDE.hu.md#tippek-és-hibák)

## Továbbiak

- [Letöltés](https://github.com/miklosnemeth/mnKord/releases/latest): a legfrissebb kiadás
- [Változásnapló](../CHANGELOG.md) (angolul): mi változott az egyes verziókban
- [Forráskód és fordítási útmutató](../README.md) (angolul, Windows, Linux)
- [Hibabejelentés, ötletek](https://github.com/miklosnemeth/mnKord/issues)
- [Főoldal](https://miklosnemeth.github.io/mnKord/) (weboldal képernyőképekkel)

A mnKord szabad szoftver az [AGPL-3.0](../LICENSE) alatt. Vibecoder: Németh Miklós.
