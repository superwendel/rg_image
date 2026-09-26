# Changelog

## 0.1.0

Initial release contents for RGI, a lossless RGBA8 image format and
single-header C99 codec for game assets.

- One `rgif` stream with three payload profiles: pixel operations, extended
  runs/copies/literal spans, and exact packed palettes. Palette selection keeps
  the smaller complete stream and never quantizes colors or alpha.
- Checked decoding, scalar/SIMD paths, caller-owned encode workspace, optional
  allocator hooks, and C++ inclusion. Runtime decoding produces ordinary RGBA8.
- PNG/RGI/QOI converter, SDL3 viewer, and optional Windows thumbnail provider.
  The viewer is tested through its texture-loading path for all three profiles.
- Reproducible QOI/PNG CPU and GPU benchmark tooling, public aggregate evidence,
  introductory article, normative format documentation, and a printable spec.

Existing profile-0/1 files remain readable. Older readers need an update before
loading profile 2. Define `RG_RGI_NO_PALETTE_ENCODE` when producing assets for
older readers; this option does not disable decoding any profile.

Release gate: Windows and Linux CI must both pass on the exact commit before
the `v0.1.0` tag is created. This file does not assert that a release is published.
