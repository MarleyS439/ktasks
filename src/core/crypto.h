// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace KTasks::Crypto
{

struct SealedData {
    QString salt; // base64
    QString passwordHash; // base64
    QString ciphertext; // base64 (iv + ciphertext + tag)
};

/** Generates a random base64 salt and PBKDF2-HMAC-SHA256 verification hash for @p password. */
bool hashPassword(const QString &password, QString &saltOut, QString &hashOut);

/** Constant-time verification of @p password against stored @p saltB64 and @p hashB64. */
bool verifyPassword(const QString &password, const QString &saltB64, const QString &hashB64);

/** Encrypts @p plaintext using a key derived from @p password and @p saltB64 (AES-256-GCM). */
bool encryptText(const QString &plaintext, const QString &password, const QString &saltB64, QString &cipherOutB64);

/** Decrypts @p cipherB64 using @p password and @p saltB64. Returns false if authentication fails. */
bool decryptText(const QString &cipherB64, const QString &password, const QString &saltB64, QString &plainOut);

/** Convenience: creates salt, hash, and encrypted payload in one call. */
bool seal(const QString &plaintext, const QString &password, SealedData &out);

}
