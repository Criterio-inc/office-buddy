# Office Buddy

Ett ansikte på hyllan som blinkar, gäspar, tittar på en och reagerar på vad
som händer under dagen. Inga ord, bara ögon, mun och färger.

Ansiktet lyser cyanblått ur en svart yta, som i förlagan. Svart är inte ett
stilval utan ett hårdvaruval: på en AMOLED är en släckt bildpunkt verkligen
släckt.

## Vad som skiljer en buddy från en pryl

Det här stod klart innan bygget, så att det inte glöms bort under vägen.

1. **Oregelbundenhet gör det levande.** En blinkning var tredje sekund är en
   animation; en efter 2,4 sekunder, sedan 5,1, sedan en dubbelblinkning, är
   en varelse. Ingenting i ansiktet upprepas exakt likadant.
2. **Inre tillstånd, inte bara reaktioner.** Uttrycket följer av ett humör som
   ändras över dagen och av vad som händer, inte av enskilda händelser.
3. **Tystnad är en funktion.** Uttryck hellre med ansiktet än med text.
4. **Det som sägs bygger på verklig data**: kalendern, mejlen, påminnelserna
   och de verktyg du själv kopplar in.
5. **Den ska titta på en.** Blicken vandrar, stannar, kommer tillbaka.

## Hårdvaran

Waveshare ESP32-S3-Touch-AMOLED-1.8, stående 368 × 448. Kortet sitter i
datorn på USB-C hela tiden, både för ström och för data.

| Del | Roll |
|---|---|
| CO5300 AMOLED | ansiktet |
| CST816S pekskärm | petningar |
| QMI8658 rörelsesensor | knack och lyft |
| PCF85063 klocka | tid på dygnet, humöret |
| ES8311 ljud | tre små toner, inga ord |

## Delarna

**`delat/ansikte.c`** är hela ansiktet: ritkod, uttryck och liv. Modulen
känner bara till LVGL och byggs in i både kortet och emulatorn, så det som
syns i fönstret är exakt det som hamnar på glaset.

Ansiktet ritas som former, inte bilder. Ögonen är rundade rektanglar som
täcks av svarta lock och bågar, ögonbrynen är korta streck som tonar in i
de uttryck som behöver dem, munnen är en båge, ett streck eller en öppen
form. Varje uttryck är en uppsättning tal, och rörelse är att talen glider mot
sina mål. Därför hoppar aldrig ett byte av uttryck, och därför kan en
blinkning lägga sig ovanpå vad som helst.

**`delat/humor.c`** är det inre tillståndet: tre tal som styr vilket
uttryck ansiktet får. Energin följer dygnet (pigg på förmiddagen, seg vid
tretiden, sover från elva till sex), glädjen går upp av en petning eller en
avbockad sak och klingar av på en minut, oron kommer av det som ligger och
väntar. Grunduttrycket byts först när ett annat val hållit i sig i tre
sekunder, och aldrig oftare än var sjätte. Ovanpå kommer små episoder med
slumpade mellanrum: en fundersam blick, en gäspning när energin är låg, en
orolig stund när mycket väntar. Och ofta ingenting alls. Tystnad är också ett
svar.

**`sim/`** är emulatorn på MacBooken. **`firmware/`** är kortets del: en
tunn fil som startar panelen, läser klockkretsen och kopplar in ansiktet
och humöret, plus samma byggkonfiguration som Projektpulsen använde.

## Emulatorn

```bash
cd ~/Projects/office-buddy && cmake -S sim -B sim/build -G Ninja && ninja -C sim/build && ./sim/build/office-buddy-sim
```

Fönstrets titel visar klockan, humöret och uttrycket. Humöret styr tills du
själv väljer ett uttryck; M lämnar tillbaka rodret.

| Tangent | Gör |
|---|---|
| vänster och höger | bläddra mellan uttrycken, och stäng av humöret |
| M | slå på humöret igen |
| mellanslag | tillbaka till neutral |
| B | blinka nu |
| G | gäspa |
| P eller ett klick | peta på buddyn |
| K, L, A | knacka, lyfta, bocka av något |
| + och - | vrid klockan en timme fram eller tillbaka |
| skift + piltangent | titta åt det hållet |
| Esc | avsluta |

Vill du se hur den beter sig vid en annan tid, eller snabbare:

```bash
./sim/build/office-buddy-sim --klocka 14:30 --fart 20 --vantande 16 --roda 3
```

