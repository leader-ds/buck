# BUCK chain stall / consensus investigation

## Executive summary

A static source-code review of this BUCK node (Zcash-derived) indicates three high-probability root-cause classes for a “network-wide freeze” where pools, wallets, and explorers all stop advancing:

1. **Difficulty dead-zone after hashpower shock** (most likely): the current PoW retarget path uses a short averaging window and bounded per-step adjustment. A large transient miner can drive difficulty up and leave remaining hashrate unable to find blocks in practical time. Relevant logic is in `GetNextWorkRequired` and `CalculateNextWorkRequired`.  
2. **Peer graph degradation / partition**: BUCK mainnet uses fixed seeds and no configured DNS seeds in `chainparams`; if seed set is stale or clustered, pools may self-isolate and fail to converge on the same best chain quickly.  
3. **Consensus-rule mismatch across node versions**: activation heights, protocol-version gates, and header/block checks can split peers when operators run mixed binaries or stale configs, causing “valid for some, invalid for others” behavior.

Most dangerous scenario: **silent partition plus acceptance divergence** (headers accepted then blocks rejected, or upgrade mismatch) because operators can mistake this for “no hashrate”, while network actually forks.

Fastest triage order:

1. Check if blocks are missing globally vs only explorer/index (`getblockcount`, `getbestblockhash`, `getchaintips`, `getpeerinfo`).
2. Check reject reasons and header acceptance failures (`debug.log` for `bad-diffbits`, `time-too-*`, `invalid-solution`, `bad-version`, `bad-prevblk`).
3. Check peering health and seed/bootstrap quality (peer counts, common tip hashes across pools).
4. Verify operators run the exact same binary and consensus params (activation heights, Equihash N/K, PoW params).

---

## Observed risk model

### Consensus-critical paths

- Block header validation: `CheckBlockHeader`, `ContextualCheckBlockHeader`, `AcceptBlockHeader`.  
- Full block validation and activation: `CheckBlock`, `ContextualCheckBlock`, `AcceptBlock`, `ProcessNewBlock`, `ActivateBestChain`.  
- PoW and difficulty: `GetNextWorkRequired`, `CalculateNextWorkRequired`, `CheckProofOfWork`, `CheckEquihashSolution`.  
- Time rules: median-time-past and future-time checks in contextual header validation.

### Operational fragility paths

- Peer discovery and connectivity: fixed seeds, outbound/inbound limits, peer eviction, addrman bootstrap behavior.
- IBD/safety gating with `nMinimumChainWork` and expected upgrade block hashes.
- Explorer/index dependence on txindex/addressindex/spentindex/timestampindex and `-insightexplorer`/`-lightwalletd` flags.

---

## Most likely root causes

### 1) Difficulty / retarget lock-up after high-hash transient (Priority: P1)

**Why realistic**

- BUCK mainnet has `nPowAveragingWindow = 13`, `nPowMaxAdjustDown = 34`, `nPowMaxAdjustUp = 34`, with Equihash target spacing derived from consensus constants. A large transient miner can raise effective difficulty; after departure, remaining pools may need a long wall-clock to find each next block.
- Retarget uses MTP and bounded adjustment, then clamps to `powLimit`. This can become slow-to-recover under abrupt hashrate collapse.

**Relevant files/functions**

- `src/chainparams.cpp`: mainnet PoW parameters (`powLimit`, averaging window, adjust bounds, spacing).
- `src/pow.cpp`: `GetNextWorkRequired`, `CalculateNextWorkRequired`.
- `src/main.cpp`: `ContextualCheckBlockHeader` checks `GetNextWorkRequired` via `bad-diffbits` reject.

**How to confirm / falsify**

- RPC: `getblockchaininfo` (difficulty, best height), `getblockheader <tiphash> true` and previous ~50 blocks for time spacing, `getnetworkhashps` if available.
- Logs: look for long inter-block times without persistent validation errors; absence of reject storms supports this hypothesis.

---

### 2) Partition / weak bootstrap / peer-starvation (Priority: P1)

**Why realistic**

