# Security Policy

## Reporting a Vulnerability

If you discover a security issue in KTasks (such as credential storage, journal encryption, CalDAV/Nextcloud sync, or calendar parsing):

- **Do not** open a public issue with exploit details.
- Report it privately using GitHub **Private Vulnerability Reporting** (Security Advisories) on the `MarleyS439/ktasks` repository.

## Design Guarantees

- **Offline-first & local encryption:** Password-protected Journal pages use per-entry **AES-256-GCM** encryption locally and are never uploaded to Nextcloud Notes while locked.
- **Credential storage:** Nextcloud / CalDAV passwords are stored in the system keyring (**KWallet / Secret Service** via QtKeychain), never in plaintext config files or logs.
- **Passive desktop calendar access:** External calendar reading from KOrganizer/Akonadi is strictly read-only.
