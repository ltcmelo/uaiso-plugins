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

#include "uaisocompletion.h"

#include <texteditor/convenience.h>
#include <texteditor/codeassist/assistproposalitem.h>
#include <texteditor/codeassist/genericproposal.h>
#include <texteditor/codeassist/genericproposalmodel.h>

#include <QTextBlock>

    /* Uaiso - https://github.com/ltcmelo/uaiso
     *
     * Notice: This implementation has the only purpose of showcasing a few
     * components of the Uaiso project. It's not intended to be efficient,
     * nor to be taken as reference on how to write Qt Creator editors. It's
     * not even complete. The primary concern is to demonstrate Uaiso's API.
     */

#include <Ast/Ast.h>
#include <Parsing/Factory.h>
#include <Parsing/IncrementalLexer.h>
#include <Parsing/Lang.h>
#include <Parsing/LangName.h>
#include <Parsing/Lexeme.h>
#include <Parsing/LexemeMap.h>
#include <Parsing/Phrasing.h>
#include <Parsing/TokenMap.h>
#include <Parsing/Unit.h>
#include <Semantic/Binder.h>
#include <Semantic/CompletionProposer.h>
#include <Semantic/Manager.h>
#include <Semantic/Program.h>
#include <Semantic/Snapshot.h>
#include <Semantic/Symbol.h>
#include <Semantic/TypeChecker.h>

#include <algorithm>

using namespace UaisoQtc;
using namespace TextEditor;

    //--- Provider ---//

UaisoAssistProvider::UaisoAssistProvider(uaiso::Factory *factory)
    : m_lang(factory->makeLang())
{}

UaisoAssistProvider::~UaisoAssistProvider()
{}

bool UaisoAssistProvider::supportsEditor(Core::Id editorId) const
{
    return editorId == UaisoQtc::Constants::EDITOR_ID;
}

IAssistProcessor *UaisoAssistProvider::createProcessor() const
{
    return new UaisoAssistProcessor;
}

int UaisoAssistProvider::activationCharSequenceLength() const
{
    return std::max(m_lang->funcCallDelim().size(),
                    std::max(m_lang->memberAccessOprtr().size(),
                             m_lang->packageSeparator().size()));
}

bool UaisoAssistProvider::isActivationCharSequence(const QString &sequence) const
{
    const std::string& seq = sequence.toStdString();
    return seq == m_lang->funcCallDelim()
            || seq == m_lang->memberAccessOprtr()
            || seq == m_lang->packageSeparator();
}

    //--- Processor ---//

IAssistProposal* UaisoAssistProcessor::perform(const AssistInterface *assistInterface)
{
    auto interface = static_cast<const UaisoAssistInterface*>(assistInterface);
    if (interface->reason() == IdleEditor && !acceptIdle(interface))
        return nullptr;

    // Find the "triggering" token (this is, in fact, not generic enough).
    int offset = interface->position();
    QChar chr;
    do {
        chr = interface->characterAt(--offset);
    } while (chr.isLetter()
             || chr.isNumber()
             || chr == QLatin1Char('_'));
    ++offset;

    // In the text editor, line numbering is 1-based and column is 0-based.
    // In Uaiso, both of them are 0-based (we need a subtraction).
    int line, col;
    Convenience::convertPosition(interface->textDocument(), offset, &line, &col);
    --line;

    int actualLine = line;
    int actualCol = col;
    std::unique_ptr<uaiso::Lang> lang = interface->m_factory->makeLang();
    if (!lang->hasNewlineAsTerminator()) {
        bool sameLine = true;
        std::unique_ptr<uaiso::Phrasing> phrasing;
        QTextBlock block = interface->textDocument()->findBlock(offset);
        std::unique_ptr<uaiso::IncrementalLexer> lexer = interface->m_factory->makeIncrementalLexer();
        while (block.isValid()) {
            lexer->lex(block.text().toStdString() + "\n");
            phrasing.reset(lexer->releasePhrasing());
            if (!phrasing)
                return nullptr;
            if (!phrasing->isEmpty())
                break;
            --line;
            sameLine = false;
            block = block.previous();
        }
        if (!block.isValid())
            return nullptr;

        if (sameLine) {
            for (auto i = 0u; i < phrasing->size(); ++i) {
                auto lineCol = phrasing->lineCol(i);
                int endCol = lineCol.col_ + phrasing->length(i);
                if (endCol > col)
                    break;
                actualCol = endCol;
            }
        } else {
            auto lineCol = phrasing->lineCol(phrasing->size() - 1);
            actualLine = line;
            actualCol = lineCol.col_ + phrasing->length(phrasing->size() - 1);
        }
    }

    uaiso::LexemeMap lexemes;
    uaiso::TokenMap tokens;
    uaiso::Snapshot snapshot;
    uaiso::Manager manager;
    manager.config(interface->m_factory,
                   &tokens, &lexemes, snapshot);
    addSearchPaths(&manager, interface->m_factory->langName());
    std::unique_ptr<uaiso::Unit> unit = manager.process(
                interface->textDocument()->toPlainText().toStdString(),
                interface->fileName().toStdString(),
                uaiso::LineCol(actualLine, actualCol));

    if (!unit->ast())
        return nullptr;

    uaiso::TypeChecker checker(interface->m_factory);
    checker.setLexemes(&lexemes);
    checker.setTokens(&tokens);
    checker.check(Program_Cast(unit->ast()));

    uaiso::CompletionProposer proposer(interface->m_factory);
    auto result = proposer.propose(Program_Cast(unit->ast()), &lexemes);
    auto syms = std::get<0>(result);
    if (syms.empty())
        return nullptr;

    QList<TextEditor::AssistProposalItem *> items;
    std::for_each(syms.begin(), syms.end(), [&items] (const uaiso::Symbol* sym) {
        if (uaiso::isDecl(sym)) {
            AssistProposalItem *item = new AssistProposalItem;
            item->setText(QString::fromStdString(ConstDeclSymbol_Cast(sym)->name()->str()));
            items.append(item);
        } else if (sym->kind() == uaiso::Symbol::Kind::Namespace) {
            AssistProposalItem *item = new AssistProposalItem;
            item->setText(QString::fromStdString(ConstNamespace_Cast(sym)->name()->str()));
            items.append(item);
        }
    });

    GenericProposalModel* model = new GenericProposalModel;
    model->loadContent(items);

    return new GenericProposal(offset, model);
}

bool UaisoAssistProcessor::acceptIdle(const UaisoAssistInterface* interface) const
{
    int pos = interface->position();

    std::unique_ptr<uaiso::Lang> thesaurus = interface->m_factory->makeLang();
    std::string s(interface->characterAt(pos - 1).toLatin1(), 1);
    if (thesaurus->funcCallDelim() == s
            || thesaurus->memberAccessOprtr() == s
            || thesaurus->packageSeparator() == s) {
        return true;
    }

    const QChar chAt = interface->characterAt(pos);
    if (!(chAt.isNull() || chAt.isSpace()))
        return false;

    int count = 0;
    while (count < 3) {
        char ch = interface->characterAt(pos - 1 - count).toLatin1();
        if (!thesaurus->isIdentChar(ch))
            return false;
        ++count;
    }

    return true;
}

    //--- Assist Interface ---//

UaisoAssistInterface::UaisoAssistInterface(QTextDocument *textDocument,
                                           int position,
                                           const QString &fileName,
                                           AssistReason reason,
                                           uaiso::Factory *factory)
    : AssistInterface(textDocument, position, fileName, reason)
    , m_factory(factory)
{}
