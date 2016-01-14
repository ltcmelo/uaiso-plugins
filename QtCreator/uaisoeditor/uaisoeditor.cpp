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

#include "uaisoeditor.h"
#include "uaisocompletion.h"
#include "uaisosettings.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/icore.h>
#include <coreplugin/mimedatabase.h>
#include <coreplugin/navigationwidget.h>
#include <coreplugin/progressmanager/progressmanager.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditoractionhandler.h>
#include <texteditor/texteditorconstants.h>
#include <texteditor/texteditorsettings.h>
#include <texteditor/semantichighlighter.h>
#include <utils/qtcassert.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QFuture>
#include <QFutureInterface>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTextBlock>
#include <QtConcurrentRun>

    /* Uaiso - https://github.com/ltcmelo/uaiso
     *
     * Notice: This implementation has the only purpose of showcasing a few
     * components of the Uaiso project. It's not intended to be efficient,
     * nor to be taken as reference on how to write Qt Creator editors. It's
     * not even complete. The primary concern is to demonstrate Uaiso's API.
     */

#include <Ast/Ast.h>
#include <Ast/AstDumper.h>
#include <Parsing/Diagnostic.h>
#include <Parsing/Factory.h>
#include <Parsing/IncrementalLexer.h>
#include <Parsing/Lexeme.h>
#include <Parsing/Phrasing.h>
#include <Parsing/SourceLoc.h>
#include <Parsing/TokenCategory.h>
#include <Parsing/Unit.h>
#include <Semantic/Binder.h>
#include <Semantic/Manager.h>
#include <Semantic/Program.h>
#include <Semantic/Snapshot.h>
#include <Semantic/Symbol.h>
#include <Semantic/SymbolCollector.h>
#include <Semantic/TypeChecker.h>
#include <algorithm>


using namespace UaisoQtc;
using namespace TextEditor;

#define PLUGIN UaisoEditorPlugin::instance()

namespace {

const int kSyntaxCheckInterval = 200;
const int kSemanticCheckInterval = 100;

} // anonymous

    //--------------//
    //--- Plugin ---//
    //--------------//

UaisoEditorPlugin *UaisoEditorPlugin::m_instance = 0;

UaisoEditorPlugin::UaisoEditorPlugin()
    : m_settingsPage(new UaisoSettingsPage)
{
    m_instance = this;
}

UaisoEditorPlugin::~UaisoEditorPlugin()
{
    m_instance = 0;
}

UaisoEditorPlugin *UaisoEditorPlugin::instance()
{
    return m_instance;
}

bool UaisoEditorPlugin::initialize(const QStringList & /*arguments*/,
                                   QString *errorMessage)
{
    if (!Core::MimeDatabase::addMimeTypes(QLatin1String(
            ":/uaisoeditor/UaisoEditor.mimetypes.xml"), errorMessage)) {
        return false;
    }

    addAutoReleasedObject(new UaisoEditorFactory);
    addAutoReleasedObject(m_settingsPage);

    return true;
}

void UaisoEditorPlugin::extensionsInitialized()
{}

UaisoSettingsPage *UaisoEditorPlugin::settingsPage()
{
    return m_settingsPage;
}

    //---------------//
    //--- Factory ---//
    //---------------//

