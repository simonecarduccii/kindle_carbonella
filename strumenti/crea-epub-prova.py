#!/usr/bin/env python3
"""Crea un piccolo EPUB di prova (testo di pubblico dominio: l'inizio di Pinocchio, Collodi 1883).

Serve per provare Carbonella senza dover cercare un libro: capitoli, corsivi, grassetti,
un'interruzione di scena, indice EPUB 3 (nav) ed EPUB 2 (ncx).
Uso: python3 strumenti/crea-epub-prova.py prova.epub
"""
import sys
import zipfile

CAPITOLI = [
    ("I. Come andò che maestro Ciliegia trovò un pezzo di legno", [
        "— <i>C’era una volta…</i>",
        "— Un re! — diranno subito i miei piccoli lettori.",
        "— No, ragazzi, avete sbagliato. C’era una volta un pezzo di legno.",
        "Non era un legno di lusso, ma un semplice pezzo da catasta, di quelli che d’inverno si mettono nelle stufe e nei caminetti per accendere il fuoco e per riscaldare le stanze.",
        "Non so come andasse, ma il fatto gli è che un bel giorno questo pezzo di legno capitò nella bottega di un vecchio falegname, il quale aveva nome mastr’Antonio, se non che tutti lo chiamavano <b>maestro Ciliegia</b>, per via della punta del suo naso, che era sempre lustra e paonazza, come una ciliegia matura.",
        "Appena maestro Ciliegia ebbe visto quel pezzo di legno, si rallegrò tutto; e dandosi una fregatina di mani per la contentezza, borbottò a mezza voce:",
        "— Questo legno è capitato a tempo: voglio servirmene per fare una gamba di tavolino.",
        "Detto fatto, prese subito l’ascia arrotata per cominciare a levargli la scorza e a digrossarlo; ma quando fu lì per lasciare andare la prima asciata, rimase col braccio sospeso in aria, perché sentì una vocina sottile sottile, che disse raccomandandosi:",
        "— Non mi picchiar tanto forte!",
        "Figuratevi come rimase quel buon vecchio di maestro Ciliegia!",
        "Girò gli occhi smarriti intorno alla stanza per vedere di dove mai poteva essere uscita quella vocina, e non vide nessuno! Guardò sotto il banco, e nessuno; guardò dentro un armadio che stava sempre chiuso, e nessuno; guardò nel corbello dei trucioli e della segatura, e nessuno; apri l’uscio di bottega per dare un’occhiata anche sulla strada, e nessuno. O dunque?…",
        "* * *",
        "— Ho capito; — disse allora ridendo e grattandosi la parrucca, — si vede che quella vocina me la son figurata io. Rimettiamoci a lavorare.",
        "E ripresa l’ascia in mano, tirò giù un solennissimo colpo sul pezzo di legno.",
        "— Ohi! tu m’hai fatto male! — gridò rammaricandosi la solita vocina.",
        "Questa volta maestro Ciliegia restò di stucco, cogli occhi fuori del capo per la paura, colla bocca spalancata e colla lingua giù ciondoloni fino al mento, come un mascherone da fontana.",
    ]),
    ("II. Maestro Ciliegia regala il pezzo di legno all’amico Geppetto", [
        "In quel punto fu bussato alla porta.",
        "— Passate pure, — disse il falegname, senza aver la forza di rizzarsi in piedi.",
        "Allora entrò in bottega un vecchietto tutto arzillo, il quale aveva nome Geppetto; ma i ragazzi del vicinato, quando lo volevano far montare su tutte le furie, lo chiamavano col soprannome di <i>Polendina</i>, a motivo della sua parrucca gialla che somigliava moltissimo alla polendina di granturco.",
        "Geppetto era bizzosissimo. Guai a chiamarlo Polendina! Diventava subito una bestia e non c’era più verso di tenerlo.",
        "— Buon giorno, mastr’Antonio, — disse Geppetto. — Che cosa fate costì per terra?",
        "— Insegno l’abbaco alle formicole.",
        "— Buon pro vi faccia!",
        "— Chi vi ha portato da me, compar Geppetto?",
        "— Le gambe. Sappiate, mastr’Antonio, che son venuto da voi, per chiedervi un favore.",
        "— Eccomi qui, pronto a servirvi, — replicò il falegname, rizzandosi su i ginocchi.",
        "— Stamani m’è piovuta nel cervello un’idea.",
        "— Sentiamola.",
        "— Ho pensato di fabbricarmi da me un bel burattino di legno; ma un burattino maraviglioso, che sappia ballare, tirare di scherma e fare i salti mortali. Con questo burattino voglio girare il mondo, per buscarmi un tozzo di pane e un bicchier di vino; che ve ne pare?",
    ]),
]


def xhtml(titolo, paragrafi):
    corpo = "\n".join(f"<p>{p}</p>" for p in paragrafi)
    return f"""<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" lang="it">
<head><title>{titolo}</title><style>p {{ text-indent: 1em; }}</style></head>
<body>
<h2>{titolo}</h2>
{corpo}
</body>
</html>
"""


def main(path):
    # make the book long enough to have many pages: repeat the chapters a few times
    capitoli = []
    for giro in range(3):
        for titolo, par in CAPITOLI:
            capitoli.append((titolo if giro == 0 else f"{titolo} ({giro + 1})", par))
    with zipfile.ZipFile(path, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", """<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>""")
        manifest, spine, nav, ncx = [], [], [], []
        z.writestr("OEBPS/testo/copertina.xhtml", '<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>Copertina</title></head><body><div><img src="../img/copertina.jpg" alt=""/></div></body></html>')
        manifest.append('<item id="copertina" href="testo/copertina.xhtml" media-type="application/xhtml+xml"/>')
        spine.append('<itemref idref="copertina"/>')
        for i, (titolo, par) in enumerate(capitoli, 1):
            nome = f"testo/cap{i:02d}.xhtml"
            z.writestr("OEBPS/" + nome, xhtml(titolo, par))
            manifest.append(f'<item id="c{i}" href="{nome}" media-type="application/xhtml+xml"/>')
            spine.append(f'<itemref idref="c{i}"/>')
            nav.append(f'<li><a href="{nome}">{titolo}</a></li>')
            ncx.append(f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>{titolo}</text></navLabel><content src="{nome}"/></navPoint>')
        z.writestr("OEBPS/nav.xhtml", f"""<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Indice</title></head>
<body><nav epub:type="toc"><ol>{''.join(nav)}</ol></nav></body></html>""")
        z.writestr("OEBPS/toc.ncx", f"""<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><navMap>{''.join(ncx)}</navMap></ncx>""")
        z.writestr("OEBPS/content.opf", f"""<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:identifier id="id">carbonella-prova</dc:identifier>
<dc:title>Le avventure di Pinocchio</dc:title>
<dc:creator>Carlo Collodi</dc:creator>
<dc:language>it</dc:language>
</metadata>
<manifest>
<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
{chr(10).join(manifest)}
</manifest>
<spine toc="ncx">{''.join(spine)}</spine>
</package>""")
    print(f"creato {path}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "prova.epub")
