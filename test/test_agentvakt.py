import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'server'))
from agentvakt import Agentvakt, radtext

class Agenttest(unittest.TestCase):
    def setUp(self):
        self.rader=[]; self.nu=0
        self.v=Agentvakt(self.rader.append, ('claude','codex'), lambda:self.nu)
    def data(self, state='working', activity=None, event='a', age=0):
        return {'v':2,'agents':{'claude':{'jobs':[]},'codex':{'jobs':[
            {'task_id':'session','event_id':event,'state':state,'activity':activity,
             'updated_ms':age,'project':'Svenskt projekt'}]}}}
    def test_vanligt_waiting_ar_inte_fraga(self):
        self.v.uppdatera(self.data('waiting'))
        self.assertEqual(self.rader,[])
    def test_fraga_fornyas_utan_ny_identitet_och_forsvinner(self):
        self.v.uppdatera(self.data('waiting','waiting_input'))
        self.assertIn('codex vantar',self.rader[0]); self.nu=5
        self.v.uppdatera(self.data('waiting','waiting_input')); self.assertEqual(len(self.rader),1)
        self.nu=26; self.v.uppdatera(self.data('waiting','waiting_input'))
        self.assertEqual(self.rader[0],self.rader[1])
        self.v.uppdatera(self.data()); self.assertIn('codex borta',self.rader[-1])
    def test_gamla_klara_spelas_inte_vid_start(self):
        self.v.uppdatera(self.data('done')); self.assertFalse(self.rader)
    def test_ny_klart_spelas_en_gang(self):
        self.v.uppdatera(self.data()); self.rader.clear(); d=self.data('done',event='b')
        self.v.uppdatera(d); self.v.uppdatera(d)
        self.assertEqual(len(self.rader),1); self.assertIn('codex klar',self.rader[0])
    def test_gammal_klart_spelas_inte_efter_pollavbrott(self):
        self.v.uppdatera(self.data()); self.rader.clear(); self.v.uppdatera(self.data('done',event='b',age=60000))
        self.assertFalse(self.rader)
    def test_explicit_pending_codex_och_utgangen(self):
        d=self.data(); d['pending']={'provider':'codex','request_id':'q1','expires_in_ms':5000,'project':'App'}
        self.v.uppdatera(d); self.assertIn('codex vantar',self.rader[-1])
        d['pending']['expires_in_ms']=0; self.v.uppdatera(d)
        self.assertIn('codex borta',self.rader[-1]); self.assertFalse(any('klar' in x for x in self.rader))
    def test_forr_claude_pending_utan_provider(self):
        d=self.data();d['pending']={'request_id':'q1','expires_in_ms':5000}
        self.v.uppdatera(d);self.assertTrue(any('claude vantar' in x for x in self.rader))
    def test_avbrott_tystar_och_aterstart_firar_inte(self):
        self.v.uppdatera(self.data('waiting','waiting_approval'));self.nu=31;self.v.fel()
        self.assertIn('borta',self.rader[-1]);n=len(self.rader)
        self.v.uppdatera(self.data('done',event='b'));self.assertEqual(len(self.rader),n)
    def test_ateranslut_aterstaller_bara_fragor(self):
        self.v.uppdatera(self.data('waiting','waiting_input'));self.v.ateranslut()
        self.assertEqual(self.rader[0],self.rader[1])
    def test_felaktigt_schema_bevarar_fraga(self):
        self.v.uppdatera(self.data('waiting','waiting_input'))
        with self.assertRaises(ValueError):self.v.uppdatera({'v':2,'agents':{}})
        self.assertEqual(len(self.rader),1)
    def test_text_kan_inte_skicka_extra_kommandon(self):
        self.assertEqual(radtext('App\nagent codex klar x'), 'App agent codex klar x')
        self.assertLessEqual(len(radtext('å'*100).encode()),96)
    def test_provider_kan_valjas(self):
        self.v.aktorer={'claude'};self.v.uppdatera(self.data('waiting','waiting_input'))
        self.assertFalse(self.rader)

    def test_klar_fornyas_inte_och_slacks_nar_samma_uppgift_fortsatter(self):
        self.v.uppdatera(self.data());self.rader.clear()
        d=self.data('done',event='b')
        self.v.uppdatera(d)
        self.nu=26;self.v.uppdatera(d)
        self.assertEqual(len(self.rader), 1)
        self.v.uppdatera(self.data('working',event='c'))
        self.assertIn('codex borta',self.rader[-1])
    def test_klar_aterstalls_inte_vid_usb_ateranslutning(self):
        self.v.uppdatera(self.data());self.rader.clear();self.v.uppdatera(self.data('done',event='b'))
        self.v.ateranslut();self.assertEqual(len(self.rader),1)
    def test_klar_slacks_vid_forlorad_kalla(self):
        self.v.uppdatera(self.data());self.rader.clear();self.v.uppdatera(self.data('done',event='b'))
        self.nu=31;self.v.fel();self.assertIn('codex borta',self.rader[-1])

    def test_ny_codex_turn_slacker_gamla_klara_men_inte_fragor(self):
        self.v.uppdatera(self.data())
        done=self.data('done',event='b')
        self.v.uppdatera(done)
        old_id=done['agents']['codex']['jobs'][0]['task_id']
        d=self.data('working',event='c')
        d['agents']['codex']['jobs'][0]['task_id']='ny-turn'
        d['agents']['codex']['jobs'] += done['agents']['codex']['jobs']
        d['pending']={'provider':'codex','request_id':'annan-fraga','expires_in_ms':5000}
        self.v.uppdatera(d)
        self.assertFalse(self.v.klara)
        self.assertTrue(self.v.fragor)
        self.assertTrue(any('codex borta' in x for x in self.rader))
        n=len(self.rader);self.nu=26;self.v.uppdatera(d)
        self.assertFalse(any('codex klar' in x for x in self.rader[n:]))

    def test_omstart_synkar_arbetande_aktor_utan_att_ateranvanda_klara(self):
        self.v.uppdatera(self.data())
        self.assertEqual(len(self.rader),1)
        self.assertIn('codex jobbar',self.rader[0])
        self.v.uppdatera(self.data());self.assertEqual(len(self.rader),1)

    def test_snabb_ny_turn_ersatter_aldre_klar_utan_observerad_working(self):
        self.v.uppdatera(self.data())
        old=self.data('done',event='b');self.v.uppdatera(old)
        d=self.data('done',event='c');d['agents']['codex']['jobs'][0]['task_id']='snabb-turn'
        d['agents']['codex']['jobs'] += old['agents']['codex']['jobs']
        n=len(self.rader);self.v.uppdatera(d)
        self.assertEqual(len(self.v.klara),1)
        self.assertTrue(any('codex borta' in x for x in self.rader[n:]))
        self.assertIn('codex klar',self.rader[-1])

if __name__=='__main__':unittest.main()