UaisoEditorFactory::UaisoEditorFactory()
{
    setId(Constants::EDITOR_ID);
    setDisplayName(tr(Constants::EDITOR_DISPLAY_NAME));

    UaisoSettings settings;
    settings.load(Core::ICore::settings());
    if (settings.m_options[static_cast<int>(uaiso::LangName::D)].m_enabled)
        addMimeType(QLatin1String(Constants::D_MIMETYPE));
    if (settings.m_options[static_cast<int>(uaiso::LangName::Go)].m_enabled)
        addMimeType(QLatin1String(Constants::GO_MIMETYPE));
    if (settings.m_options[static_cast<int>(uaiso::LangName::Py)].m_enabled)
        addMimeType(QLatin1String(Constants::PY_MIMETYPE));
    //addMimeType(QLatin1String(Constants::RUST_MIMETYPE));

    setEditorActionHandlers(TextEditorActionHandler::Format
                            | TextEditorActionHandler::UnCommentSelection
                            | TextEditorActionHandler::UnCollapseAll);

    setDocumentCreator([]() { return new UaisoEditorDocument; });
    setEditorWidgetCreator([]() { return new UaisoEditorWidget; });
    setEditorCreator([]() { return new UaisoEditor; });
    addHoverHandler(new UaisoHoverHandler);
    setMarksVisible(true);
    setParenthesesMatchingEnabled(true);
    setCodeFoldingSupported(true);
    setCommentStyle(Utils::CommentDefinition::HashStyle);
}

    //----------------//
    //--- Document ---//
    //----------------//

UaisoEditorDocument::UaisoEditorDocument()
    : m_reports(nullptr)
{
    setId(Constants::EDITOR_ID);

    m_syntaxCheckTimer.setSingleShot(true);
    m_syntaxCheckTimer.setInterval(kSyntaxCheckInterval);
    connect(&m_syntaxCheckTimer, SIGNAL(timeout()), this, SLOT(parse()));
    connect(this, SIGNAL(contentsChanged()), this, SLOT(triggerAnalysis()));

    m_semanticCheckTimer.setSingleShot(true);
    m_semanticCheckTimer.setInterval(kSemanticCheckInterval);
    connect(&m_semanticCheckTimer, SIGNAL(timeout()), this, SLOT(bindAndCheck()));

    connect(this, SIGNAL(filePathChanged(QString,QString)),
            this, SLOT(configure(QString,QString)));

    connect(TextEditorSettings::instance(), SIGNAL(fontSettingsChanged(TextEditor::FontSettings)),
            this, SLOT(updateFontSettings(TextEditor::FontSettings)));
}

UaisoEditorDocument::~UaisoEditorDocument()
{
    if (m_watcher) {
        disconnectWatcher();
        m_watcher->cancel();
        m_watcher->waitForFinished();
    }
}

void UaisoEditorDocument::configure(const QString &oldPath, const QString &path)
{
    Q_UNUSED(oldPath);

    QFileInfo fileInfo(path);
    const QString& suffix = fileInfo.suffix();
    if (suffix == QLatin1String("d"))
        m_factory = uaiso::FactoryCreator::create(uaiso::LangName::D);
    else if (suffix == QLatin1String("go"))
        m_factory = uaiso::FactoryCreator::create(uaiso::LangName::Go);
    else if (suffix == QLatin1String("py"))
        m_factory = uaiso::FactoryCreator::create(uaiso::LangName::Py);

    m_unit = m_factory->makeUnit();

    delete completionAssistProvider();
    setCompletionAssistProvider(new UaisoAssistProvider(m_factory.get()));
    setSyntaxHighlighter(new UaisoSyntaxHighlighter(m_factory.get()));
    setFilePath(path);
    updateFontSettings(fontSettings());
}

void UaisoEditorDocument::updateFontSettings(const TextEditor::FontSettings& fs)
{
    m_kindToFormat[static_cast<int>(uaiso::Symbol::Kind::Record)] =
            fs.toTextCharFormat(TextEditor::C_TYPE);
    m_kindToFormat[static_cast<int>(uaiso::Symbol::Kind::Var)] =
            fs.toTextCharFormat(TextEditor::C_FIELD);
    m_kindToFormat[static_cast<int>(uaiso::Symbol::Kind::EnumItem)] =
            fs.toTextCharFormat(TextEditor::C_ENUMERATION);
    m_kindToFormat[static_cast<int>(uaiso::Symbol::Kind::Func)] =
            fs.toTextCharFormat(TextEditor::C_FUNCTION);
}

void UaisoEditorDocument::triggerAnalysis()
{
    m_syntaxCheckTimer.start(kSyntaxCheckInterval);
    m_semanticCheckTimer.stop();
}

