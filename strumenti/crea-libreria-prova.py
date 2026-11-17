#!/usr/bin/env python3
"""Crea una libreria finta con gli stessi libri del frame Figma "00 · Home 3 colonne", per confrontare
le schermate del simulatore con il design.

Uso: python3 strumenti/crea-libreria-prova.py LIBRO_BASE.epub CARTELLA_LIBRI CARTELLA_DATI

Copia LIBRO_BASE.epub cambiando titolo e autore; mette tre libri nella cartella "Saggi Brevi"
(diventa una raccolta) e scrive dati/posizioni.tsv con letture e percentuali come nel design.
"""
import os
import re
import sys
import time
import zipfile

LIBRI = [
    # (titolo, autore, sottocartella, percentuale o None, minuti fa dell'ultima lettura)
    ("I promessi sposi", "Alessandro Manzoni", "", 30, 5),
    ("Il barone rampante", "Italo Calvino", "", 10, 60),
    ("Lessico famigliare", "Natalia Ginzburg", "", 100, 600),
    ("Lezioni americane", "Italo Calvino", "Saggi Brevi", 40, 120),
    ("Sei passeggiate", "Umberto Eco", "Saggi Brevi", None, 0),
    ("Il sistema periodico", "Primo Levi", "Saggi Brevi", None, 0),
    ("Se una notte d'inverno un viaggiatore", "Italo Calvino", "", None, 0),
    ("Il nome della rosa", "Umberto Eco", "", 12, 900),
    ("Il deserto dei Tartari", "Dino Buzzati", "", 100, 2000),
    ("Se questo è un uomo", "Primo Levi", "", 100, 3000),
    ("La luna e i falò", "Cesare Pavese", "", 55, 1200),
    ("Il fu Mattia Pascal", "Luigi Pirandello", "", None, 0),
    ("Gli indifferenti", "Alberto Moravia", "", None, 0),
]


def main(base, books, data):
    src = zipfile.ZipFile(base)
    os.makedirs(data, exist_ok=True)
    now = int(time.time())
    pos = []
    for i, (titolo, autore, cartella, perc, fa) in enumerate(LIBRI):
        d = os.path.join(books, cartella) if cartella else books
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, "libro%02d.epub" % i)
        with zipfile.ZipFile(path, "w") as z:
            for info in src.infolist():
                data_b = src.read(info.filename)
                if info.filename.endswith(".opf"):
                    t = data_b.decode("utf-8")
                    t = re.sub(r"<dc:title>.*?</dc:title>", "<dc:title>%s</dc:title>" % titolo, t)
                    t = re.sub(r"<dc:creator>.*?</dc:creator>", "<dc:creator>%s</dc:creator>" % autore, t)
                    data_b = t.encode("utf-8")
                new = zipfile.ZipInfo(info.filename, info.date_time)  # a copy: writestr changes it
                new.compress_type = info.compress_type
                z.writestr(new, data_b)
        os.utime(path, (now - 30 * 86400 + i, now - 30 * 86400 + i))
        if perc is not None:
            pos.append("%s\t1\t0\t%d\t%d\t%d\n" % (path, perc, now - fa * 60, 450))
    with open(os.path.join(data, "posizioni.tsv"), "w") as f:
        f.writelines(pos)


if __name__ == "__main__":
    main(*sys.argv[1:4])