Ett helt dygn som bildremsa, en bild per timme:

```bash
./sim/build/office-buddy-sim --dag sim/bilder/dag && python3 verktyg/kontaktark.py sim/bilder/dygn.png --bmp "sim/bilder/dag-*.bmp" --kol 6
```

Vill du ha en stillbild i stället för ett fönster:

```bash
./sim/build/office-buddy-sim --bild /tmp/buddy.bmp --uttryck nöjd --tid 900 && python3 verktyg/bmp2png.py /tmp/buddy.bmp /tmp/buddy.png
```

Alla uttryck på en gång, som ett kontaktark:

```bash
python3 verktyg/kontaktark.py sim/bilder/kontaktark.png
```

En rörelse bild för bild, till exempel en blinkning:

```bash
./sim/build/office-buddy-sim --serie /tmp/blink --antal 10 --steg 33 --uttryck glad --blink
```

## Kortet

Bygg och flasha. Portnamnet följer USB-porten, inte kortet, så kontrollera
det först med `ls /dev/cu.usbmodem*`:

```bash
source ~/esp/esp-idf/export.sh && cd ~/Projects/office-buddy/firmware && idf.py build
```

```bash
source ~/esp/esp-idf/export.sh && cd ~/Projects/office-buddy/firmware && idf.py -p /dev/cu.usbmodem144301 flash monitor
```

Sitter kortet tyst vid flashning: håll BOOT, tryck RESET, släpp BOOT.

Kortet arbetar i UTC rakt igenom. Datorn skickar sin tidszon tillsammans
med tiden, och utan dator faller kortet tillbaka på centraleuropeisk tid
med sommartidsregeln. Har klockkretsen
tappat tiden ställs den efter byggtiden, så att kortet ligger inom minuter
från rätt direkt efter en flashning. Datorn ställer den exakt i fas 4.

Var tionde sekund sätts ljuset om, av samma skäl som i Projektpulsen:
panelens strömförsörjning går via en I2C-expander som kan tappa sitt
tillstånd. När buddyn sover dämpas glaset. En gång i minuten skriver kortet
ut klockslag, energi, glädje, oro och uttryck i loggen.

Projektpulsen ligger kvar i `~/Projects/projektpulsen` och kan flashas
tillbaka när som helst.

## Kvittot

Orange är en fråga, och ett knack på bordet eller ett tryck på glaset är
svaret. Färgen glider tillbaka till cyan, raden försvinner och buddyn
nickar nöjt utan ljud, vad det än var som frågade: Claude, ett möte, en
påminnelse, backupen eller timern.

## Svep på glaset

Ansiktet är hemma. Ett svep åt vänster ger **klockan**: tiden stor mitt på
glaset, datumet på svenska under, och sekunderna som en tunn linje som
växer över glaset i stället för siffror. Ett svep till ger **timern**:
tryck upptill lägger på fem minuter, nedtill drar av fem, mitten startar,
och ett tryck medan den går pausar. När tiden är ute kommer ansiktet
tillbaka i orange med "tiden är ute" och trudelutten, tills du trycker.
Timern går vidare i bakgrunden om du sveper hem. Ett svep till är hemma.

Från datorn: `vy klocka`, `vy timer`, `timer 25`.

## Datorn sover, buddyn sover

Datorn skriver till kortet var trettionde sekund. Har den varit tyst i två
minuter sover datorn, och då somnar buddyn oavsett klockslag. När datorn
vaknar och länken kommer tillbaka vaknar buddyn med en gäspning och
trudelutten, om det inte är natt.

## Kalendern och mejlen

Länktjänsten läser macOS egna kalender via ett litet EventKit-program,
`server/kalender`, som byggs ur `kalender.swift` första gången. Alla riktiga
konton i Kalender-appen räknas, men inte prenumerationer, födelsedagar,
helgdagar eller hockeyn. Tio minuter före ett möte blir buddyn
orange, tittar upp och säger "möte om 10 min: styrgruppen" med
tvåtonen; när mötet börjar säger den "möte nu" med trudelutten. Första
gången måste programmet få tillgång till kalendern i Systeminställningar
under Integritet och säkerhet.

Påminnelserna går samma väg: `server/kalender paminnelser` listar de som
förfallit eller förfaller inom fem minuter, och när en förfaller blir
buddyn orange en halv minut, tittar upp och säger "påminnelse: ring
Niclas" med ett blipp. En gång per påminnelse.

