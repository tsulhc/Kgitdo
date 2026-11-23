#include "SavedFilterStore.h"

#include <QSettings>
#include <QString>
#include <memory>

SavedFilterStore::SavedFilterStore(const QString &settingsFile)
    : m_settingsFile(settingsFile)
{
}

QVariantList SavedFilterStore::load(const QString &login) const
{
    if (login.isEmpty()) {
        return {};
    }
    std::unique_ptr<QSettings> settings = m_settingsFile.isEmpty()
        ? std::make_unique<QSettings>()
        : std::make_unique<QSettings>(m_settingsFile, QSettings::IniFormat);
    settings->beginGroup(QStringLiteral("SavedFilters"));
    settings->beginGroup(QString::fromLatin1(login.toUtf8().toHex()));
    const QVariantList filters = settings->value(QStringLiteral("items")).toList();
    settings->endGroup();
    settings->endGroup();
    return filters;
}

void SavedFilterStore::save(const QString &login, const QVariantList &filters)
{
    if (login.isEmpty()) {
        return;
    }
    std::unique_ptr<QSettings> settings = m_settingsFile.isEmpty()
        ? std::make_unique<QSettings>()
        : std::make_unique<QSettings>(m_settingsFile, QSettings::IniFormat);
    settings->beginGroup(QStringLiteral("SavedFilters"));
    settings->beginGroup(QString::fromLatin1(login.toUtf8().toHex()));
    settings->setValue(QStringLiteral("items"), filters);
    settings->endGroup();
    settings->endGroup();
    settings->sync();
}
