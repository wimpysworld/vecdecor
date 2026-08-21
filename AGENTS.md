# Vecdecor

## Branch synchronisation

- Keep `main` compatible with Wayfire 0.11 or later.
- Keep `wayfire-0.10` compatible with Wayfire 0.10 and use it to support Felkor.
- Develop initial shared features on `wayfire-0.10`.
- Make each shared change a complete logical commit on `wayfire-0.10`.
- Transfer each shared commit from `wayfire-0.10` to `main` with `git cherry-pick -x <commit>`.
- Put each Wayfire 0.11 API adaptation in a separate `fix(wayfire): ...` commit on `main`.
- Never merge `main` into `wayfire-0.10` or `wayfire-0.10` into `main`.
- Transfer shared assets, metadata, tests, and documentation only as complete logical commits by cherry-pick.
- Adapt rendering differences explicitly on the destination branch and add branch-specific tests.
- Build and test `main` against Wayfire 0.11.0 as its initial target.
- Build and test `wayfire-0.10` against Wayfire 0.10.1.
- When Felkor moves to Wayfire 0.11, develop shared features on `main`.
- After that move, keep `wayfire-0.10` maintenance-only and cherry-pick only critical fixes from `main` with `git cherry-pick -x <commit>`.

## Prior art

- Study [Windecor](https://gitlab.com/wayfireplugins/windecor) for typed layout, input regions, and decoration state.
- Study [Firedecor](https://github.com/mntmn/Firedecor) and [Cosmodecor](https://github.com/CosmicFusion/cosmodecor) for scene nodes, geometry, vector rendering, and caching.
- Reimplement useful ideas within Vecdecor's retained Pixdecor history.
- Do not copy code, assets, metadata, or documentation from Firedecor or Cosmodecor because neither project has clear retained licence provenance.
