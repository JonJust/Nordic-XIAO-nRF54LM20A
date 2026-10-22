# Seeed board definitions

`platform-seeedboards/` is an unmodified full source archive of the official
[Seeed repository](https://github.com/Seeed-Studio/platform-seeedboards), pinned to
commit `a6fd41debbbcda13b5340fff50c9092ed3289194` (platform release 1.1.1).

- Archive: https://codeload.github.com/Seeed-Studio/platform-seeedboards/tar.gz/a6fd41debbbcda13b5340fff50c9092ed3289194
- Archive SHA-256: `7b07f6bcb64adcf98f867985c4c0aea9ae196014e98795a623576a56e9db2a72`
- Board root: `platform-seeedboards/zephyr/nrf`
- License: retained in the dependency's `LICENSE` and individual source files.

This source is used as an external Zephyr board root, without installing
PlatformIO or changing the installed Nordic SDK. The downloaded directory is
ignored by this project's Git settings. To recreate it with Git instead:

```bash
cd /home/skipper/repos/seeed_studio_nRF54LM20A
git clone https://github.com/Seeed-Studio/platform-seeedboards.git third_party/platform-seeedboards
git -C third_party/platform-seeedboards checkout a6fd41debbbcda13b5340fff50c9092ed3289194
```

These restoration commands assume the destination is absent. The existing local
copy was downloaded from the archive and has no nested Git repository.
