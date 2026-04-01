# BUCK mainnet emergency liveness patch terv (difficulty/hashrate shock)

## Rövid döntési javaslat

**Javasolt elsődleges irány:** vezessünk be egy *időalapú emergency min-difficulty* szabályt, amelyet:

- csak mainneten aktiválunk,
- csak egy fix aktivációs magasság után alkalmazunk,
- csak akkor enged min-difficulty-t, ha a következő blokk időbélyege jelentősen késik az előző blokkhoz képest.

Ez minimális, jól izolált kódváltoztatás (elsősorban `GetNextWorkRequired`), és célzottan a chain stall liveness problémára reagál, miközben normál üzemet alig érint.

**Másodlagos irány (önmagában nem elég):** csak `nPowMaxAdjustDown` emelése emergency szabály nélkül nem kezel azonnali stall-helyzetet, mert a retarget így is csak korlátozottan csökkent egy lépésben.

## Kiinduló megfigyelések a jelenlegi implementációból

- A retarget averaging-window alapú (`nPowAveragingWindow`) és max fel/le módosítási korlátot használ (`nPowMaxAdjustDown`, `nPowMaxAdjustUp`).
- A testnet-min-difficulty logika már létezik: ha engedélyezett és a blokk „túl későn” érkezik (`> 6 * targetSpacing`), akkor `powLimit` érvényesül.
- Mainneten jelenleg a `nPowAllowMinDifficultyBlocksAfterHeight = boost::none`, tehát ez az ág nem aktiválható.

## Minimális patch design

### 1) Új konszenzus paraméterek

A `Consensus::Params` struktúrába:

- `boost::optional<uint32_t> nPowEmergencyMinDifficultyAfterHeight;`
- `boost::optional<uint32_t> nPowEmergencyMinDifficultyDelayMultiplier;`

Értelmezés:

- **AfterHeight**: ettől a magasságtól (pontosabban `pindexLast->nHeight >= X`) él a szabály.
- **DelayMultiplier**: ha `next_block_time > prev_block_time + multiplier * targetSpacing`, akkor a következő blokk min-difficulty (`powLimit`) lehet.

Megjegyzés: külön paramétert használunk a meglévő testnet mezőktől, hogy ne keveredjenek a hálózatspecifikus viselkedések.

### 2) Döntési logika bővítése `GetNextWorkRequired`-ben

A jelenlegi testnet ág után (vagy mellette) egy új mainnet-emergency ág:

1. Ha az emergency mezők be vannak állítva.
2. Ha `pindexLast->nHeight` elérte az activation height-ot.
3. Ha `pblock` nem null.
4. Ha `pblock->GetBlockTime() > pindexLast->GetBlockTime() + PoWTargetSpacing(nextHeight) * multiplier`.
5. Akkor térjen vissza `nProofOfWorkLimit`-tel.

Minden más esetben marad a jelenlegi averaging-window retarget.

### 3) Chainparams beállítások

- **mainnet:** explicit aktivációs magasság + konzervatív multiplier.
- **testnet/regtest:** opcionálisan maradhat `boost::none`, vagy regtesten külön teszteléshez bekapcsolható.

## Küszöbérték opciók (k * targetSpacing)

A következő opciók mind konzervatívak; a kisebb `k` gyorsabb liveness-helyreállítást ad, a nagyobb `k` kisebb visszaélési felületet.

1. **k = 6 (testnet mintájú, agresszívebb)**
   - Előny: gyors mentés stall esetén.
   - Hátrány: hosszabb, de még átmeneti hash ingadozásoknál is könnyebben triggerelhet.

2. **k = 12 (ajánlott alapérték)**
   - Előny: lényegesen konzervatívabb, mégis időben kilazít tartós stallnál.
   - Hátrány: helyreállás lassabb, mint k=6.

3. **k = 18 (nagyon konzervatív)**
   - Előny: minimális normálüzemi beavatkozás.
   - Hátrány: valódi stallból lassabban hozza vissza a láncot.

**Ajánlás:** indulásnak `k = 12`, és csak szükség esetén csökkenteni 6-ra.

## Fork-következmény (hard/soft/no fork)

Ez **konszenzusszabály-változtatás**, ezért koordinálatlanul bevezetve **chain split**-et okozhat.