void UaisoEditorDocument::parse()
{
    m_syntaxCheckTimer.stop();

    PLUGIN->tokens()->clear(filePath().toStdString());
    PLUGIN->lexemes()->clear(filePath().toStdString());

    std::string code = plainText().toStdString();
    m_unit->assignInput(code);
    m_unit->setFileName(filePath().toStdString());
    m_unit->parse(PLUGIN->tokens(), PLUGIN->lexemes());
    m_reports.reset(m_unit->releaseReports());

    emit requestDiagnosticsUpdate();

    if (m_unit->ast())
        m_semanticCheckTimer.start(kSemanticCheckInterval);
}

void UaisoEditorDocument::bindAndCheck()
{
    m_semanticCheckTimer.stop();

    uaiso::ProgramAst* progAst = Program_Cast(m_unit->ast());
    if (!progAst)
        return;

    // Create symbols.
    uaiso::Binder binder(m_factory.get());
    binder.setLexemes(PLUGIN->lexemes());
    binder.setTokens(PLUGIN->tokens());
    binder.collectDiagnostics(m_reports.get());
    std::unique_ptr<uaiso::Program> prog(binder.bind(progAst, m_unit->fileName()));

    if (!prog || prog->env().isEmpty())
        return;

    PLUGIN->snapshot().insertOrReplace(m_unit->fileName(), std::move(prog));

    // Analyse dependencies.
    uaiso::Manager manager;
    manager.config(m_factory.get(),
                   PLUGIN->tokens(),
                   PLUGIN->lexemes(),
                   PLUGIN->snapshot());
    addSearchPaths(&manager, m_factory->langName());
    manager.processDeps(m_unit->fileName());

    // Type checking.
    uaiso::TypeChecker typeChecker(m_factory.get());
    typeChecker.setLexemes(PLUGIN->lexemes());
    typeChecker.setTokens(PLUGIN->tokens());
    typeChecker.collectDiagnostics(m_reports.get());
    typeChecker.check(Program_Cast(m_unit->ast()));

    emit requestDiagnosticsUpdate();

    processSemanticData();
}


namespace {

class SymbolCollectorWrapper :
        public QRunnable,
        public QFutureInterface<TextEditor::HighlightingResult>
{
private:
    uaiso::Factory* m_factory { nullptr };
    uaiso::ProgramAst* m_progAst { nullptr } ;

public:
    SymbolCollectorWrapper(uaiso::Factory* factory,
                           uaiso::ProgramAst* progAst)
        : m_factory(factory), m_progAst(progAst)
    {}

    void run()
    {
        uaiso::SymbolCollector collector(m_factory);
        auto refs = collector.collect(m_progAst, PLUGIN->lexemes());

        if (!refs.empty()) {
            QVector<HighlightingResult> results;
            for (auto ref : refs) {
                auto sym = std::get<1>(ref);
                auto loc = std::get<2>(ref);
                HighlightingResult result(loc.line_ + 1, loc.col_ + 1,
                                          loc.lastCol_ - loc.col_,
                                          static_cast<int>(sym->kind()));
                results.append(result);
            }

            // Results are expected to be sorted.
            std::sort(results.begin(), results.end(),
                      [](const HighlightingResult& a, const HighlightingResult& b) {
                return a.line != b.line ? a.line < b.line : a.column < b.column;
            }
            );
            reportResults(results);
        }

        reportFinished();
    }

    typedef TextEditor::HighlightingResult Result;
    typedef QFuture<Result> Future;

    Future start()
    {
        this->setRunnable(this);
        this->reportStarted();
        Future future = this->future();
        QThreadPool::globalInstance()->start(this, QThread::LowestPriority);
        return future;
    }
};

} // anonymous

