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

#include "ui_uaisosettings.h"
#include "uaisosettings.h"
#include "uaisoeditor.h"

#include <coreplugin/icore.h>

#include <QPointer>

    /* Uaiso - https://github.com/ltcmelo/uaiso
     *
     * Notice: This implementation has the only purpose of showcasing a few
     * components of the Uaiso project. It's not intended to be efficient,
     * nor to be taken as reference on how to write Qt Creator editors. It's
     * not even complete. The primary concern is to demonstrate Uaiso's API.
     */

using namespace UaisoQtc;
using namespace TextEditor;
using namespace uaiso;

struct UaisoSettingsPage::UaisoSettingsPagePrivate
{
    explicit UaisoSettingsPagePrivate()
        : m_displayName(tr("Uaiso Source Analyser"))
        , m_settingsPrefix(QLatin1String("Uaiso"))
        , m_prevIndex(-1)
        , m_page(0)
    {}

    const Core::Id m_id;
    const QString m_displayName;
    const QString m_settingsPrefix;
    int m_prevIndex;

    UaisoSettings m_settings;

    QPointer<QWidget> m_widget;
    Ui::UaisoSettingsPage *m_page;
};

UaisoSettingsPage::UaisoSettingsPage(QObject *parent)
    : Core::IOptionsPage(parent)
    , m_d(new UaisoSettingsPagePrivate)
{
    setId(Constants::SETTINGS_ID);
    setDisplayName(m_d->m_displayName);
    setCategory(Constants::SETTINGS_CATEGORY);
    setDisplayCategory(QCoreApplication::translate("Uaiso", Constants::SETTINGS_TR_CATEGORY));
    //setCategoryIcon(QLatin1String(Constants::SETTINGS_CATEGORY_ICON));

}

UaisoSettingsPage::~UaisoSettingsPage()
{
    delete m_d;
}

QWidget *UaisoSettingsPage::widget()
{
    if (!m_d->m_widget) {
        m_d->m_widget = new QWidget;
        m_d->m_page = new Ui::UaisoSettingsPage;
        m_d->m_page->setupUi(m_d->m_widget);
        m_d->m_page->langCombo->setCurrentIndex(0);

        for (auto lang : supportedLangs()) {
            m_d->m_page->langCombo->addItem(QString::fromStdString(langName(lang)),
                                            QVariant::fromValue(static_cast<int>(lang)));
        }

        m_d->m_settings.load(Core::ICore::settings());

        m_d->m_prevIndex = -1; // Reset previous index.
        connect(m_d->m_page->langCombo, SIGNAL(currentIndexChanged(int)),
                this, SLOT(displayOptionsForLang(int)));

        settingsToUI();
    }

    return m_d->m_widget;
}

void UaisoSettingsPage::apply()
{
    if (!m_d->m_page)
        return;

    settingsFromUI();
    m_d->m_settings.store(Core::ICore::settings());
}

void UaisoSettingsPage::finish()
{
    delete m_d->m_widget;
    if (!m_d->m_page)
        return;

    delete m_d->m_page;
    m_d->m_page = 0;
}

void UaisoSettingsPage::displayOptionsForLang(int index)
{
    if (m_d->m_prevIndex != -1)
        updateOptionsOfLang(m_d->m_prevIndex);

    UaisoSettings::LangOptions &options = m_d->m_settings.m_options[index];
    m_d->m_page->enabledCheck->setChecked(options.m_enabled);
    m_d->m_page->interpreterEdit->setText(options.m_interpreter);
    m_d->m_page->sysPathEdit->setText(options.m_systemPaths);
    m_d->m_page->extraPathEdit->setText(options.m_extraPaths);

    m_d->m_prevIndex = index;
}

void UaisoSettingsPage::updateOptionsOfLang(int index)
{
    UaisoSettings::LangOptions &options = m_d->m_settings.m_options[index];
    options.m_enabled = m_d->m_page->enabledCheck->isChecked();
    options.m_interpreter = m_d->m_page->interpreterEdit->text();
    options.m_systemPaths = m_d->m_page->sysPathEdit->text();
    options.m_extraPaths = m_d->m_page->extraPathEdit->text();
}

void UaisoSettingsPage::settingsFromUI()
{
    updateOptionsOfLang(m_d->m_page->langCombo->currentIndex());
}

void UaisoSettingsPage::settingsToUI()
{
    displayOptionsForLang(m_d->m_page->langCombo->currentIndex());
}

namespace {

const QLatin1String kUaiso("UaisoSettings");
const QLatin1String kEnabled("Enabled");
const QLatin1String kInterpreter("Interpreter");
const QLatin1String kSystemPaths("SystemPaths");
const QLatin1String kExtraPaths("ExtraPaths");

} // anonymous

void UaisoSettings::store(QSettings *settings,
                                  const LangOptions &options,
                                  const QString& group) const
{
    settings->beginGroup(group);
    settings->setValue(kEnabled, options.m_enabled);
    settings->setValue(kInterpreter, options.m_interpreter);
    settings->setValue(kSystemPaths, options.m_systemPaths);
    settings->setValue(kExtraPaths, options.m_extraPaths);
    settings->endGroup();
}

void UaisoSettings::store(QSettings *settings) const
{
    for (auto const& option : m_options) {
        store(settings,
                      option.second,
                      kUaiso + QString::fromStdString(langName(LangName(option.first))));
    }
}

void UaisoSettings::load(QSettings *settings,
                                 LangOptions &options,
                                 const QString& group)
{
    settings->beginGroup(group);
    options.m_enabled = settings->value(kEnabled).toBool();
    options.m_interpreter = settings->value(kInterpreter).toString();
    options.m_systemPaths = settings->value(kSystemPaths).toString();
    options.m_extraPaths = settings->value(kExtraPaths).toString();
    settings->endGroup();
}

void UaisoSettings::load(QSettings *settings)
{
    for (auto lang : supportedLangs()) {
        auto& option = m_options[static_cast<int>(lang)];
        load(settings,
                     option,
                     kUaiso + QString::fromStdString(langName(lang)));
    }
}
