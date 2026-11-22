// SPDX-License-Identifier: GPL-3.0-or-later
#include "crypto.h"

#include <QByteArray>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace KTasks::Crypto
{

static constexpr int kSaltBytes = 16;
static constexpr int kKeyBytes = 32;
static constexpr int kIvBytes = 12;
static constexpr int kTagBytes = 16;
static constexpr int kPbkdf2Iterations = 210000;

static bool deriveSubkeys(const QString &password, const QByteArray &salt, QByteArray &encKey, QByteArray &authHash)
{
    if (password.isEmpty() || salt.size() != kSaltBytes) {
        return false;
    }
    const QByteArray passBytes = password.toUtf8();
    QByteArray master(kKeyBytes * 2, Qt::Uninitialized);
    if (PKCS5_PBKDF2_HMAC(passBytes.constData(),
                          int(passBytes.size()),
                          reinterpret_cast<const unsigned char *>(salt.constData()),
                          int(salt.size()),
                          kPbkdf2Iterations,
                          EVP_sha256(),
                          int(master.size()),
                          reinterpret_cast<unsigned char *>(master.data()))
        != 1) {
        OPENSSL_cleanse(master.data(), size_t(master.size()));
        return false;
    }
    encKey = master.left(kKeyBytes);
    authHash = master.mid(kKeyBytes, kKeyBytes);
    OPENSSL_cleanse(master.data(), size_t(master.size()));
    return true;
}

bool hashPassword(const QString &password, QString &saltOut, QString &hashOut)
{
    QByteArray salt(kSaltBytes, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(salt.data()), kSaltBytes) != 1) {
        return false;
    }
    QByteArray encKey, authHash;
    if (!deriveSubkeys(password, salt, encKey, authHash)) {
        return false;
    }
    OPENSSL_cleanse(encKey.data(), size_t(encKey.size()));
    saltOut = QString::fromLatin1(salt.toBase64());
    hashOut = QString::fromLatin1(authHash.toBase64());
    return true;
}

bool verifyPassword(const QString &password, const QString &saltB64, const QString &hashB64)
{
    const QByteArray salt = QByteArray::fromBase64(saltB64.toLatin1());
    const QByteArray expected = QByteArray::fromBase64(hashB64.toLatin1());
    if (salt.size() != kSaltBytes || expected.size() != kKeyBytes) {
        return false;
    }
    QByteArray encKey, authHash;
    if (!deriveSubkeys(password, salt, encKey, authHash)) {
        return false;
    }
    OPENSSL_cleanse(encKey.data(), size_t(encKey.size()));
    const bool ok = authHash.size() == expected.size() && CRYPTO_memcmp(authHash.constData(), expected.constData(), size_t(expected.size())) == 0;
    OPENSSL_cleanse(authHash.data(), size_t(authHash.size()));
    return ok;
}

bool encryptText(const QString &plaintext, const QString &password, const QString &saltB64, QString &cipherOutB64)
{
    const QByteArray salt = QByteArray::fromBase64(saltB64.toLatin1());
    QByteArray encKey, authHash;
    if (!deriveSubkeys(password, salt, encKey, authHash)) {
        return false;
    }
    OPENSSL_cleanse(authHash.data(), size_t(authHash.size()));

    QByteArray iv(kIvBytes, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(iv.data()), kIvBytes) != 1) {
        OPENSSL_cleanse(encKey.data(), size_t(encKey.size()));
        return false;
    }

    const QByteArray plain = plaintext.toUtf8();
    QByteArray cipher(plain.size() + 16, Qt::Uninitialized);
    QByteArray tag(kTagBytes, Qt::Uninitialized);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    bool ok = ctx != nullptr;
    int len = 0;
    int total = 0;
    if (ok) {
        ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
            && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIvBytes, nullptr) == 1
            && EVP_EncryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast<const unsigned char *>(encKey.constData()),
                                  reinterpret_cast<const unsigned char *>(iv.constData()))
                == 1;
    }
    if (ok && !plain.isEmpty()) {
        ok = EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(cipher.data()), &len, reinterpret_cast<const unsigned char *>(plain.constData()), int(plain.size())) == 1;
        total = len;
    }
    if (ok) {
        ok = EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(cipher.data()) + total, &len) == 1;
        total += len;
    }
    if (ok) {
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagBytes, tag.data()) == 1;
    }
    if (ctx) {
        EVP_CIPHER_CTX_free(ctx);
    }
    OPENSSL_cleanse(encKey.data(), size_t(encKey.size()));
    if (!ok) {
        return false;
    }
    cipher.truncate(total);
    QByteArray blob;
    blob.reserve(kIvBytes + cipher.size() + kTagBytes);
    blob.append(iv);
    blob.append(cipher);
    blob.append(tag);
    cipherOutB64 = QString::fromLatin1(blob.toBase64());
    return true;
}

bool decryptText(const QString &cipherB64, const QString &password, const QString &saltB64, QString &plainOut)
{
    const QByteArray salt = QByteArray::fromBase64(saltB64.toLatin1());
    const QByteArray blob = QByteArray::fromBase64(cipherB64.toLatin1());
    if (blob.size() < kIvBytes + kTagBytes) {
        return false;
    }
    QByteArray encKey, authHash;
    if (!deriveSubkeys(password, salt, encKey, authHash)) {
        return false;
    }
    OPENSSL_cleanse(authHash.data(), size_t(authHash.size()));

    const QByteArray iv = blob.left(kIvBytes);
    const QByteArray tag = blob.right(kTagBytes);
    const QByteArray cipher = blob.mid(kIvBytes, blob.size() - kIvBytes - kTagBytes);
    QByteArray plain(cipher.size() + 16, Qt::Uninitialized);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    bool ok = ctx != nullptr;
    int len = 0;
    int total = 0;
    if (ok) {
        ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
            && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIvBytes, nullptr) == 1
            && EVP_DecryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast<const unsigned char *>(encKey.constData()),
                                  reinterpret_cast<const unsigned char *>(iv.constData()))
                == 1;
    }
    if (ok && !cipher.isEmpty()) {
        ok = EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(plain.data()), &len, reinterpret_cast<const unsigned char *>(cipher.constData()), int(cipher.size())) == 1;
        total = len;
    }
    if (ok) {
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagBytes, const_cast<char *>(tag.constData())) == 1;
    }
    if (ok) {
        ok = EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(plain.data()) + total, &len) == 1;
        total += len;
    }
    if (ctx) {
        EVP_CIPHER_CTX_free(ctx);
    }
    OPENSSL_cleanse(encKey.data(), size_t(encKey.size()));
    if (!ok) {
        return false;
    }
    plain.truncate(total);
    plainOut = QString::fromUtf8(plain);
    OPENSSL_cleanse(plain.data(), size_t(plain.size()));
    return true;
}

bool seal(const QString &plaintext, const QString &password, SealedData &out)
{
    if (!hashPassword(password, out.salt, out.passwordHash)) {
        return false;
    }
    return encryptText(plaintext, password, out.salt, out.ciphertext);
}

}