void UaisoEditorDocument::processSemanticData()
{
    if (m_watcher) {
        disconnectWatcher();
        m_watcher->cancel();
    }

    m_semanticRevision = document()->revision();
    m_watcher.reset(new QFutureWatcher<TextEditor::HighlightingResult>);
    connect(m_watcher.get(), SIGNAL(resultsReadyAt(int,int)),
            this, SLOT(semanticDataAvailable(int,int)));
    connect(m_watcher.get(), SIGNAL(finished()),
            this, SLOT(semanticDataFinished()));

    SymbolCollectorWrapper *collector =
            new SymbolCollectorWrapper(m_factory.get(), Program_Cast(m_unit->ast()));
    m_watcher->setFuture(collector->start());
}

void UaisoEditorDocument::disconnectWatcher()
{
    disconnect(m_watcher.get(), SIGNAL(resultsReadyAt(int,int)),
               this, SLOT(semanticDataAvailable(int,int)));
    disconnect(m_watcher.get(), SIGNAL(finished()),
               this, SLOT(semanticDataFinished()));
}

void UaisoEditorDocument::semanticDataAvailable(int from, int to)
{
    if (document()->revision() != m_semanticRevision)
        return;

    if (!m_watcher || m_watcher->isCanceled())
        return;

    SyntaxHighlighter *highlighter = syntaxHighlighter();
    QTC_CHECK(highlighter);
    SemanticHighlighter::incrementalApplyExtraAdditionalFormats(
                highlighter, m_watcher->future(), from, to, m_kindToFormat);
}

void UaisoEditorDocument::semanticDataFinished()
{
    if (!m_watcher->isCanceled()
            && document()->revision() == m_semanticRevision) {
        SyntaxHighlighter *highlighter = syntaxHighlighter();
        QTC_CHECK(highlighter);
        SemanticHighlighter::clearExtraAdditionalFormatsUntilEnd(
               highlighter, m_watcher->future());
    }
    m_watcher.reset();
}

    //--------------//
    //--- Editor ---//
    //--------------//

UaisoEditor::UaisoEditor()
{}

UaisoEditor::~UaisoEditor()
{}

    //--------------//
    //--- Widget ---//
    //--------------//

UaisoEditorWidget::UaisoEditorWidget()
{}

UaisoEditorWidget::~UaisoEditorWidget()
{}

AssistInterface *
UaisoEditorWidget::createAssistInterface(AssistKind kind,
                                         AssistReason reason) const
{
    if (kind == Completion) {
        auto doc = static_cast<UaisoEditorDocument*>(textDocument());
        return new UaisoAssistInterface(document(),
                                        position(),
                                        textDocument()->filePath(),
                                        reason,
                                        doc->m_factory.get());
    }
    return TextEditorWidget::createAssistInterface(kind, reason);
}

void UaisoEditorWidget::finalizeInitialization()
{
    UaisoEditorDocument* doc = static_cast<UaisoEditorDocument*>(textDocument());

    connect(doc, SIGNAL(requestDiagnosticsUpdate()),
            this, SLOT(updateDiagnostics()));
}

void UaisoEditorWidget::updateDiagnostics()
{
    UaisoEditorDocument* doc = static_cast<UaisoEditorDocument*>(textDocument());
    if (!doc->m_reports)
        return;

    QList<QTextEdit::ExtraSelection> selecs;
    for (const uaiso::DiagnosticReport& report : *doc->m_reports) {
        const int line = report.sourceLoc().line_;
        const int column = report.sourceLoc().col_;

        QTextEdit::ExtraSelection selec;
        QTextCursor c(document()->findBlockByNumber(line));
        selec.cursor = c;
        selec.cursor.setPosition(c.position() + column);

        int length = report.sourceLoc().lastCol_ - report.sourceLoc().col_;
        if (!length) {
            if (selec.cursor.atBlockEnd())
                selec.cursor.movePosition(QTextCursor::StartOfWord,
                                          QTextCursor::KeepAnchor);
            else
                selec.cursor.movePosition(QTextCursor::EndOfWord,
                                          QTextCursor::KeepAnchor);
        } else {
            selec.cursor.movePosition(QTextCursor::NextCharacter,
                                      QTextCursor::KeepAnchor, length);
        }

        if (report.diagnostic().severity() == uaiso::Severity::Warning)
            selec.format.setUnderlineColor(Qt::darkYellow);
        else
            selec.format.setUnderlineColor(Qt::red);

        selec.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        selec.format.setToolTip(QString::fromStdString(report.diagnostic().desc()));

        selecs.append(selec);
    }
    setExtraSelections(CodeWarningsSelection, selecs);
}

    //------------------------//
    //--- Syntax Highlight ---//
    //------------------------//