- Mainnet `vSeeds.clear()` (DNS seeds empty), relying on compiled fixed seeds. If fixed peers are stale or concentrated, nodes may connect to limited islands.
- Outbound connections are capped, peer eviction may prefer certain peers, and isolated pool clusters can continue on local tips without enough cross-links.

**Relevant files/functions**

- `src/chainparams.cpp`: `vSeeds.clear()`, `vFixedSeeds` population.
- `src/net.cpp`: DNS seeding thread, fixed seed fallback, addrman selection, outbound connect loops, eviction.
- `src/main.cpp`: headers/block relay and best-chain selection (`FindMostWorkChain`, `ActivateBestChain`).

**How to confirm / falsify**

- RPC: `getpeerinfo`, `getnetworkinfo`, `getchaintips` across multiple operators; compare best header hash / best block hash.
- Logs: repeated small peer set, frequent disconnect/reconnect, no fresh addrman growth.

---

### 3) Consensus-version mismatch (activation/protocol/Equihash params) (Priority: P1/P2)

**Why realistic**

- Upgrades (Overwinter/Sapling/Blossom/Heartwood/Canopy) are height-gated in chainparams.
- Peers with obsolete protocol can be disconnected/rejected depending on epoch and settings.
- Any divergence in activation heights, Equihash N/K, or build lineage causes one group to reject blocks from another.

**Relevant files/functions**

- `src/chainparams.cpp`: upgrade activation heights and protocol versions.
- `src/main.cpp`: version negotiation and peer rejection logic; block contextual checks.
- `src/pow.cpp`: Equihash validation via `librustzcash_eh_isvalid` using consensus N/K.

**How to confirm / falsify**

- Compare `getinfo` / binary versions across pools.
- Inspect `debug.log` for `bad-version`, `version-too-low`, `invalid-solution`, `bad-diffbits`, `bad-prevblk`.
- Verify identical chainparams in deployed binaries (hash / release provenance).

---

## Code areas to inspect first (source map)

### Consensus / params / upgrades

- `src/chainparams.cpp`
- `src/chainparams.h`
- `src/consensus/params.h`
- `src/consensus/upgrades.cpp`

### Validation pipeline

- `src/main.cpp`:
  - `CheckBlockHeader`
  - `ContextualCheckBlockHeader`
  - `CheckBlock`
  - `ContextualCheckBlock`
  - `AcceptBlockHeader`
  - `AcceptBlock`
  - `ProcessNewBlock`
  - `ActivateBestChain`

### PoW / Equihash

- `src/pow.cpp`, `src/pow.h`
- `src/crypto/equihash.cpp`, `src/crypto/equihash.h`

### Checkpoints / chainwork / assume logic

- `src/chainparams.cpp` (checkpoint map + `nMinimumChainWork`)
- `src/checkpoints.cpp`, `src/checkpoints.h`
- `src/main.cpp` (`IsInitialBlockDownload` safeguards)

### Network / peering / protocol gates

- `src/net.cpp`, `src/net.h`
- `src/main.cpp` (version message handling and peer reject paths)
- `src/init.cpp` (peering and startup flags)

### Deprecation / shutdown

- `src/deprecation.h`, `src/deprecation.cpp`
- call sites in `src/main.cpp`

### RPC diagnostics / explorer coupling

- `src/rpc/blockchain.cpp`
- `src/rpc/net.cpp`
- `src/rpc/mining.cpp`
- `src/init.cpp` (`-txindex`, `-insightexplorer`, `-lightwalletd`, ZMQ)

---

## Evidence and reproduction plan

## Output A – Incident report

- **Top-3 likely causes:**
  1. Difficulty overshoot / slow-recovery dead-zone after external high-hash miner.
  2. Network partition due weak bootstrap/peering topology.
  3. Consensus mismatch between node binaries (upgrade/protocol/Equihash params).
- **Most dangerous:** partition + divergent validation rules (can create persistent split).
- **Fast check order:** (a) chain tip consistency across nodes, (b) reject reasons in logs, (c) peer topology, (d) binary/params parity.

## Output B – Technical findings

