# 0001: Explicit development configuration

Accepted 2026-10-12. The owner authorized a separate fixture build until the
production endpoint and public key are available. Production CMake configuration
fails without explicit HTTPS URL, key ID and 32-byte Ed25519 public key. Private
keys are never configuration inputs to the launcher. Fixture mode is opt-in and
visibly labeled. This is not permission to weaken production verification.

Dependencies are source archives pinned to full commits and SHA-256 digests.
The SDL revision is explicitly supplied because an archive nested under a new,
uncommitted Git repository otherwise discovers that parent's unborn HEAD.

M1 artwork is original compile-time raster data, as permitted for the placeholder
hero. The bundled ImGui font provides initial Latin glyph coverage. Additional
locale coverage is not promised by this initial shell.

Native title dragging uses SDL_SetWindowHitTest. If the backend rejects hit
testing, the application tells the user to use the compositor/window-manager
move command. It never sets Wayland top-level coordinates to emulate dragging.
