// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.h"

#include <KConfigGroup>

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig())
{
}

QVariant Settings::read(const QString &group, const QString &key, const QVariant &def) const
{
    return m_config->group(group).readEntry(key, def);
}

void Settings::write(const QString &group, const QString &key, const QVariant &value)
{
    m_config->group(group).writeEntry(key, value);
    m_config->sync();
}

int Settings::sortMode(const QString &view) const
{
    return m_config->group(QStringLiteral("Sort")).readEntry(view, -1);
}

void Settings::setSortMode(const QString &view, int mode)
{
    m_config->group(QStringLiteral("Sort")).writeEntry(view, mode);
    m_config->sync();
}