- Nem „no fork”.
- Leginkább **scheduled hard fork jellegű** kompatibilitás szempontból, mert a patch-et nem futtató node-ok elutasíthatják az emergency szabállyal elfogadott blokkokat.

Következmény: kötelező hálózati upgrade koordináció (release + activation height).

## Regtest reprodukció és tesztterv

### Reprodukció cél

Szimulálni, hogy egy túl magas difficulty után a blokkidők extrémen megnyúlnak, majd az emergency szabály engedi a min-difficulty blokkot.

### Javasolt lépések

1. Regtesten állítsunk olyan PoW paramétereket, hogy könnyen előidézhető legyen magas target-sokk (pl. kis `powLimit` különbséggel).
2. Bányásszunk gyors blokkokat (szimulált high hashrate), hogy a difficulty felmenjen.
3. Következő blokk timestampjét toljuk túl a `k * targetSpacing` küszöbön.
4. Ellenőrizzük, hogy `GetNextWorkRequired` `powLimit`-et ad.
5. Ellenőrizzük, hogy a küszöb alatti késésnél *nem* ad min-difficulty-t.
6. Reorg-szcenárió: emergency blokk után normál ritmusnál visszatér retarget logikára.

### Konkrét unit/integration tesztpontok

- `gtest/test_pow.cpp` új tesztek:
  - emergency disabled → nincs hatás.
  - enabled + height gate not reached → nincs hatás.
  - enabled + gate reached + delay below threshold → nincs hatás.
  - enabled + gate reached + delay above threshold → `powLimit`.

- (opcionális) Python functional test:
  - timestamp manipulációval valódi node-szintű validáció.

## Visszaélési kockázat elemzés

Lehetséges támadási minták:

- Időbélyeg-manipulációval min-difficulty triggerelés.
- Alacsony hash szakaszokban olcsóbb blokkgyártás.

Mi csökkenti a kockázatot:

- Magas (konzervatív) `k` választás.
- Activation height csak emergency rollout után.
- A szabály csak nagy késésnél él; normál blokkidőnél nincs hatása.
- A meglévő timestamp validációs szabályok továbbra is érvényben maradnak.

## Konkrét patch terv (fájlok / függvények / logika)

1. **`src/consensus/params.h`**
   - új optional mezők hozzáadása az emergency szabályhoz.

2. **`src/chainparams.cpp`**
   - mainnet consensus paramétereknél:
     - `nPowEmergencyMinDifficultyAfterHeight = <activation_height>`
     - `nPowEmergencyMinDifficultyDelayMultiplier = <k>`
   - testnet/regtest: kezdetben `boost::none` (vagy regtesten célzottan beállítva teszthez).

3. **`src/pow.cpp` (`GetNextWorkRequired`)**
   - új emergency elágazás a meglévő min-difficulty minta alapján.
   - minimális, jól olvasható check-sorozat; semmilyen admin override vagy runtime kapcsoló nélkül.

4. **`src/gtest/test_pow.cpp`**
   - 3–4 új teszteset a fenti mátrix szerint.

5. **(opcionális) `src/test/...` functional teszt**
   - end-to-end validáció timestamp és bányászat mellett.

## Rollout sorrend

1. Patch implementáció + unit tesztek.
2. Regtest/functional verifikáció.
3. Release candidate build.
4. Nyilvános kommunikáció: pontos aktivációs magasság és dátum.
5. Kötelező node upgrade ablak.
6. Aktiváció monitorozása (block interval, orphan rate, chainwork).

## Másodlagos opció értékelése: csak `nPowMaxAdjustDown` emelése

Önmagában **nem javasolt**, mert:

- Nem ad azonnali „mentőövet” hosszú blokk-kimaradáskor.
- Továbbra is averaging-window + clamp keretében lassan engedi le a difficulty-t.
- Pont a chain stall helyzetben az időalapú escape hatch hiányzik.

Legfeljebb kiegészítő finomhangolásként értelmezhető az emergency szabály mellett.

## Javasolt végső döntés

- Válasszuk az **időalapú emergency min-difficulty** megoldást.
- Mainneten aktiváljuk explicit magasságtól.
- Induló küszöbnek használjuk a **k = 12** értéket.
- `nPowMaxAdjustDown` emelését csak másodlagos, külön hatásvizsgálat után kezeljük.