| Hypothesis | Files / functions | Reproducible? | Evidence level |
|---|---|---:|---|
| Difficulty stall after hash shock | `chainparams.cpp` PoW params; `pow.cpp` retarget; `main.cpp::ContextualCheckBlockHeader` (`bad-diffbits`) | Yes (lab hash shock) | **High** |
| Header/block acceptance divergence | `main.cpp::AcceptBlockHeader`, `CheckBlockHeader`, `CheckBlock`, `ContextualCheckBlock`; `pow.cpp::CheckEquihashSolution` | Yes (mixed params/binaries) | **High** |
| Peering isolation | `chainparams.cpp` seeds; `net.cpp` addrman/bootstrap/eviction/connect | Yes (limited seed graph) | **Medium-High** |
| Deprecation shutdown | `deprecation.*`, `main.cpp` call sites | Yes, but only at deprecation height | **Low** for current incident |
| `nMinimumChainWork` / expected upgrade hash interaction | `chainparams.cpp`, `main.cpp::IsInitialBlockDownload` | Yes (misconfigured release) | **Medium** |
| Time drift / MTP future block rejection | `main.cpp::ContextualCheckBlockHeader` | Yes (clock skew simulation) | **High** |
| Explorer-only failure | `init.cpp` index flags; `main.cpp` index writes; RPC blockchain endpoints | Yes | **High** (as differential diagnosis) |

## Output C – Safe remediation proposals (defensive only)

### Immediate / low risk (Level 1)

1. **Add structured reject telemetry**
   - Count and expose last N block/header rejects by reason (`bad-diffbits`, `time-too-new`, `invalid-solution`, etc.).
   - New RPC: `getchainhealth` summary with tip age, last accepted/rejected header, and top reject reasons.
2. **Chain stall detector**
   - Alert when tip age exceeds X×target spacing and peers advertise higher headers.
   - Expose via RPC + log warnings.
3. **Peer health RPC extensions**
   - For each peer: best known header height/hash, synced tip, fork point distance, last block announcement time.
4. **Bootstrap hardening**
   - Maintain multiple independent DNS seeds; keep fixed seeds updated per release.
5. **Operator sanity checks**
   - Startup warning if peers are too few, if local clock offset is large, or if local binary epoch is near/behind network upgrade epochs.

### Medium term / medium risk (Level 2)

1. **Difficulty algorithm audit + simulation suite**
   - Add deterministic tests for hash-shock scenarios and timestamp games.
   - If weaknesses confirmed, patch adjustment behavior with careful compatibility analysis.
2. **Validation observability**
   - Add debug category for full retarget trace and MTP decision path per header.
3. **Reorg / partition diagnostics**
   - Expose longest competing tips and cumulative work delta via RPC.

### Protocol-level / high risk (Level 3)

1. **Consensus parameter changes (if absolutely required)**
   - Any retarget formula change is consensus-critical (likely hard fork unless exactly backward-compatible in all historic contexts).
2. **Emergency checkpoint mechanism**
   - Only as manual, transparent, documented emergency procedure; never automatic hidden override.

**Explicit non-goals (forbidden):** no miner/IP/address blacklist in consensus, no hidden chain override, no centralized backdoor.

## Output D – Patch plan

### Phase 1: Diagnostics
- **Goal:** Make stalls observable in minutes, not days.
- **Files:** `src/rpc/blockchain.cpp`, `src/rpc/net.cpp`, `src/main.cpp`, `src/init.cpp`.
- **Changes:** new health RPC(s), reject counters, stall warnings.
- **Risk:** low.
- **Rollback:** feature flags / disable RPC additions.

### Phase 2: Reproduction
- **Goal:** Recreate “all stopped” patterns in lab.
- **Files:** `qa/rpc-tests/*` + helper scripts.
- **Changes:** multi-node tests for hash shock, partition, clock skew, mixed activation params.
- **Risk:** low.
- **Rollback:** test-only, no consensus effect.

### Phase 3: Fix
- **Goal:** Remove confirmed root cause(s).
- **Files:** likely `src/pow.cpp`, `src/main.cpp`, maybe `src/net.cpp`, `src/chainparams.cpp`.
- **Changes:** targeted fixes after evidence.
- **Risk:** medium to high (especially PoW/consensus changes).
- **Rollback:** release gating, canary deploy, revert tags.

