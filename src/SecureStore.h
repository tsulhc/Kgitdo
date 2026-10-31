#pragma once

#include <QObject>
#include <QString>

// Tokens as persisted by the secure store. Only the store implementation
// may touch these values on disk; AuthSession keeps the live copies.
struct StoredTokens {
    QString accessToken;
    QString refreshToken;
    qint64 accessExpiresAtSecsSinceEpoch = 0; // 0 = never expires
    QString login;

    bool hasTokens() const { return !accessToken.isEmpty(); }
    void clear()
    {
        accessToken.clear();
        refreshToken.clear();
        accessExpiresAtSecsSinceEpoch = 0;
        login.clear();
    }
};

// Minimal persistence contract so QtTest can inject fakes and the app can
// fall back to a volatile in-memory session when KWallet is unavailable.
// Implementations must never write tokens to plain files, logs, or QSettings.
// (No Q_OBJECT: no signals are needed; plain QObject parenting suffices.)
class ISecureStore : public QObject
{
public:
    explicit ISecureStore(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    virtual bool isAvailable() const = 0;
    virtual QString backendName() const = 0;
    virtual bool load(StoredTokens &out) = 0;
    virtual bool save(const StoredTokens &tokens) = 0;
    virtual bool clear() = 0;
};

// Volatile fallback: keeps tokens only for the process lifetime.
// Used when the wallet is absent/refused and in unit tests.
class InMemorySecureStore : public ISecureStore
{
public:
    explicit InMemorySecureStore(QObject *parent = nullptr)
        : ISecureStore(parent)
    {
    }

    bool isAvailable() const override { return true; }
    QString backendName() const override { return QStringLiteral("memory"); }

    bool load(StoredTokens &out) override
    {
        if (!m_hasValue) {
            return false;
        }
        out = m_value;
        return true;
    }

    bool save(const StoredTokens &tokens) override
    {
        m_value = tokens;
        m_hasValue = true;
        return true;
    }

    bool clear() override
    {
        m_value.clear();
        m_hasValue = false;
        return true;
    }

private:
    StoredTokens m_value;
    bool m_hasValue = false;
};

// Test double simulating a refused/absent wallet.
class FailingSecureStore : public ISecureStore
{
public:
    explicit FailingSecureStore(QObject *parent = nullptr)
        : ISecureStore(parent)
    {
    }

    bool isAvailable() const override { return false; }
    QString backendName() const override { return QStringLiteral("unavailable"); }
    bool load(StoredTokens &) override { return false; }
    bool save(const StoredTokens &) override { return false; }
    // Clearing local state always succeeds: there is nothing persisted.
    bool clear() override { return true; }
};
