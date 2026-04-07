BUCK v4.0.5
============

This is a stabilization release focused on restoring reliable node and mining
operation under adverse network-sync conditions.

Notable changes
---------------

- Build and packaging reliability:
  - Restored executable permissions on build helper scripts used in Linux and
    cross-build workflows.
- Node and mining stabilization:
  - Added targeted `getblocktemplate` behavior to allow template serving on a
    stale but fully-synced tip, reducing deadlock scenarios during IBD-related
    stalls.
  - Included the mainnet emergency min-difficulty liveness fallback to help
    recover block production during prolonged chain-stall conditions.
- Operational recovery:
  - Improved pool/node compatibility and mining recovery in situations where
    block production had previously stalled.

This release contains focused stability fixes and avoids unrelated refactors.