### Phase 4: Testing
- **Goal:** Ensure no split regressions.
- **Files:** unit + RPC + long-run integration tests.
- **Changes:** adversarial scenarios; split-brain detection checks.
- **Risk:** low.
- **Rollback:** keep previous release artifacts and bootstrap configs.

### Phase 5: Rollout
- **Goal:** coordinated operator upgrade.
- **Files:** release notes, upgrade guides, seed infrastructure.
- **Changes:** staged rollout with pool/explorer operators first.
- **Risk:** operational medium.
- **Rollback:** freeze rollout, revert to prior stable binaries (if consensus-compatible).

---

## Mandatory pattern check results

- **deprecation height / shutdown:** present (`deprecation.*`), but current constant is far in the future for this build; still needs monitoring.
- **checkpoint map:** present in `chainparams.cpp`.
- **minimum chain work:** present (`consensus.nMinimumChainWork`) and used in `IsInitialBlockDownload`.
- **assume valid:** no explicit `defaultAssumeValid` pattern found in this fork snapshot.
- **powLimit / retarget interval fields:** present (`powLimit`, averaging window, max adjust up/down).
- **target spacing / timespan:** derived in consensus params and used by PoW calculation.
- **Equihash parameters:** present (`nEquihashN`, `nEquihashK`) and validated in `pow.cpp`.
- **future block / adjusted time / MTP:** enforced in `ContextualCheckBlockHeader` (`time-too-old`, `time-too-new`, `time-too-far-ahead-of-mtp`).
- **ban score / misbehavior:** present (`Misbehaving`, ban map in net).
- **peer eviction:** present (`AttemptToEvictConnection`).
- **seed nodes:** DNS seed thread + fixed seed fallback present.
- **protocol version gates:** present in version handshake and epoch checks.
- **activation heights:** present for Overwinter/Sapling/Blossom/Heartwood/Canopy.
- **reorg handling:** present via `FindMostWorkChain`/`ActivateBestChain`.
- **block index consistency assertions:** present in load/check paths.
- **IBD edge checks:** present; includes minimum chain work and upgrade hash safety checks.

---

## “Everything stopped” focused interpretation (3–5 explanations)

1. **Hash-shock difficulty trap**: external high hashrate appears, leaves, network difficulty remains too high; pools appear frozen.
2. **Invalid chain attempted by external miner**: miner-produced blocks fail full validation (`invalid-solution`, `bad-diffbits`, time rules), so explorers never show them.
3. **Multi-island network partition**: pools each see insufficient peers to converge, explorers attached to one island appear stale.
4. **Activation/protocol mismatch among operators**: some nodes reject peer blocks during/after upgrade gating.
5. **Clock drift on key mining nodes**: future/old-time rejection blocks propagation and acceptance.

---

## Recommended changes

### Minimal safe patch set (suggested first release)

1. Add `getchainhealth` RPC (tip age, peers-at-same-tip ratio, reject summary, stall flag).
2. Add ring-buffered reject reason counters and per-peer fork-gap metrics.
3. Add explicit log line on every header reject with peer id + reason + height/hash.
4. Add startup warnings for low peer diversity and large local clock skew.
5. Add operator docs: mandatory triage command bundle.

### Long-term hardening plan

1. Continuous seed maintenance + independent DNS seed operators.
2. Regression harness for difficulty and timestamp adversarial scenarios.
3. Release process checklists: activation height consistency, minimum chainwork updates, binary fingerprint parity for pools/explorers/wallet backends.
4. Optional protocol improvement (only after simulation and ecosystem coordination) if difficulty behavior is proven root cause.

---

## Explorer vs node vs consensus separation

- **Consensus-level causes:** retarget logic, header/block validation mismatches, activation mismatch, time-rule rejects.
- **Node operational causes:** poor peering/bootstrap, stale seeds, too few outbound links, mixed binary deployments.
- **Explorer/index symptoms:** txindex/addressindex/spentindex/timestampindex disabled or stale; explorer lag can be purely downstream even when chain is healthy.

