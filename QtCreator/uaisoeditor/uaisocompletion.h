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

#ifndef UAISO_QTC_COMPLETION_H
#define UAISO_QTC_COMPLETION_H

#include "uaisoeditor.h"

#include <texteditor/codeassist/completionassistprovider.h>
#include <texteditor/codeassist/iassistprocessor.h>
#include <texteditor/codeassist/assistinterface.h>

namespace uaiso {
class Lang;
}

class UaisoAssistProvider : public TextEditor::CompletionAssistProvider
{
    Q_OBJECT

public:
    UaisoAssistProvider(uaiso::Factory* factory);
    ~UaisoAssistProvider();

    bool supportsEditor(Core::Id editorId) const Q_DECL_OVERRIDE;
    TextEditor::IAssistProcessor *createProcessor() const Q_DECL_OVERRIDE;
    int activationCharSequenceLength() const Q_DECL_OVERRIDE;
    bool isActivationCharSequence(const QString &sequence) const Q_DECL_OVERRIDE;

    std::unique_ptr<uaiso::Lang> m_lang;
};

class UaisoAssistInterface;

class UaisoAssistProcessor : public TextEditor::IAssistProcessor
{
public:
    TextEditor::IAssistProposal*
    perform(const TextEditor::AssistInterface *interface) Q_DECL_OVERRIDE;

    bool acceptIdle(const UaisoAssistInterface* interface) const;
};

class UaisoAssistInterface : public TextEditor::AssistInterface
{
public:
    UaisoAssistInterface(QTextDocument *textDocument,
                         int position,
                         const QString &fileName,
                         TextEditor::AssistReason reason,
                         uaiso::Factory* factory);

    uaiso::Factory* m_factory;
};

#endif
