/******************************************************************************
 * Copyright (c) 2014-2015 Leandro T. C. Melo (ltcmelo@gmail.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 * USA
 *****************************************************************************/

#ifndef UAISO_QTC_SETTINGS_H
#define UAISO_QTC_SETTINGS_H

#include <coreplugin/dialogs/ioptionspage.h>

#include <unordered_map>

    /* Uaiso - https://github.com/ltcmelo/uaiso
     *
     * Notice: This implementation has the only purpose of showcasing a few
     * components of the Uaiso project. It's not intended to be efficient,
     * nor to be taken as reference on how to write Qt Creator editors. It's
     * not even complete. The primary concern is to demonstrate Uaiso's API.
     */

#include <Parsing/LangName.h>

QT_BEGIN_NAMESPACE
class QSettings;
QT_END_NAMESPACE

namespace UaisoQtc {

struct UaisoSettings
{
    struct LangOptions
    {
        bool m_enabled;
        QString m_interpreter;
        QString m_systemPaths;
        QString m_extraPaths;
    };

    std::unordered_map<int, LangOptions> m_options;

    void store(QSettings *s) const;
    void store(QSettings *s, const LangOptions& options, const QString& group) const;
    void load(QSettings *s);
    void load(QSettings *s, LangOptions& options, const QString& group);
};

class UaisoSettingsPage : public Core::IOptionsPage
{
    Q_OBJECT

public:
    UaisoSettingsPage(QObject *parent = 0);
    ~UaisoSettingsPage();

    QWidget *widget();
    void apply();
    void finish();

private slots:
    void displayOptionsForLang(int);

private:
    void updateOptionsOfLang(int);

    void settingsFromUI();
    void settingsToUI();

    struct UaisoSettingsPagePrivate;
    UaisoSettingsPagePrivate *m_d;
};

} // namespace UaisoQtc

#endif
