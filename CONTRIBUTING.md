# Contributing to KTasks

Thank you for your interest in contributing to **KTasks**!

## Governance & Maintainer Approval

KTasks is currently maintained by a sole developer (**@MarleyS439**). To keep the codebase secure, lightweight, and reliable:

- Direct pushes to `main` are blocked for everyone.
- All changes must arrive via **Pull Requests**.
- **@MarleyS439** reviews and must approve every Pull Request (enforced via `.github/CODEOWNERS` and branch protection) before it can be merged.
- Releases, distribution packages, and tags are published exclusively by the maintainer.

## Before Opening a Pull Request

1. Open an issue first to discuss new features or major UI changes.
2. Keep changes focused and modular, respecting KDE Frameworks / Kirigami conventions and C++20 style.
3. Add or update unit tests in `tests/` for any core or manager logic changes.
4. Run the full validation suite locally before pushing:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
appstreamcli validate --pedantic --no-net data/io.github.marleys439.ktasks.metainfo.xml
desktop-file-validate data/io.github.marleys439.ktasks.desktop
```

5. Never include personal data, credentials, email addresses, private Nextcloud URLs, or local databases in commits or tests.
6. Ensure every source file includes a valid `SPDX-License-Identifier: GPL-3.0-or-later` header.
