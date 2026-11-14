# Contributing to ORBIT

Thanks for helping! ORBIT is a small homebrew project, so clear and focused contributions help most.

## Reporting bugs

Use the [bug report form](https://github.com/C2V4ZA/ORBIT/issues/new/choose). Please include:

- ORBIT version, CFW or HEN (and its version)
- What you did, what you expected, what happened
- A photo of the screen if possible; for voice problems the *Microphone data* line from Settings › Audio › Test microphone

Security problems: please follow [SECURITY.md](SECURITY.md) instead of opening a public issue.

## Suggesting features

Use the [feature request form](https://github.com/C2V4ZA/ORBIT/issues/new/choose). Check the [roadmap](docs/ROADMAP.md) first.

## Code contributions

1. Fork, create a branch, keep changes focused.
2. Build and run the desktop tests (`make test`, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#building)).
3. Changes that affect the PS3 build should be tested on real hardware if you can – say in the pull request whether you did.
4. Keep the style of the surrounding code (C11, 4 spaces, comments in English).
5. New interface text goes into `include/i18n_keys_a.h` / `i18n_keys_b.h` with all four languages. Never translate user content.
6. Don't add dependencies on proprietary SDKs.

## Translations

Interface texts live in `include/i18n_keys_a.h` and `include/i18n_keys_b.h` (one column per language). Improvements by native speakers are very welcome.

## License

By contributing you agree that your contribution is licensed under the [GPL-3.0-or-later](LICENSE).

Please follow our [Code of Conduct](CODE_OF_CONDUCT.md).