Mejlen läses ur Mail-appens samlade inkorg var annan minut. Blir de olästa
fler kastar buddyn en blick åt sidan, skiftar till kontots färg och säger
"nytt mejl: avsändare, ämne" i åtta sekunder, utan ljud, och sedan är den
tyst i minst tio minuter. Mail måste vara igång. Färgerna står i
`kontofarger` i `server/buddy.json`, med kontonas namn som de heter i
Mail.

## Backupvakten

Valfritt. Pekar `backup_status` och `offsite_status` i `server/buddy.json`
på JSON-filer med fältet `senasteLyckade` (och ev. `senasteFel`), läser
länktjänsten dem en gång om dagen efter `backup_koll_efter`. Är någon
äldre än ett dygn eller har ett fel, blir buddyn orange en minut, bekymrad
en stund, och säger "backup saknas: molnet". Är allt som det ska säger den
ingenting.

## Replikerna

Buddyn säger få saker, och bara sådant som är sant. En dämpad rad under
ansiktet tonar in, står i några sekunder och tonar ut. Oron får ord högst
var tionde minut, i samband med en orolig stund: "varstadning-skrivbord har
väntat i 126 dagar nu", hämtat ur pulstjänsten om en sådan är inkopplad
(`puls_url` i `server/buddy.json`). En avbockning ger "en sak
mindre!". Morgonen och natten får ett ord var. Resten är tystnad.

Datorn kan också lägga ord i munnen på den:

```bash
python3 ~/Projects/office-buddy/server/buddylank.py --skicka "sag lunch om tio minuter"
```

## Ljudet

Buddyn är tyst av princip. Det som finns är ett litet blipp när någon
knackar eller petar, en stigande tvåton när den lyfts, och tre glada toner
när något som väntade blev gjort. Volymen är låg. Tystas helt med:

```bash
python3 ~/Projects/office-buddy/server/buddylank.py --skicka "ljud 0"
```

## Knapparna

Knapparna på sidan styr volymen: BOOT sänker, strömknappen höjer, i sex
steg från tyst till 80 procent. Varje steg hörs som ett blipp och visas
under ansiktet, och nivån sparas så att den överlever en omstart. Håll inte
strömknappen inne: efter sex sekunder stänger strömkretsen av kortet.

## Rörelsesensorn

BSP:n saknar drivrutin för QMI8658, så `firmware/main/rorelse.c` skriver
registren själv och läser accelerometern femtio gånger i sekunden. Ett
lågpassfilter följer tyngdkraften. Det som avviker snabbt från den är en
stöt: ett **knack**, som gör buddyn lite gladare och får den att titta upp.
Om tyngdkraftens riktning i stället vandrar bort från viloläget och stannar
där har någon **lyft** kortet: buddyn blir förvånad, vaknar om den sov och
håller sig pigg en stund. Viloläget lärs in på nytt varje gång kortet legat
stilla i några sekunder, så det spelar ingen roll hur det står. Kortet
loggar tyngdkraftsvektorn en gång i minuten.

## Länken från datorn

Kortet får rader över USB enligt `delat/protokoll.h`: klockan, vad som
ligger och väntar ur en valfri pulstjänst, och en avbockning när något som
väntade försvann. Svaren börjar med `ob ` och går på samma ström som kortets logg.

| Rad | Gör |
|---|---|
| `hej` | svarar med uttrycket |
| `status` | energi, glädje, oro, humör och uttryck |
| `tid <epok>` | ställer klockan, unix-tid i UTC |
| `vantande <antal> <röda>` | vad som väntar |
| `avbockat` | något blev gjort, buddyn blir väldigt glad en stund |
| `peta`, `knack`, `lyft` | händelser till humöret |
| `uttryck <namn> [ms]` | ett uttryck en stund |
| `ljus <procent>` | glasets ljusstyrka |

`server/buddylank.py` sköter det från Macen. `server/installera.sh` lägger
in den som launchd-tjänsten `se.critero.office-buddy`, med logg i
`~/Library/Logs/office-buddy.log`. Inställningarna står i
`server/buddy.json` (mall: `buddy.example.json`). Den hittar kortet själv,
ställer klockan var tionde minut och rör aldrig DTR och RTS, så kortet
startar inte om när den ansluter. Emulatorn läser samma rader på stdin.

