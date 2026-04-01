# BUCK mainnet emergency min-difficulty upgrade note

## Summary

This change introduces a **consensus rule change** to improve mainnet liveness during extreme block production stalls.

At and after activation height **2,757,000**, BUCK mainnet applies an emergency fallback in difficulty selection:

- if `next_block_time > prev_block_time + 12 * targetSpacing`
- then `GetNextWorkRequired` may return `powLimit` (minimum difficulty).

All other cases continue to use the existing averaging-window retarget logic unchanged.

## Activation and rollout

- **Activation height:** `2,757,000`
- **Delay multiplier (k):** `12`
- **Network scope:** mainnet only

Because this is a consensus change, a coordinated rollout is required.
Nodes that do not upgrade before activation can follow incompatible chains and cause/experience a chain split.

## Why this is minimal

- Rule is only active after a fixed activation height.
- Rule only triggers under extreme delay conditions.
- No admin override is added.
- No runtime switch is added.
- No full difficulty algorithm replacement is included in this patch.

## If activation height must be changed

The activation constants are defined in `src/chainparams.cpp` as:

- `MAINNET_EMERGENCY_MIN_DIFFICULTY_ACTIVATION_HEIGHT`
- `MAINNET_EMERGENCY_MIN_DIFFICULTY_DELAY_MULTIPLIER`

To modify activation for a future coordinated release, update those constants and ship a new coordinated binary release before the target activation height.
