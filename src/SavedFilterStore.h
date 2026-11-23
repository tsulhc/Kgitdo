#pragma once

#include <QVariantList>
#include <QString>

// Stores non-secret, account-scoped saved client-side filters.
class SavedFilterStore
{
public:
    explicit SavedFilterStore(const QString &settingsFile = QString());

    QVariantList load(const QString &login) const;
    void save(const QString &login, const QVariantList &filters);

private:
    QString m_settingsFile;
};