En rad för hand:

```bash
python3 ~/Projects/office-buddy/server/buddylank.py --skicka "uttryck kär 3000"
```

### Brevlådan och Claude

Andra program på Macen når kortet genom tjänstens brevlåda: posta rader
till `http://127.0.0.1:8739/`, en per textrad. Claude Codes krokar gör det
när Claude behöver dig: `claude vantar` när den ber om tillstånd eller
ett svar, `claude klar` när den är färdig, `claude jobbar` när du svarat.
Varje session skickar med sitt id och projektmappens namn, så buddyn
skiljer sessionerna åt: en ny session som väntar annonseras alltid, även
om en annan redan väntar, och raden säger vilket projekt det gäller,
"Claude väntar: office-buddy och 1 till". Buddyn skiftar till orange,
tittar upp mot datorn och håller raden kvar tills du är tillbaka, med en
liten blick åt datorns håll varannan minut. När du svarar i en session
försvinner just den; det som återstår avgör vad glaset visar. Ett knack
kvitterar allt. Sessioner som inte kör Claude Codes krokar, som Cowork
och webben, syns inte. Krokarna läggs in med `python3 server/claude-krokar.py` och anropar
`server/buddy-krok.sh`.

```bash
curl -s -X POST --data-binary "sag lunch om tio minuter" http://127.0.0.1:8739/
```

**Tjänsten håller porten**, så stoppa den innan en flashning och starta den
efteråt:

```bash
launchctl unload ~/Library/LaunchAgents/se.critero.office-buddy.plist
```

```bash
launchctl load ~/Library/LaunchAgents/se.critero.office-buddy.plist
```

## Uttrycken

neutral, glad, väldigt glad, förvånad, entusiastisk, nöjd, blinkning, ledsen,
besviken, orolig, arg, fundersam, trött, sömnig, gäspar, stressad, nyfiken,
kär, överväldigad, sover.

## Koden

Öppen källkod under MIT: `Criterio-inc/office-buddy`. Engelsk README i
[README.md](README.md).

## Lärdomar som följer med från Projektpulsen

Samma kort, samma LVGL, samma fällor. De tio som kostade tid står i
`~/Projects/projektpulsen/README.md`. De som redan påverkat det här bygget:
ritbufferten är justerad till 64 byte, `LV_USE_LOG` står på, och emulatorn
sätter `lv_tick_set_cb` så att LVGL har en tidskälla.

**Ljuskommandot får inte skickas medan LVGL flushar.** Ljusstyrkan sätts
med ett kommando på samma SPI-buss som bildpunkterna går på, och esp_lcd:s
panel-IO tål inte två uppgifter samtidigt. Projektpulsen ritade var
trettionde sekund och kom undan; buddyn ritar trettio gånger i sekunden, och
kortet frös direkt efter "Backlight on": ingen krasch, ingen omstart, ingen
vakthund, bara tystnad. Både LVGL-uppgiften och huvuduppgiften stod stilla,
den senare i väntan på en buss som den första aldrig släppte. Felet var
tajmingberoende: med två spårrader i ritkoden gick det igenom, utan dem
frös det igen. Alla ljuskommandon går nu under LVGL-låset, där ingen flush
pågår. LVGL-uppgiften fick samtidigt 16 kB stack i stället för 4, som
marginal för bågar och trianglar med kantutjämning.

**Svart glas på ett kort som lever: expandern.** Fem till åtta minuter
efter start slocknade panelen medan chippet loggade vidare, samma sak som
Projektpulsen råkade ut för. Att skicka om ljusstyrkan hjälpte inte, och
strömkretsens register var oförändrade. Schemat gav svaret: panelens reset
och strömaktivering går via I/O-expandern TCA9554 (EXIO0 och EXIO1,
pekskärmens reset på EXIO2), och BSP:n rör aldrig expandern, så alla
pinnar stod som ingångar och panelens ström hängde på ett pull-up-motstånd.
`firmware/main/panelstrom.c` sätter pinnarna som utgångar, ger panelen ett
riktigt reset-pulståg före starten och säkrar läget var tionde sekund.

En till: **det som täcker ett öga får bara verka inom ögats egen ruta.** Locken
och bågarna ritas i bakgrundsfärg, och utan klippning skulle en glad båge
kunna bita i munnen. Klippningen sker genom att tillfälligt krympa lagrets
`_clip_area`.
