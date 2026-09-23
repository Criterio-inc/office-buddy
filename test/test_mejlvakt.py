import sys
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'server'))
import buddylank


class Mejltest(unittest.TestCase):
    def test_nytt_last_mejl_ger_signal_men_inte_start_eller_dubblett(self):
        vakt = buddylank.Mejlvakt()
        lank = Mock()
        with patch.object(buddylank, 'mejl_lage', side_effect=[
            (0, 'A', 'Gammalt', '', '10'),
            (0, 'B', 'Nytt', '', '11'),
            (0, 'B', 'Nytt', '', '11'),
        ]):
            vakt.kolla(lank)
            lank.skicka.assert_not_called()
            vakt.kolla(lank)
            vakt.kolla(lank)
        lank.skicka.assert_called_once_with('mejl B, Nytt')

    def test_tillfalligt_mailfel_tappar_inte_jamforelsen(self):
        vakt = buddylank.Mejlvakt()
        lank = Mock()
        with patch.object(buddylank, 'mejl_lage', side_effect=[
            (0, 'A', '', '', '10'), None, (0, 'B', '', '', '11')
        ]):
            for _ in range(3):
                vakt.kolla(lank)
        lank.skicka.assert_called_once_with('mejl B')


class Pulsfiltertest(unittest.TestCase):
    def test_bortvalt_projekt_forsvinner_aven_ur_antalet(self):
        puls = {"antal": 2, "poster": [{"projekt": "gammalt"}, {"projekt": "aktuellt"}]}
        with patch.dict(buddylank.INST, {"puls_ignorera": ["gammalt"]}):
            resultat = buddylank.filtrera_puls(puls)
        self.assertEqual(resultat, {"antal": 1, "poster": [{"projekt": "aktuellt"}]})
        self.assertEqual(len(puls["poster"]), 2)


class Bakgrundstest(unittest.TestCase):
    def test_langsam_kalla_blockerar_inte_och_startas_bara_en_gang(self):
        import threading
        klar = threading.Event()
        borjat = threading.Event()
        class Kalla:
            antal = 0
            def kolla(self, lank):
                self.antal += 1
                borjat.set()
                klar.wait(2)
        kalla = Kalla()
        vakt = buddylank.Bakgrundsvakt(kalla)
        vakt.kolla(None)
        self.assertTrue(borjat.wait(1))
        try:
            vakt.kolla(None)
            self.assertEqual(kalla.antal, 1)
        finally:
            klar.set()
            vakt.trad.join(2)