namespace {

const int kNumericFormat = 0;
const int kStringFormat = 1;
const int kBuiltinFormat = 2;
const int kKeywordFormat = 3;
const int kOperatorFormat = 4;
const int kCommentFormat = 6;

} // anonymous

UaisoSyntaxHighlighter::UaisoSyntaxHighlighter(uaiso::Factory *factory)
    : m_lexer(factory->makeIncrementalLexer())
{
    static QVector<TextEditor::TextStyle> categories;
    if (categories.isEmpty()) {
        categories << TextEditor::C_NUMBER
                   << TextEditor::C_STRING
                   << TextEditor::C_TYPE
                   << TextEditor::C_KEYWORD
                   << TextEditor::C_OPERATOR
                   << TextEditor::C_LABEL
                   << TextEditor::C_COMMENT
                   << TextEditor::C_VISUAL_WHITESPACE;
    }
    setTextFormatCategories(categories);
}

UaisoSyntaxHighlighter::~UaisoSyntaxHighlighter()
{}

void UaisoSyntaxHighlighter::highlightBlock(const QString &text)
{
    int state = previousBlockState();
    if (state == -1)
        state = uaiso::IncrementalLexer::State::InCode;

    m_lexer->lex(text.toStdString(), uaiso::IncrementalLexer::State(state));

    std::unique_ptr<uaiso::Phrasing> phrasing(m_lexer->releasePhrasing());
    if (!phrasing)
        return;

    for (size_t i = 0; i < phrasing->size(); ++i) {
        uaiso::Token tk = phrasing->token(i);
        int pos = phrasing->lineCol(i).col_;
        int leng = phrasing->length(i);
        if (uaiso::isBuiltin(tk)) {
            setFormat(pos, leng, formatForCategory(kBuiltinFormat));
        } else if (uaiso::isKeyword(tk)) {
            setFormat(pos, leng, formatForCategory(kKeywordFormat));
        } else if (uaiso::isOprtr(tk)) {
            setFormat(pos, leng, formatForCategory(kOperatorFormat));
        } else if (uaiso::isNumLit(tk)) {
            setFormat(pos, leng, formatForCategory(kNumericFormat));
        } else if (uaiso::isStrLit(tk)) {
            setFormat(pos, leng, formatForCategory(kStringFormat));
        } else if (uaiso::isComment(tk)) {
            setFormat(pos, leng, formatForCategory(kCommentFormat));
        }
    }

    setCurrentBlockState(m_lexer->state());
}

    //-------------//
    //--- Hover ---//
    //-------------//

void UaisoHoverHandler::identifyMatch(TextEditorWidget *widget, int pos)
{
    foreach (const QTextEdit::ExtraSelection &selection,
             widget->extraSelections(TextEditorWidget::CodeWarningsSelection)) {
        if (pos >= selection.cursor.selectionStart()
                && pos <= selection.cursor.selectionEnd()) {
            setToolTip(selection.format.toolTip());
            return;
        }
    }
}

void UaisoQtc::addSearchPaths(uaiso::Manager* manager, uaiso::LangName lang)
{
    UaisoSettings settings;
    settings.load(Core::ICore::settings());
    QString paths = settings.m_options[static_cast<int>(lang)].m_systemPaths;
    if (!paths.isEmpty()) {
        const QStringList& pathList = paths.split(QLatin1Char(':'));
        foreach (const QString& path, pathList)
            manager->addSearchPath(path.toStdString());
    }
}
